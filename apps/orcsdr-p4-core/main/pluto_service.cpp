// PlutoSDR (AD9361) as an OrcSDR receiver source, over the board's USB host port. It claims receiver 0 through radio::ext while a
// Pluto is attached and no RTL-SDR is streaming there, converts the 12-bit IQ to the CU8 every consumer already takes, and
// feeds the receiver's sinks, so audio, RF Lab, the decoders and the browser stream need no change. Receive only: nothing here
// ever writes a transmit attribute. Ranges (tune span, rate floor, gain span) are read from the device, not assumed.
#include "pluto_service.hpp"

#include "sdkconfig.h"

#if CONFIG_ORCSDR_PLUTO_SOURCE

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pluto_iiod.h"
#include "pluto_iq.h"
#include "pluto_usb.h"
#include "radio_service.hpp"

namespace orcsdr::pluto {
namespace {
const char* TAG = "PLUTO";
constexpr int kRx = 0;                       // the receiver this source takes (the RTL-SDR keeps it whenever it is streaming)
constexpr uint32_t kSamplesPerRead = 16384;  // complex samples per iiod buffer read (4 bytes each)
// The USB link carried 11.5 MB/s (2.88 MS/s) in the probe; headroom above that is unmeasured, so the AD9361 rate is capped at 3 MS/s.
constexpr uint32_t kMaxAdRate = 3000000;

struct Range {
  long long lo = 0, step = 1, hi = 0;
  bool ok = false;
};

Range parse_range(const char* s) {  // "[lo step hi]"
  Range r;
  r.ok = std::sscanf(s, "[%lld %lld %lld]", &r.lo, &r.step, &r.hi) == 3;
  return r;
}

Range read_range(pluto_iiod_t* c, const char* ch, bool out, const char* attr) {
  char v[64] = {};
  if (pluto_iiod_read_attr(c, c->phy, ch, out, attr, v, sizeof(v)) < 0) return Range{};
  return parse_range(v);
}

bool write_attr(pluto_iiod_t* c, const char* ch, bool out, const char* attr, const char* value) {
  return pluto_iiod_write_attr(c, c->phy, ch, out, attr, value) >= 0;
}

bool write_num(pluto_iiod_t* c, const char* ch, bool out, const char* attr, long long v) {
  char b[24];
  std::snprintf(b, sizeof(b), "%lld", v);
  return write_attr(c, ch, out, attr, b);
}

struct Session {
  pluto_iiod_t iiod;
  Range freq, rate, gain_db;
  uint32_t out_rate = 0, ad_rate = 0, decim = 1, lo_hz = 0, req_rate = 0;  // req_rate: what the console asked for (the device may round it)
  bool manual = false;
  int gain_tenth = 0;
  pluto_iq_conv_t conv;
  SemaphoreHandle_t conv_lock = nullptr;  // the conversion task and a rate change both touch `conv`
  bool buffer_open = false;
  uint32_t drops = 0;
};

// USB reads must never wait for conversion, audio or the FFT: the Pluto drops samples when the host is late. The reader (the pluto
// task) only reads and queues; a second task converts and feeds the sinks. kBuffers raw blocks circulate between them.
constexpr int kBuffers = 4;
constexpr size_t kRawBytes = kSamplesPerRead * 4;
struct Pipe {
  Session* s = nullptr;
  int16_t* raw[kBuffers + 1] = {};  // the last one is scratch for reads that find no free block
  uint8_t* cu8 = nullptr;
  QueueHandle_t filled = nullptr, freeq = nullptr;
  std::atomic<bool> run{false}, exited{true};
};
Pipe g_pipe;

void convert_task(void*) {
  Pipe& p = g_pipe;
  uint32_t v = 0;
  while (p.run.load()) {
    if (xQueueReceive(p.filled, &v, pdMS_TO_TICKS(100)) != pdTRUE) continue;
    const int idx = static_cast<int>(v & 0xF);
    const size_t bytes = v >> 4;
    xSemaphoreTake(p.s->conv_lock, portMAX_DELAY);
    const size_t pairs = pluto_iq_convert(&p.s->conv, p.raw[idx], bytes / 4, p.cu8);
    xSemaphoreGive(p.s->conv_lock);
    if (pairs) radio::ext::feed(kRx, p.cu8, pairs * 2);
    xQueueSend(p.freeq, &idx, 0);
  }
  p.exited = true;
  vTaskDelete(nullptr);
}

// Chooses the AD9361 rate and the integer decimation that give `wanted` output samples per second.
bool plan_rate(const Session& s, uint32_t wanted, uint32_t* out, uint32_t* ad, uint32_t* decim) {
  const uint32_t min_ad = s.rate.ok ? static_cast<uint32_t>(s.rate.lo) : 2083334;
  uint32_t max_ad = s.rate.ok ? static_cast<uint32_t>(std::min<long long>(s.rate.hi, kMaxAdRate)) : kMaxAdRate;
  if (wanted == 0) wanted = 960000;
  uint32_t d = pluto_iq_pick_decimation(wanted, min_ad, max_ad);
  if (d == 0) {  // above the ceiling: run at the highest rate the link is allowed to carry, undecimated
    *out = *ad = max_ad;
    *decim = 1;
    return true;
  }
  *out = wanted;
  *ad = wanted * d;
  *decim = d;
  return true;
}

void close_buffer(Session& s) {
  if (s.buffer_open) pluto_iiod_buffer_close(&s.iiod);
  s.buffer_open = false;
}

bool apply_rate(Session& s, uint32_t wanted) {
  close_buffer(s);
  uint32_t out, ad, d;
  plan_rate(s, wanted, &out, &ad, &d);
  // The analog filter follows the output rate (never below the device's own minimum bandwidth).
  long long bw = std::max<long long>(out, 200000);
  if (!write_num(&s.iiod, "voltage0", false, "sampling_frequency", ad)) return false;
  (void)write_num(&s.iiod, "voltage0", false, "rf_bandwidth", bw);
  char v[32] = {};
  if (pluto_iiod_read_attr(&s.iiod, s.iiod.phy, "voltage0", false, "sampling_frequency", v, sizeof(v)) > 0) {
    const long long got = std::atoll(v);
    if (got > 0 && got != static_cast<long long>(ad)) {
      ESP_LOGW(TAG, "asked %u S/s, device runs %lld", unsigned(ad), got);
      ad = static_cast<uint32_t>(got);
      out = ad / d;
    }
  }
  s.req_rate = wanted;
  s.out_rate = out;
  s.ad_rate = ad;
  s.decim = d;
  xSemaphoreTake(s.conv_lock, portMAX_DELAY);
  pluto_iq_conv_init(&s.conv, d);
  xSemaphoreGive(s.conv_lock);
  if (pluto_iiod_buffer_open(&s.iiod, kSamplesPerRead, 0x3) < 0) return false;
  s.buffer_open = true;
  ESP_LOGI(TAG, "RATE out=%u ad9361=%u decim=%u", unsigned(out), unsigned(ad), unsigned(d));
  return true;
}

void read_gain(Session& s) {
  char v[40] = {};
  if (pluto_iiod_read_attr(&s.iiod, s.iiod.phy, "voltage0", false, "hardwaregain", v, sizeof(v)) > 0)
    s.gain_tenth = static_cast<int>(std::atof(v) * 10.0 + (std::atof(v) < 0 ? -0.5 : 0.5));
  char m[40] = {};
  if (pluto_iiod_read_attr(&s.iiod, s.iiod.phy, "voltage0", false, "gain_control_mode", m, sizeof(m)) > 0)
    s.manual = std::strncmp(m, "manual", 6) == 0;
}

radio::Gain make_gain(const Session& s) {
  radio::Gain g;
  g.available = s.gain_db.ok;
  g.auto_available = true;  // the AD9361's own AGC (slow attack), which adapts to antenna and band
  g.auto_mode = !s.manual;
  g.tenth_db = s.gain_tenth;
  if (s.gain_db.ok) {
    // At most 32 ladder entries spread over the device's own span.
    const long long span = s.gain_db.hi - std::max<long long>(s.gain_db.lo, 0);
    const long long stride = std::max<long long>(s.gain_db.step, (span + 30) / 31);
    for (long long v = std::max<long long>(s.gain_db.lo, 0); v <= s.gain_db.hi && g.step_count < 32; v += stride)
      g.steps[g.step_count++] = static_cast<int>(v * 10);
  }
  return g;
}

void publish(const Session& s, int state, uint32_t sps) {
  radio::ext::Status st;
  st.state = state;
  st.applied_hz = s.lo_hz;
  st.rate = s.out_rate;
  st.sps = sps;
  st.drops = s.drops;
  if (s.freq.ok) {
    st.tune_min_hz = static_cast<uint32_t>(s.freq.lo);
    st.tune_max_hz = static_cast<uint32_t>(std::min<long long>(s.freq.hi, 0xFFFFFFFFLL));
  }
  std::snprintf(st.device, sizeof(st.device), "PlutoSDR AD9361");
  std::snprintf(st.usb_path, sizeof(st.usb_path), "pluto-usb");
  radio::ext::publish(kRx, st, make_gain(s));
}

void handle_requests(Session& s, int64_t now, int64_t* rate_changed_at, uint32_t* wanted_rate) {
  const radio::ext::Requests q = radio::ext::poll(kRx);
  if (q.tune_hz) {
    const bool ok = write_num(&s.iiod, "altvoltage0", true, "frequency", q.tune_hz);
    radio::ext::tune_result(kRx, q.tune_hz, ok ? 0 : -1);
    if (ok) s.lo_hz = q.tune_hz;
    ESP_LOGI(TAG, "TUNE %u -> %s", unsigned(q.tune_hz), ok ? "ok" : "refused by the device");
  }
  if (q.gain_kind == 1) {
    if (write_attr(&s.iiod, "voltage0", false, "gain_control_mode", "slow_attack")) s.manual = false;
    read_gain(s);
  } else if (q.gain_kind == 2) {
    long long db = q.gain_value / 10;
    if (s.gain_db.ok) db = std::clamp<long long>(db, s.gain_db.lo, s.gain_db.hi);
    if (write_attr(&s.iiod, "voltage0", false, "gain_control_mode", "manual") &&
        write_num(&s.iiod, "voltage0", false, "hardwaregain", db))
      s.manual = true;
    read_gain(s);
  }
  // A rate change is a buffer close and reopen. Wait until the request has been steady for a moment (screens ask twice in quick
  // succession), the same rule the RTL-SDR loop applies.
  if (q.rate_target && q.rate_target != *wanted_rate) {
    *wanted_rate = q.rate_target;
    *rate_changed_at = now;
  }
  if (*wanted_rate && *wanted_rate != s.req_rate && now - *rate_changed_at > 400000) {
    radio::ext::new_stream(kRx);
    if (!apply_rate(s, *wanted_rate)) ESP_LOGE(TAG, "rate change failed");
  }
}

// One attach: configure, stream until the device goes away or a read fails. Returns when the session is over.
void run_session(uint32_t start_hz) {
  static Session s;
  s = Session{};
  pluto_io_t io;
  if (pluto_usb_open(&io) != 0) {
    ESP_LOGE(TAG, "libiio USB pipe did not open");
    return;
  }
  if (pluto_iiod_open(&s.iiod, &io) != 0) {
    ESP_LOGE(TAG, "iiod handshake failed");
    return;
  }
  Pipe& p = g_pipe;
  p.s = &s;
  s.conv_lock = xSemaphoreCreateMutex();
  p.filled = xQueueCreate(kBuffers, sizeof(uint32_t));
  p.freeq = xQueueCreate(kBuffers, sizeof(int));
  p.cu8 = static_cast<uint8_t*>(heap_caps_malloc(kSamplesPerRead * 2 + 8, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  bool alloc_ok = s.conv_lock && p.filled && p.freeq && p.cu8;
  for (int b = 0; b <= kBuffers; ++b) {
    p.raw[b] = static_cast<int16_t*>(heap_caps_malloc(kRawBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    alloc_ok = alloc_ok && p.raw[b];
    if (alloc_ok && b < kBuffers) xQueueSend(p.freeq, &b, 0);
  }
  p.run = true;
  p.exited = false;
  if (!alloc_ok || xTaskCreatePinnedToCore(convert_task, "pluto_cv", 6144, nullptr, 5, nullptr, 0) != pdPASS) {
    ESP_LOGE(TAG, "IQ buffers or conversion task could not be created");
    p.run = false;
    p.exited = true;
  }
  s.freq = read_range(&s.iiod, "altvoltage0", true, "frequency_available");
  s.rate = read_range(&s.iiod, "voltage0", false, "sampling_frequency_available");
  s.gain_db = read_range(&s.iiod, "voltage0", false, "hardwaregain_available");
  ESP_LOGI(TAG, "ranges: tune %lld..%lld Hz, rate %lld..%lld S/s, gain %lld..%lld dB", s.freq.lo, s.freq.hi, s.rate.lo, s.rate.hi,
           s.gain_db.lo, s.gain_db.hi);
  read_gain(s);
  const radio::ext::Requests first = radio::ext::poll(kRx);  // takes any tune the console already asked for
  uint32_t wanted = first.rate_target ? first.rate_target : 960000;
  if (first.tune_hz) start_hz = first.tune_hz;
  s.lo_hz = start_hz;
  if (!write_num(&s.iiod, "altvoltage0", true, "frequency", start_hz))
    ESP_LOGW(TAG, "start frequency %u refused", unsigned(start_hz));
  radio::ext::new_stream(kRx);
  bool ok = alloc_ok && apply_rate(s, wanted);
  int64_t last_pub = 0, window_start = esp_timer_get_time(), rate_changed_at = 0;
  uint64_t window_samples = 0;
  uint32_t sps = 0;
  uint32_t log_count = 0;
  while (ok && p.run.load() && !pluto_usb_gone()) {
    const int64_t now = esp_timer_get_time();
    handle_requests(s, now, &rate_changed_at, &wanted);
    if (!s.buffer_open) break;  // a failed rate change leaves no buffer: end the session and start over
    int idx = -1;
    const bool have = xQueueReceive(p.freeq, &idx, 0) == pdTRUE;
    // Without a free block the data still has to be read off the pipe (and is lost): the count shows it.
    const int n = pluto_iiod_buffer_read(&s.iiod, have ? p.raw[idx] : p.raw[kBuffers], kRawBytes);
    if (n <= 0) {
      ESP_LOGW(TAG, "buffer read -> %d, ending the session", n);
      if (have) xQueueSend(p.freeq, &idx, 0);
      break;
    }
    if (have) {
      const uint32_t v = (static_cast<uint32_t>(n) << 4) | static_cast<uint32_t>(idx);
      xQueueSend(p.filled, &v, 0);
    } else {
      ++s.drops;
    }
    window_samples += static_cast<uint64_t>(n) / 4 / s.decim;
    if (now - window_start >= 750000) {
      sps = static_cast<uint32_t>(window_samples * 1000000ULL / static_cast<uint64_t>(now - window_start));
      ESP_LOGI(TAG, "IQ hz=%u sps=%u rate=%u dropped_blocks=%u", unsigned(s.lo_hz), unsigned(sps), unsigned(s.out_rate),
               unsigned(s.drops));
      window_start = now;
      window_samples = 0;
      if (++log_count % 4 == 0) {  // a coarse spectrum read: strongest bin against the median shows a real carrier from noise
        uint8_t bins[256];
        uint32_t seq = 0;
        if (radio::peek_bins(kRx, bins, &seq)) {
          int peak = 0;
          uint8_t sorted[256];
          std::memcpy(sorted, bins, sizeof(sorted));
          for (int i = 1; i < 256; ++i)
            if (bins[i] > bins[peak]) peak = i;
          std::nth_element(sorted, sorted + 128, sorted + 256);
          ESP_LOGI(TAG, "SPECTRUM peak_bin=%d (offset %+d kHz) peak=%u median=%u level=%.1f dBFS", peak,
                   static_cast<int>((peak - 128) * static_cast<int>(s.out_rate) / 256 / 1000), unsigned(bins[peak]),
                   unsigned(sorted[128]), static_cast<double>(radio::level_dbfs(kRx)));
        }
      }
    }
    if (now - last_pub > 250000) {
      last_pub = now;
      publish(s, 2, sps);
    }
  }
  close_buffer(s);
  p.run = false;
  for (int i = 0; i < 100 && !p.exited.load(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
  if (!p.exited.load()) {
    ESP_LOGE(TAG, "conversion task did not stop; its buffers are kept, not freed");
    return;
  }
  for (int b = 0; b <= kBuffers; ++b) heap_caps_free(p.raw[b]);
  heap_caps_free(p.cu8);
  if (p.filled) vQueueDelete(p.filled);
  if (p.freeq) vQueueDelete(p.freeq);
  if (s.conv_lock) vSemaphoreDelete(s.conv_lock);
}

void task(void*) {
  while (!radio::ext::host_ready()) vTaskDelay(pdMS_TO_TICKS(500));
  if (pluto_usb_init(false) != 0) {  // the RTL-SDR driver owns the USB host library; this registers one more client
    ESP_LOGE(TAG, "USB client registration failed");
    vTaskDelete(nullptr);
    return;
  }
  ESP_LOGI(TAG, "PlutoSDR source ready (receive only)");
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!pluto_usb_poll()) continue;
    if (!radio::ext::acquire(kRx)) {  // an RTL-SDR is streaming on the receiver: it keeps it
      ESP_LOGI(TAG, "Pluto attached but receiver %d is streaming an RTL-SDR; leaving it", kRx);
      pluto_usb_close();
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(300));  // the device finishes enumerating
    run_session(radio::snapshot().requested_hz);
    radio::ext::release(kRx);
    pluto_usb_close();
    ESP_LOGW(TAG, "PlutoSDR session ended; looking for it again");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
}  // namespace

bool start() { return xTaskCreatePinnedToCore(task, "pluto", 8192, nullptr, 7, nullptr, 1) == pdPASS; }
}  // namespace orcsdr::pluto

#else  // !CONFIG_ORCSDR_PLUTO_SOURCE

namespace orcsdr::pluto {
bool start() { return true; }
}  // namespace orcsdr::pluto

#endif
