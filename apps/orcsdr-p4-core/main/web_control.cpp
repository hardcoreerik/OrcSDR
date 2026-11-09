#include "web_control.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>
#include "commands.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_rtl_sdr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "home_app.hpp"
#include "radio_service.hpp"
#include "web_audio.hpp"
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
  radio::clear_tune_error(rx);
  if (rx == 0) home_app::request_frequency(hz);  // the Home screen owns receiver 0's tuning, as for the serial commands
  else radio::tune_to(rx, hz);
}

// Each dongle's range is enforced by the driver (ESP_RTL_SDR_ERR_BAD_FREQ for a frequency this dongle cannot do). Give it a moment to answer, then say so
// instead of reporting a success that did not happen.
bool driver_rejected(int rx, uint32_t hz) {
  for (int i = 0; i < 12; ++i) {
    vTaskDelay(pdMS_TO_TICKS(40));
    uint32_t bad = 0;
    if (radio::tune_error(rx, &bad) != 0 && bad == hz) return true;
    radio::ReceiverInfo ri;
    if (radio::receiver_info(rx, &ri) && ri.frequency_hz == hz) return false;
  }
  return false;
}
}  // namespace

uint32_t tune_min_hz(int rx) { return radio::tune_min_hz(rx); }

Result run(const char* route, const char* query, uint32_t peer, char* out, size_t cap) {
  long long v = 0;
  if (std::strcmp(route, "receivers/tune") == 0) {
    const int rx = receiver_index(query);
    if (rx < 0 || !parse_long(query, "hz", &v) || v < tune_min_hz(rx) || v > ESP_RTL_SDR_FREQ_MAX_HZ) return fail(out, cap, Result::bad_request, "bad_hz_or_index");
    tune(rx, static_cast<uint32_t>(v));
    if (driver_rejected(rx, static_cast<uint32_t>(v))) return fail(out, cap, Result::bad_request, "driver_rejected_frequency");
    char f[64];
    std::snprintf(f, sizeof(f), "\"index\":%d,\"hz\":%lld", rx, v);
    return done(out, cap, f);
  }
  if (std::strcmp(route, "receivers/rate") == 0) {
    const int rx = receiver_index(query);
    if (rx < 0 || !parse_long(query, "sps", &v) || v < 0 || v > 0xFFFFFFFFll) return fail(out, cap, Result::bad_request, "bad_sps_or_index");
    if (v == 0) {  // sps=0 hands the rate back to the console's own policy (the Home screen picks it again)
      radio::set_sample_rate_pin(rx, 0);
      char f[64];
      std::snprintf(f, sizeof(f), "\"index\":%d,\"requested_sps\":0,\"applied_sps\":0", rx);
      return done(out, cap, f);
    }
    // The driver decides what it can run: it quantises the request to the nearest rate it can program, and the applied rate is reported.
    uint32_t applied = 0;
    if (!esp_rtl_sdr_quantize_sample_rate(static_cast<uint32_t>(v), &applied)) return fail(out, cap, Result::bad_request, "sps_not_supported_by_the_driver");
    radio::set_sample_rate_pin(rx, applied);  // pinned: the Home screen would otherwise put its own rate back within a moment
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
    // This is the page's browser-audio session (web_audio). It never touches the board's own speaker, volume or mute: the volume here is the page's own.
    if (std::strcmp(action, "volume") == 0) {
      if (!parse_long(query, "volume", &v) || v < 0 || v > 80) return fail(out, cap, Result::bad_request, "bad_volume");
      if (!web_audio::set_volume(static_cast<int>(v))) return fail(out, cap, Result::not_available, "no_audio_session");
      return done(out, cap);
    }
    if (std::strcmp(action, "start") == 0) {
      const int rx = receiver_index(query);
      if (rx < 0 || !parse_long(query, "hz", &v) || v <= 0 || v > 0xFFFFFFFFll) return fail(out, cap, Result::bad_request, "bad_hz_or_index");
      long long sps = 0, vol = 30;
      char probe[4];
      if (httpd_query_key_value(query, "sps", probe, sizeof(probe)) != ESP_ERR_NOT_FOUND && !parse_long(query, "sps", &sps)) return fail(out, cap, Result::bad_request, "bad_sps");
      parse_long(query, "volume", &vol);
      char mode[8] = {};
      httpd_query_key_value(query, "mode", mode, sizeof(mode));
      const web_audio::Result s = web_audio::start(rx, static_cast<uint32_t>(v), sps < 0 ? 0 : static_cast<uint32_t>(sps), static_cast<int>(vol), mode, peer, out, cap);
      return s == web_audio::Result::ok ? Result::ok : s == web_audio::Result::bad_request ? Result::bad_request : Result::not_available;
    }
    if (std::strcmp(action, "stop") == 0) {
      web_audio::stop();
      return done(out, cap);
    }
    return fail(out, cap, Result::bad_request, "bad_action");
  }
  if (std::strcmp(route, "home") == 0) {
    // A Home action from the page: the same handler a touch reaches, VFO actions only (see home_app::request_web_action). Never the board's speaker or screen.
    char action[24] = {};
    if (httpd_query_key_value(query, "action", action, sizeof(action)) != ESP_OK) return fail(out, cap, Result::bad_request, "bad_action");
    long long hv = 0;
    parse_long(query, "value", &hv);
    if (hv < 0 || hv > 0xFFFFFFFFll) return fail(out, cap, Result::bad_request, "bad_value");
    switch (home_app::request_web_action(action, static_cast<uint32_t>(hv))) {
      case home_app::WebAction::ok: {
        char f[64];
        std::snprintf(f, sizeof(f), "\"action\":\"%s\"", action);
        return done(out, cap, f);
      }
      case home_app::WebAction::bad_value: return fail(out, cap, Result::bad_request, "bad_value");
      case home_app::WebAction::not_allowed: return fail(out, cap, Result::not_available, "not_allowed_from_the_web");
      case home_app::WebAction::busy: return fail(out, cap, Result::not_available, "home_not_ready");
      default: return fail(out, cap, Result::bad_request, "unknown_action");
    }
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
    long long wide_center = 0, wide_sps = 0;  // the wide window that decoders want: `center` and `rx_sps` come together
    const bool has_center = httpd_query_key_value(query, "center", scratch, sizeof(scratch)) == ESP_OK, has_sps = httpd_query_key_value(query, "rx_sps", scratch, sizeof(scratch)) == ESP_OK;
    if (has_center || has_sps) {
      if (!parse_long(query, "center", &wide_center) || !parse_long(query, "rx_sps", &wide_sps) || wide_center <= 0 || wide_center > 0xFFFFFFFFll || wide_sps <= 0 || wide_sps > 3200000)
        return fail(out, cap, Result::bad_request, "bad_wide_window");
    }
    const web_iq::Start s = web_iq::start_channel(rx, static_cast<uint32_t>(v), static_cast<uint32_t>(wide_center), static_cast<uint32_t>(wide_sps), peer, out, cap);
    return s == web_iq::Start::ok ? Result::ok : s == web_iq::Start::bad_request ? Result::bad_request : Result::not_available;
  }
  if (std::strcmp(route, "channel/stop") == 0) {
    web_iq::stop_channel();
    return done(out, cap);
  }
  return Result::unknown_route;
}

}  // namespace web_control
