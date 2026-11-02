#include "web_audio.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rtl_sdr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orcsdr_dsp/demod.hpp"
#include "radio_service.hpp"
#include "sdkconfig.h"
#include "web_control.hpp"
#include "web_iq.hpp"

#define ORCSDR_WEB_AUDIO (CONFIG_HTTPD_WS_SUPPORT && CONFIG_LWIP_MAX_SOCKETS >= 16)

namespace web_audio {
#if !ORCSDR_WEB_AUDIO
Result start(int, uint32_t, uint32_t, int, const char*, uint32_t, char* out, size_t cap) {
  std::snprintf(out, cap, "{\"ok\":false,\"error\":\"browser_audio_not_built_into_this_firmware\"}");
  return Result::not_available;
}
void stop() {}
bool set_volume(int) { return false; }
Status status() { return Status{}; }
bool available() { return false; }
#else
namespace {
constexpr char TAG[] = "WEB_AUDIO";
namespace radio = orcsdr::radio;
namespace dsp = orcsdr::dsp;
constexpr uint32_t kMagic = 0x3141464f;  // "OFA1" little-endian, what the page checks
constexpr size_t kHeader = 24, kFrames = 960;  // 20 ms of audio per packet (the page refuses more than 960 frames)
constexpr uint32_t kRateStep = 240000;         // the demodulator takes any multiple of 240 kS/s from 960 kS/s up to 2.4 MS/s

struct Session {
  dsp::Demod* demod = nullptr;
  int rx = 0;
  uint32_t station_hz = 0, in_rate = 0, id = 0, seq = 0, first = 0;
  size_t fill = 0;
  int64_t start_us = 0;  // blocks that were queued before the retune arrive for a moment after it
  std::atomic<bool> ended{false};
  float tmp[2048];
  uint8_t frame[kHeader + kFrames * 2];
};

Session* g_sess = nullptr;
radio::SinkHandle g_sink = -1;
std::atomic<int> g_volume{30};
std::atomic<float> g_rf_dbfs{-120.f};
std::atomic<uint32_t> g_dsp_us{0}, g_block_us{0};

void put32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
  p[2] = uint8_t(v >> 16);
  p[3] = uint8_t(v >> 24);
}
void put16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}

void send_frame(Session& s, size_t frames, bool playing) {
  uint8_t* f = s.frame;
  put32(f, kMagic);
  put32(f + 4, s.id);
  put32(f + 8, s.seq++);
  put32(f + 12, s.first);
  put32(f + 16, 48000);
  put16(f + 20, static_cast<uint16_t>(frames));
  put16(f + 22, playing ? 1 : 0);
  web_iq::slot_send(f, kHeader + frames * 2);  // no browser connected yet: the audio is simply not heard
  s.first += static_cast<uint32_t>(frames);
}

void on_block(const radio::IqBlock& b, void* ctx) {
  Session& s = *static_cast<Session*>(ctx);
  if (s.ended.load()) return;
  const int64_t t0 = esp_timer_get_time();
  if (b.sample_rate != s.in_rate || b.center_hz != s.station_hz) {
    if (t0 - s.start_us < 1500000) return;  // still draining blocks from before the retune
    s.fill = 0;
    send_frame(s, 0, false);  // someone else retuned or changed the rate: this is no longer the station the page was told about
    s.ended = true;
    ESP_LOGW(TAG, "WEB_AUDIO_ENDED receiver_changed rate=%u center=%u", static_cast<unsigned>(b.sample_rate), static_cast<unsigned>(b.center_hz));
    return;
  }
  size_t offset = 0;
  while (offset + 1 < b.bytes) {
    const size_t part = std::min<size_t>(b.bytes - offset, 8192);
    const size_t n = s.demod->process(b.data + offset, part & ~static_cast<size_t>(1), s.tmp, 2048);
    offset += part;
    for (size_t i = 0; i < n; ++i) {
      const float v = std::clamp(s.tmp[i] * 0.9f, -1.f, 1.f);  // the page applies its own volume; this is a fixed, safe level
      const int16_t q = static_cast<int16_t>(v * 32767.f);
      uint8_t* o = s.frame + kHeader + 2 * s.fill;
      o[0] = uint8_t(q);
      o[1] = uint8_t(static_cast<uint16_t>(q) >> 8);
      if (++s.fill == kFrames) {
        send_frame(s, kFrames, true);
        s.fill = 0;
      }
    }
  }
  g_rf_dbfs.store(s.demod->channel_dbfs());
  g_dsp_us.store((g_dsp_us.load() * 15 + static_cast<uint32_t>(esp_timer_get_time() - t0)) / 16);
  g_block_us.store(static_cast<uint32_t>(static_cast<uint64_t>(b.bytes / 2) * 1000000ull / b.sample_rate));
}

void end_session() {
  if (g_sess) radio::set_sample_rate_pin(g_sess->rx, 0);  // the receiver goes back to the console's own rate policy; where it is tuned is left alone
  if (g_sink >= 0) {
    if (!radio::remove_sink(g_sink)) {
      ESP_LOGW(TAG, "WEB_AUDIO_SINK_QUARANTINED the receiver service could not stop the sink cleanly; the session buffers are kept");
      g_sink = -1;
      g_sess = nullptr;
      web_iq::slot_close();
      return;  // never free what a quarantined sink may still touch
    }
    g_sink = -1;
  }
  if (g_sess) {
    if (!g_sess->ended.load()) {
      g_sess->fill = 0;
      send_frame(*g_sess, 0, false);  // tell the page the audio stopped, then close the socket
    }
    if (g_sess->demod) {
      g_sess->demod->~Demod();
      heap_caps_free(g_sess->demod);
    }
    heap_caps_free(g_sess);
    g_sess = nullptr;
  }
  web_iq::slot_close();
}
}  // namespace

bool available() { return true; }

// Runs on the HTTP server's task (the same one that serves status), so the session pointer is never freed under a reader.
Result start(int rx, uint32_t station_hz, uint32_t request_sps, int volume, const char* mode_name, uint32_t peer, char* out, size_t cap) {
  auto fail = [&](Result r, const char* msg) {
    std::snprintf(out, cap, "{\"ok\":false,\"error\":\"%s\"}", msg);
    return r;
  };
  dsp::Mode mode = dsp::Mode::wfm;
  if (mode_name && *mode_name) {
    if (!std::strcmp(mode_name, "wfm")) mode = dsp::Mode::wfm;
    else if (!std::strcmp(mode_name, "nfm")) mode = dsp::Mode::nfm;
    else if (!std::strcmp(mode_name, "am")) mode = dsp::Mode::am;
    else return fail(Result::bad_request, "bad_mode");
  }
  radio::ReceiverInfo ri;
  for (int i = 0; i < 100 && (!radio::receiver_info(rx, &ri) || ri.state != 2); ++i) vTaskDelay(pdMS_TO_TICKS(50));  // a receiver that is restarting for a rate change is given a few seconds
  if (!radio::receiver_info(rx, &ri) || ri.state != 2) return fail(Result::not_available, "receiver_not_streaming");
  if (station_hz < web_control::tune_min_hz(rx) || station_hz > ESP_RTL_SDR_FREQ_MAX_HZ) return fail(Result::bad_request, "bad_hz");

  // The receiver's rate: as much as was asked for (0 = the most), in whole 240 kS/s steps the demodulator takes, and one the driver accepts.
  uint32_t k = std::min<uint32_t>(request_sps ? request_sps : 2400000, 2400000) / kRateStep;
  if (k < 4) k = 4;
  while (k > 4 && !esp_rtl_sdr_is_rate_supported(k * kRateStep)) --k;
  const uint32_t rate = k * kRateStep;
  if (!esp_rtl_sdr_is_rate_supported(rate)) return fail(Result::not_available, "no_rate_shared_by_driver_and_demodulator");

  web_iq::stop_channel();  // one listener slot: this session replaces a channel stream
  end_session();           // ...and an earlier audio session
  if (!web_iq::slot_open(web_iq::Slot::audio, peer)) return fail(Result::not_available, "ws_server_start_failed");

  radio::set_sample_rate_pin(rx, rate);  // pinned: the Home screen keeps asking for its own rate and would take the receiver back
  radio::tune_to(rx, station_hz);
  // A rate change restarts the USB stream: wait until the receiver shows both the new rate and the station before the sink starts matching blocks.
  const int64_t deadline = esp_timer_get_time() + 8000000;
  radio::ReceiverInfo now;
  bool there = false;
  while (esp_timer_get_time() < deadline) {
    if (radio::receiver_info(rx, &now) && now.state == 2 && now.frequency_hz == station_hz && now.sample_rate == rate) {
      there = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (!there) {  // never start a session that cannot match its blocks: it would end itself a moment later
    radio::set_sample_rate_pin(rx, 0);
    web_iq::slot_close();
    ESP_LOGW(TAG, "WEB_AUDIO_REFUSED the receiver did not reach %u S/s at %u Hz", static_cast<unsigned>(rate), static_cast<unsigned>(station_hz));
    return fail(Result::not_available, "receiver_did_not_take_the_station_and_rate");
  }

  Session* s = static_cast<Session*>(heap_caps_calloc(1, sizeof(Session), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  void* dm = heap_caps_malloc(sizeof(dsp::Demod), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  // the demodulator's state lives in PSRAM, as for the speaker
  if (!s || !dm) {
    heap_caps_free(s);
    heap_caps_free(dm);
    radio::set_sample_rate_pin(rx, 0);
    web_iq::slot_close();
    return fail(Result::not_available, "no_memory");
  }
  new (&s->ended) std::atomic<bool>(false);
  s->demod = new (dm) dsp::Demod();
  s->demod->set_mode(mode);
  s->demod->set_input_rate(rate);
  s->rx = rx;
  s->station_hz = station_hz;
  s->in_rate = rate;
  s->id = esp_random();

  radio::SinkSpec spec;
  spec.name = "webaudio";
  spec.fn = on_block;
  spec.context = s;
  spec.kind = radio::SinkKind::queued;
  spec.receiver = static_cast<uint8_t>(rx);
  spec.queue_bytes = 256 * 1024;
  spec.task_core = 0;
  spec.task_priority = 5;  // above the screen task (4), as for the channel stream
  spec.task_stack = 6144;
  s->start_us = esp_timer_get_time();
  g_sess = s;
  g_sink = radio::add_sink(spec);
  if (g_sink < 0) {
    g_sess = nullptr;
    s->demod->~Demod();
    heap_caps_free(s->demod);
    heap_caps_free(s);
    radio::set_sample_rate_pin(rx, 0);
    web_iq::slot_close();
    return fail(Result::not_available, "no_sink_slot");
  }
  g_volume = std::clamp(volume, 0, 80);
  ESP_LOGI(TAG, "WEB_AUDIO_STARTED rx=%d hz=%u in=%u mode=%s", rx, static_cast<unsigned>(station_hz), static_cast<unsigned>(rate), mode_name && *mode_name ? mode_name : "wfm");
  std::snprintf(out, cap, "{\"ok\":true,\"index\":%d,\"hz\":%u,\"input_sps\":%u,\"audio_sps\":48000,\"mode\":\"%s\",\"volume\":%d,\"ws_port\":81}", rx, static_cast<unsigned>(station_hz),
                static_cast<unsigned>(rate), mode_name && *mode_name ? mode_name : "wfm", g_volume.load());
  return Result::ok;
}

void stop() { end_session(); }

bool set_volume(int volume) {
  if (!g_sess) return false;
  g_volume = std::clamp(volume, 0, 80);
  return true;
}

Status status() {
  Status st;
  const Session* s = g_sess;
  if (!s) return st;
  st.playing = !s->ended.load();
  st.receiver = s->rx;
  st.station_hz = s->station_hz;
  st.input_sps = s->in_rate;
  st.volume = g_volume.load();
  st.client = web_iq::slot_connected();
  st.rf_dbfs = g_rf_dbfs.load();
  st.dsp_us = g_dsp_us.load();
  st.block_us = g_block_us.load();
  return st;
}
#endif
}  // namespace web_audio
