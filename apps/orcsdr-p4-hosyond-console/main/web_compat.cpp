#include "web_compat.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_rtl_sdr.h"
#include "esp_timer.h"
#include "orcsdr_board_profile.h"
#include "radio_service.hpp"
#include "sd_card.hpp"
#include "wifi_app.hpp"

namespace web_compat {
namespace {
constexpr uint32_t kSyncedAfterEpoch = 1700000000u;  // the page treats earlier clocks as unset

struct Out {
  char* p;
  size_t cap, len = 0;
  bool overflow = false;
  void raw(const char* s) {
    const size_t n = std::strlen(s);
    if (len + n + 1 > cap) {
      overflow = true;
      return;
    }
    std::memcpy(p + len, s, n);
    len += n;
    p[len] = '\0';
  }
  void fmt(const char* f, ...) __attribute__((format(printf, 2, 3))) {
    if (overflow) return;
    va_list ap;
    va_start(ap, f);
    const int n = std::vsnprintf(p + len, cap - len, f, ap);
    va_end(ap);
    if (n < 0 || static_cast<size_t>(n) + 1 > cap - len) overflow = true;
    else len += static_cast<size_t>(n);
  }
  void str(const char* s) {  // JSON string with escaping
    raw("\"");
    for (; s && *s; ++s) {
      const unsigned char c = static_cast<unsigned char>(*s);
      if (c == '"' || c == '\\') {
        const char two[3] = {'\\', static_cast<char>(c), 0};
        raw(two);
      } else if (c < 0x20) {
        fmt("\\u%04x", c);
      } else {
        const char one[2] = {static_cast<char>(c), 0};
        raw(one);
      }
    }
    raw("\"");
  }
};

bool clock_synced() { return static_cast<uint32_t>(std::time(nullptr)) > kSyncedAfterEpoch; }

// The page's receiver states: 0 absent, 1 present, 2 claimed, 3 streaming, 4 error. Ours: 0 none, 1 scanning, 2 streaming, 3 error.
int page_state(int ours) { return ours == 2 ? 3 : ours == 3 ? 4 : 0; }

void receivers(Out& o) {
  static uint32_t prev_drops[orcsdr::radio::kMaxReceivers];
  o.raw("{\"receivers\":[");
  for (int i = 0; i < orcsdr::radio::kMaxReceivers; ++i) {
    if (i) o.raw(",");
    orcsdr::radio::ReceiverInfo ri;
    const bool have = i < orcsdr::radio::max_receivers() && orcsdr::radio::receiver_info(i, &ri);
    const bool present = have && (ri.state == 2 || ri.state == 3);
    if (!present) {
      o.fmt("{\"index\":%d,\"present\":false,\"state\":0}", i);
      continue;
    }
    const uint32_t recent = ri.drops >= prev_drops[i] ? ri.drops - prev_drops[i] : ri.drops;
    prev_drops[i] = ri.drops;
    const orcsdr::radio::Gain g = orcsdr::radio::gain(i);
    o.fmt("{\"index\":%d,\"present\":true,\"state\":%d,\"product\":", i, page_state(ri.state));
    o.str(ri.device);
    o.raw(",\"label\":");
    o.str(ri.device);
    o.raw(",\"tuner_chip\":\"\",\"scan_profile\":\"\",\"scan_run\":\"idle\",\"scan_baseline_ready\":false,\"serial\":\"\",\"tuner\":\"\",");
    o.fmt("\"center_hz\":%u,\"sample_rate\":%u,\"usb_bytes_received\":0,\"iq_bytes_delivered\":0,\"iq_bytes_consumed\":0,", unsigned(ri.frequency_hz),
          unsigned(ri.sample_rate));
    o.fmt("\"drops\":%u,\"drops_recent\":%u,\"usb_errors\":0,\"usb_addr\":0,\"usb_path\":", unsigned(ri.drops), unsigned(recent));
    o.str(ri.usb_path);
    // last_iq_age_ms: the P4 console does not time individual blocks; a streaming receiver reports 0 (fresh).
    o.fmt(",\"hub_port\":0,\"gain_tenth_db\":%d,\"last_iq_age_ms\":%d,\"last_error\":\"\"}", g.tenth_db, ri.state == 2 ? 0 : -1);
  }
  o.raw("]}");
}

// The FM service of the other firmware is not part of this console: report "not playing" so the page stays on the receiver view.
void fm(Out& o) {
  o.fmt("{\"supported\":false,\"playing\":false,\"receiver\":-1,\"frequency_hz\":0,\"input_sps\":0,\"audio_sps\":48000,\"mode\":\"mono\",\"volume\":0,"
        "\"rf_dbfs\":-120.0,\"uptime_us\":%llu,\"browser_audio\":{\"available\":false,\"port\":81,\"clients\":0}}",
        static_cast<unsigned long long>(esp_timer_get_time()));
}

void report(Out& o) {
  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);
  char node[24];
  std::snprintf(node, sizeof(node), "orcsdr-p4-%02x%02x%02x", mac[3], mac[4], mac[5]);
  const wifi_app::Status w = wifi_app::status();
  const sd_card::Status s = sd_card::status();
  o.raw("{\"node_id\":");
  o.str(node);
  o.raw(",\"firmware\":");
  char fw[40];
  std::snprintf(fw, sizeof(fw), "P4 console %s", __DATE__);
  o.str(fw);
  o.raw(",\"board\":");
  o.str(orcsdr_board_profile()->name);
  o.fmt(",\"uptime_us\":%lld,\"eth\":{\"link\":false,\"ip\":\"\"},\"wifi\":{\"started\":%s,\"connected\":%s,\"got_ip\":%s,\"ip\":", static_cast<long long>(esp_timer_get_time()),
        w.started ? "true" : "false", w.connected ? "true" : "false", w.connected && w.ip[0] ? "true" : "false");
  o.str(w.ip);
  o.raw(",\"ssid\":");
  o.str(w.ssid);
  o.fmt(",\"rssi\":%d,\"err\":\"\"},\"memory\":{\"internal_free\":%u,\"dma_free\":%u,\"psram_free\":%u},", int(w.rssi),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  o.fmt("\"sd\":{\"mounted\":%s,\"free_bytes\":0,\"writes_ok\":0,\"writes_fail\":0},\"iq\":{\"streaming\":%d,\"drops\":0}}", s.mounted ? "true" : "false",
        orcsdr::radio::receiver_count());
}

void tuner(Out& o, int rx) {
  if (rx < 0 || rx >= orcsdr::radio::max_receivers()) rx = 0;
  const orcsdr::radio::Gain g = orcsdr::radio::gain(rx);
  const orcsdr::radio::Info inf = orcsdr::radio::info(rx);
  const uint32_t min_hz = (inf.caps & ESP_RTL_SDR_CAP_HF_UPCONVERTER) ? ESP_RTL_SDR_FREQ_MIN_HZ : 24000000u;
  o.fmt("{\"index\":%d,\"tuner\":\"\",\"tune_min_hz\":%u,\"tune_max_hz\":%u,\"gain_manual\":%s,\"gain_auto\":%s,\"gain_mode\":\"%s\",\"gain_tenth_db\":%d,"
        "\"rtl_agc_available\":%s,\"rtl_agc\":%s,\"bias_available\":%s,\"bias\":%s,\"clock\":%s,\"caps\":%u,\"gains_tenth_db\":[",
        rx, unsigned(min_hz), unsigned(ESP_RTL_SDR_FREQ_MAX_HZ), g.available ? "true" : "false", g.auto_available ? "true" : "false",
        g.auto_mode ? "auto" : "manual", g.tenth_db, g.rtl_agc_available ? "true" : "false", g.rtl_agc ? "true" : "false",
        g.bias_available ? "true" : "false", g.bias ? "true" : "false", clock_synced() ? "true" : "false", unsigned(inf.caps));
  for (int i = 0; i < g.step_count; ++i) o.fmt("%s%d", i ? "," : "", g.steps[i]);
  o.raw("]}");
}

void time_now(Out& o) {
  o.fmt("{\"epoch\":%lld,\"synced\":%s}", static_cast<long long>(std::time(nullptr)), clock_synced() ? "true" : "false");
}

// 532-byte row, little-endian: seq, sweep, start_hz, stop_hz (u32 each), running (u8), 3 reserved, then 512 columns.
// Our spectrum is 256 relative-intensity bins (not calibrated dBFS); each is shown twice to fill the 512 columns, and 0 ("no data")
// is raised to 1 while the row is live. The scale therefore differs from the page's -80..-10 dBFS mapping: the shape is right, the level is not.
constexpr size_t kRowBytes = 532;
size_t spectrum(char* out, size_t cap, int rx) {
  if (cap < kRowBytes) return 0;
  if (rx < 0 || rx >= orcsdr::radio::max_receivers()) rx = 0;
  static uint8_t last[orcsdr::radio::kMaxReceivers][256];
  static bool have[orcsdr::radio::kMaxReceivers];
  static uint32_t seq[orcsdr::radio::kMaxReceivers];
  // peek, not take: the on-device Home screen consumes the "new data" flag, which would starve this reader.
  uint8_t fresh[256];
  uint32_t fft_seq = 0;
  if (orcsdr::radio::peek_bins(rx, fresh, &fft_seq)) {
    std::memcpy(last[rx], fresh, sizeof(fresh));
    have[rx] = true;
    seq[rx] = fft_seq;
  }
  orcsdr::radio::ReceiverInfo ri;
  const bool live = orcsdr::radio::receiver_info(rx, &ri) && ri.state == 2 && have[rx];
  const uint32_t center = live ? ri.frequency_hz : 0, half = live ? ri.sample_rate / 2 : 0;
  const uint32_t start = center > half ? center - half : 0, stop = center + half, sweep = 0;
  std::memset(out, 0, kRowBytes);
  std::memcpy(out + 0, &seq[rx], 4);
  std::memcpy(out + 4, &sweep, 4);
  std::memcpy(out + 8, &start, 4);
  std::memcpy(out + 12, &stop, 4);
  out[16] = live ? 1 : 0;
  if (live) {
    for (int j = 0; j < 512; ++j) out[20 + j] = static_cast<char>(std::max<uint8_t>(1, last[rx][j / 2]));
  }
  return kRowBytes;
}
}  // namespace

bool get(const char* name, int rx, char* out, size_t cap, size_t* len, Kind* kind) {
  *kind = Kind::json;
  *len = 0;
  if (std::strcmp(name, "spectrum") == 0) {
    *kind = Kind::binary;
    *len = spectrum(out, cap, rx);
    return true;
  }
  Out o{out, cap};
  if (cap) out[0] = '\0';
  if (std::strcmp(name, "receivers") == 0) receivers(o);
  else if (std::strcmp(name, "fm") == 0) fm(o);
  else if (std::strcmp(name, "report") == 0) report(o);
  else if (std::strcmp(name, "tuner") == 0) tuner(o, rx);
  else if (std::strcmp(name, "time") == 0) time_now(o);
  else return false;
  *len = o.overflow ? 0 : o.len;
  return true;
}
}  // namespace web_compat
