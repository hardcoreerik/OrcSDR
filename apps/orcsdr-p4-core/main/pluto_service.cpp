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
#include <new>

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
  SemaphoreHandle_t conv_lock = nullptr;  // guards `conv` and `epoch`: the conversion task and a rate change both touch them
  uint32_t epoch = 1;                     // bumped (under conv_lock) whenever the output rate or decimation changes
  bool buffer_open = false;
  uint32_t drops = 0;                     // blocks the reader had to discard because every raw buffer was in use
};

// USB reads must never wait for conversion, audio or the FFT: the Pluto drops samples when the host is late. The reader (the pluto
// task) only reads and queues; a second task converts and feeds the sinks. kBuffers raw blocks circulate between them.
// Every block queued to the converter carries the epoch, output rate and centre frequency it was read under: a block read before a rate
// change is discarded, never converted with the new decimation, and consumers get the block's own rate, not a periodically published one.
// One Pipe (with its Session) is allocated per attach and owned by the converter task until that task ends. A task that does not stop
// in time is quarantined: its Pipe is leaked, never reused, so a later session can not touch its state.
constexpr int kBuffers = 4;
constexpr size_t kRawBytes = kSamplesPerRead * 4;
struct Item {
  uint32_t idx, bytes, epoch, rate, hz;
};
struct Pipe {
  Session s;
  int16_t* raw[kBuffers + 1] = {};  // the last one is scratch for reads that find no free block
  uint8_t* cu8 = nullptr;
  QueueHandle_t filled = nullptr, freeq = nullptr;
  std::atomic<bool> run{false}, exited{true};
  std::atomic<uint32_t> blocks{0}, convert_us{0}, feed_us{0};  // totals since the last log line, for the CPU cost split
  std::atomic<uint32_t> delivered{0};                          // samples converted and handed to the sinks since the last log line
  std::atomic<uint32_t> stale{0};                              // blocks discarded because their rate/decimation was superseded
};

void convert_task(void* arg) {
  Pipe& p = *static_cast<Pipe*>(arg);
  Item it;
  while (p.run.load()) {
    if (xQueueReceive(p.filled, &it, pdMS_TO_TICKS(100)) != pdTRUE) continue;
    const int idx = static_cast<int>(it.idx);
    xSemaphoreTake(p.s.conv_lock, portMAX_DELAY);
    if (it.epoch != p.s.epoch) {  // read under a previous rate: drop it
      xSemaphoreGive(p.s.conv_lock);
      p.stale.fetch_add(1);
      xQueueSend(p.freeq, &idx, 0);
      continue;
    }
    const int64_t t0 = esp_timer_get_time();
    const size_t pairs = pluto_iq_convert(&p.s.conv, p.raw[idx], it.bytes / 4, p.cu8);
    const int64_t t1 = esp_timer_get_time();
    xSemaphoreGive(p.s.conv_lock);
    if (pairs && p.run.load()) radio::ext::feed(kRx, p.cu8, pairs * 2, it.rate, it.hz);
    p.delivered.fetch_add(static_cast<uint32_t>(pairs));
    p.convert_us.fetch_add(static_cast<uint32_t>(t1 - t0));
    p.feed_us.fetch_add(static_cast<uint32_t>(esp_timer_get_time() - t1));
    p.blocks.fetch_add(1);
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
    // The AD9361 rounds to a few Hz (2.4 MS/s reads back as 2,399,999). Within 2 Hz the nominal rate stays, as a dongle's does;
    // otherwise consumers that divide the rate down (the browser channel) would see 299,999 S/s and refuse it.
    if (got > 0 && std::llabs(got - static_cast<long long>(ad)) > 2) {
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
  ++s.epoch;  // blocks already queued belong to the old rate; the converter will discard them
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
    // The console's frequency type is 32 bits, so tuning above 4.29 GHz is not reachable until it becomes 64 bits (the device goes to 6 GHz).
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
  pluto_io_t io;
  if (pluto_usb_open(&io) != 0) {
    ESP_LOGE(TAG, "libiio USB pipe did not open");
    return;
  }
  Pipe* pp = new (std::nothrow) Pipe();
  if (!pp) {
    ESP_LOGE(TAG, "session allocation failed");
    return;
  }
  Pipe& p = *pp;
  Session& s = p.s;
  bool task_started = false;
  bool ok = pluto_iiod_open(&s.iiod, &io) == 0;
  if (!ok) ESP_LOGE(TAG, "iiod handshake failed");
  s.conv_lock = xSemaphoreCreateMutex();
  p.filled = xQueueCreate(kBuffers, sizeof(Item));
  p.freeq = xQueueCreate(kBuffers, sizeof(int));
  p.cu8 = static_cast<uint8_t*>(heap_caps_malloc(kSamplesPerRead * 2 + 8, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  bool alloc_ok = s.conv_lock && p.filled && p.freeq && p.cu8;
  for (int b = 0; b <= kBuffers; ++b) {
    p.raw[b] = static_cast<int16_t*>(heap_caps_malloc(kRawBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    alloc_ok = alloc_ok && p.raw[b];
    if (alloc_ok && b < kBuffers) xQueueSend(p.freeq, &b, 0);
  }
  ok = ok && alloc_ok;
  if (ok) {
    p.run = true;
    p.exited = false;
    task_started = xTaskCreatePinnedToCore(convert_task, "pluto_cv", 6144, &p, 5, nullptr, 0) == pdPASS;
    if (!task_started) {
      ESP_LOGE(TAG, "conversion task could not be created");
      p.run = false;
      p.exited = true;
      ok = false;
    }
  }
  uint32_t wanted = 960000;
  if (ok) {
    s.freq = read_range(&s.iiod, "altvoltage0", true, "frequency_available");
    s.rate = read_range(&s.iiod, "voltage0", false, "sampling_frequency_available");
    s.gain_db = read_range(&s.iiod, "voltage0", false, "hardwaregain_available");
    ESP_LOGI(TAG, "ranges: tune %lld..%lld Hz, rate %lld..%lld S/s, gain %lld..%lld dB", s.freq.lo, s.freq.hi, s.rate.lo, s.rate.hi,
             s.gain_db.lo, s.gain_db.hi);
    read_gain(s);
    const radio::ext::Requests first = radio::ext::poll(kRx);  // takes any tune the console already asked for
    if (first.rate_target) wanted = first.rate_target;
    if (first.tune_hz) start_hz = first.tune_hz;
    s.lo_hz = start_hz;
    if (!write_num(&s.iiod, "altvoltage0", true, "frequency", start_hz))
      ESP_LOGW(TAG, "start frequency %u refused", unsigned(start_hz));
    radio::ext::new_stream(kRx);
    ok = apply_rate(s, wanted);
  }
  int64_t last_pub = 0, window_start = esp_timer_get_time(), rate_changed_at = 0;
  uint64_t window_received = 0;
  uint32_t delivered_sps = 0, received_sps = 0;
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
      const Item it{static_cast<uint32_t>(idx), static_cast<uint32_t>(n), s.epoch, s.out_rate, s.lo_hz};  // epoch is only written by this thread
      xQueueSend(p.filled, &it, 0);
    } else {
      ++s.drops;
    }
    window_received += static_cast<uint64_t>(n) / 4 / s.decim;
    if (now - window_start >= 750000) {
      const uint64_t us = static_cast<uint64_t>(now - window_start);
      received_sps = static_cast<uint32_t>(window_received * 1000000ULL / us);
      delivered_sps = static_cast<uint32_t>(static_cast<uint64_t>(p.delivered.exchange(0)) * 1000000ULL / us);
      const uint32_t nb = std::max<uint32_t>(p.blocks.exchange(0), 1);
      // received = what came off the USB pipe; delivered = what reached the sinks; source_drops = blocks lost for want of a free buffer;
      // stale = blocks discarded by a rate-change barrier. Consumer-side drops are per sink (radio::sink_stats), audio underruns are audio's.
      ESP_LOGI(TAG, "IQ hz=%u rate=%u received_sps=%u delivered_sps=%u source_drops=%u stale=%u convert_us/block=%u feed_us/block=%u",
               unsigned(s.lo_hz), unsigned(s.out_rate), unsigned(received_sps), unsigned(delivered_sps), unsigned(s.drops),
               unsigned(p.stale.load()), unsigned(p.convert_us.exchange(0) / nb), unsigned(p.feed_us.exchange(0) / nb));
      window_start = now;
      window_received = 0;
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
      publish(s, 2, delivered_sps);
    }
  }
  close_buffer(s);
  p.run = false;
  if (task_started) {
    for (int i = 0; i < 200 && !p.exited.load(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (!p.exited.load()) {
      // Quarantine: the task may still be inside feed(). Its Pipe (state, queues, buffers) is leaked and never reused; the next
      // session allocates a fresh one and the old task, once it returns, sees run == false and ends without touching the sinks.
      ESP_LOGE(TAG, "conversion task did not stop in 2 s: its session is quarantined (leaked)");
      return;
    }
  }
  for (int b = 0; b <= kBuffers; ++b) heap_caps_free(p.raw[b]);
  heap_caps_free(p.cu8);
  if (p.filled) vQueueDelete(p.filled);
  if (p.freeq) vQueueDelete(p.freeq);
  if (s.conv_lock) vSemaphoreDelete(s.conv_lock);
  delete pp;
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
