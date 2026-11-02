#include "web_control.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>
#include "commands.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "home_app.hpp"
#include "radio_service.hpp"
#include "web_iq.hpp"

namespace web_control {
namespace {
constexpr char TAG[] = "WEB_CTL";
namespace radio = orcsdr::radio;

bool parse_long(const char* query, const char* key, long long* out) {
  char value[24];
  if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) return false;
  char* end = nullptr;
  const long long v = std::strtoll(value, &end, 10);
  if (end == value || *end != '\0') return false;
  *out = v;
  return true;
}

int receiver_index(const char* query) {
  long long v = 0;
  if (!parse_long(query, "index", &v)) return 0;
  return v >= 0 && v < radio::max_receivers() ? static_cast<int>(v) : -1;
}

Result fail(char* out, size_t cap, Result r, const char* message) {
  std::snprintf(out, cap, "{\"ok\":false,\"error\":\"%s\"}", message);
  return r;
}
Result done(char* out, size_t cap, const char* fields = "") {
  std::snprintf(out, cap, "{\"ok\":true%s%s}", fields[0] ? "," : "", fields);
  return Result::ok;
}

void tune(int rx, uint32_t hz) {
  if (rx == 0) home_app::request_frequency(hz);  // the Home screen owns receiver 0's tuning, as for the serial commands
  else radio::tune_to(rx, hz);
}
}  // namespace

Result run(const char* route, const char* query, uint32_t peer, char* out, size_t cap) {
  long long v = 0;
  if (std::strcmp(route, "receivers/tune") == 0) {
    const int rx = receiver_index(query);
    if (rx < 0 || !parse_long(query, "hz", &v) || v < orcsdr::commands::kTuneMinHz || v > orcsdr::commands::kTuneMaxHz) return fail(out, cap, Result::bad_request, "bad_hz_or_index");
    tune(rx, static_cast<uint32_t>(v));
    char f[64];
    std::snprintf(f, sizeof(f), "\"index\":%d,\"hz\":%lld", rx, v);
    return done(out, cap, f);
  }
  if (std::strcmp(route, "receivers/rate") == 0) {
    const int rx = receiver_index(query);
    if (rx < 0 || !parse_long(query, "sps", &v) || v < 225000 || v > 3200000) return fail(out, cap, Result::bad_request, "bad_sps_or_index");
    // The console runs two rates (960 kS/s and 2.4 MS/s); the nearest is applied and reported, never silently substituted.
    const uint32_t applied = v <= 1000000 ? 960000 : 2400000;
    radio::set_sample_rate(rx, applied);
    char f[96];
    std::snprintf(f, sizeof(f), "\"index\":%d,\"requested_sps\":%lld,\"applied_sps\":%u", rx, v, static_cast<unsigned>(applied));
    return done(out, cap, f);
  }
  if (std::strcmp(route, "tuner/gain") == 0) {
    const int rx = receiver_index(query);
    char mode[12] = {};
    if (rx < 0 || httpd_query_key_value(query, "mode", mode, sizeof(mode)) != ESP_OK) return fail(out, cap, Result::bad_request, "bad_mode_or_index");
    const radio::Gain g = radio::gain(rx);
    if (std::strcmp(mode, "auto") == 0) {
      if (!g.auto_available) return fail(out, cap, Result::not_available, "auto_gain_not_available");
      radio::set_gain_auto(rx);
    } else if (std::strcmp(mode, "manual") == 0) {
      if (!g.available) return fail(out, cap, Result::not_available, "manual_gain_not_available");
      radio::set_gain_manual(rx, parse_long(query, "tenth_db", &v) ? static_cast<int>(v) : g.tenth_db);
    } else {
      return fail(out, cap, Result::bad_request, "bad_mode");
    }
    return done(out, cap);
  }
  if (std::strcmp(route, "tuner/agc") == 0) {
    const int rx = receiver_index(query);
    if (rx < 0 || !parse_long(query, "on", &v)) return fail(out, cap, Result::bad_request, "bad_on_or_index");
    if (!radio::gain(rx).rtl_agc_available) return fail(out, cap, Result::not_available, "rtl_agc_not_available");
    radio::set_rtl_agc(rx, v != 0);
    return done(out, cap);
  }
  if (std::strcmp(route, "fm") == 0) {
    char action[12] = {};
    if (httpd_query_key_value(query, "action", action, sizeof(action)) != ESP_OK) return fail(out, cap, Result::bad_request, "bad_action");
    if (std::strcmp(action, "volume") == 0) {
      if (!parse_long(query, "volume", &v) || v < 0 || v > orcsdr::commands::kVolumeMax) return fail(out, cap, Result::bad_request, "bad_volume");
      home_app::request_volume(static_cast<uint8_t>(v * 255 / orcsdr::commands::kVolumeMax));
      return done(out, cap);
    }
    if (std::strcmp(action, "start") == 0) {
      const int rx = receiver_index(query);
      if (rx < 0 || !parse_long(query, "hz", &v) || v < orcsdr::commands::kTuneMinHz || v > orcsdr::commands::kTuneMaxHz) return fail(out, cap, Result::bad_request, "bad_hz_or_index");
      tune(rx, static_cast<uint32_t>(v));  // tuning only: audio from this node is not streamed to the browser yet
      long long vol = 0;
      if (parse_long(query, "volume", &vol) && vol >= 0 && vol <= orcsdr::commands::kVolumeMax) home_app::request_volume(static_cast<uint8_t>(vol * 255 / orcsdr::commands::kVolumeMax));
      return done(out, cap, "\"audio_stream\":false");
    }
    if (std::strcmp(action, "stop") == 0) return done(out, cap);
    return fail(out, cap, Result::bad_request, "bad_action");
  }
  if (std::strcmp(route, "time") == 0) {
    if (!parse_long(query, "epoch", &v) || v < 1700000000LL || v > 4102444800LL) return fail(out, cap, Result::bad_request, "bad_epoch");
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(v);
    if (settimeofday(&tv, nullptr) != 0) return fail(out, cap, Result::not_available, "clock_not_settable");
    ESP_LOGI(TAG, "WEB_CLOCK_SET epoch=%lld (from the browser)", v);
    return done(out, cap);
  }
  if (std::strcmp(route, "channel") == 0) {
    const int rx = receiver_index(query);
    char scratch[8];
    if (rx < 0 || !parse_long(query, "hz", &v) || v <= 0 || v > 0xFFFFFFFFll) return fail(out, cap, Result::bad_request, "bad_hz_or_index");
    if (httpd_query_key_value(query, "center", scratch, sizeof(scratch)) == ESP_OK || httpd_query_key_value(query, "rx_sps", scratch, sizeof(scratch)) == ESP_OK)
      return fail(out, cap, Result::not_available, "wide_channel_not_available");  // decoders that take a wide IQ window are not served yet
    const web_iq::Start s = web_iq::start_channel(rx, static_cast<uint32_t>(v), peer, out, cap);
    return s == web_iq::Start::ok ? Result::ok : s == web_iq::Start::bad_request ? Result::bad_request : Result::not_available;
  }
  if (std::strcmp(route, "channel/stop") == 0) {
    web_iq::stop_channel();
    return done(out, cap);
  }
  return Result::unknown_route;
}

}  // namespace web_control
