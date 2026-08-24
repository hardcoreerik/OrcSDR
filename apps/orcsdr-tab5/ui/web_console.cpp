#include "web_console.hpp"

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <mdns.h>

#include <atomic>
#include <cstdio>
#include <cstring>

extern const uint8_t web_console_html_start[] asm("_binary_web_console_html_start");
extern const uint8_t web_console_html_end[] asm("_binary_web_console_html_end");
extern const uint8_t orc_badge_start[] asm("_binary_orc_badge_104_png_start");
extern const uint8_t orc_badge_end[] asm("_binary_orc_badge_104_png_end");

namespace orcsdr::web_console {
namespace {
constexpr char kLogTag[] = "OrcSDR";

std::atomic<bool> g_enabled{false};
bool g_listening = false;
bool g_mdns_started = false;
httpd_handle_t g_server = nullptr;
Snapshot g_snapshot{};
Command g_pending{};
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
constexpr size_t kAudioRing = 16384;
constexpr size_t kAudioClip = 4800;
constexpr size_t kAudioMin = 1600;
constexpr uint32_t kAudioRate = 16000;
EXT_RAM_BSS_ATTR int16_t g_audio_ring[kAudioRing]{};
EXT_RAM_BSS_ATTR int16_t g_audio_clip[kAudioClip]{};
EXT_RAM_BSS_ATTR uint8_t g_wav_out[44 + kAudioClip * sizeof(int16_t)]{};
EXT_RAM_BSS_ATTR char g_status_json[2048]{};
EXT_RAM_BSS_ATTR char g_status_recent[320]{};
EXT_RAM_BSS_ATTR char g_status_spec[259]{};
uint32_t g_wifi_up_ms = 0;
std::atomic<uint32_t> g_audio_w{0};
std::atomic<uint32_t> g_audio_r{0};
std::atomic<int> g_audio_clients{0};
uint8_t g_spec[kSpectrumBins]{};
uint8_t g_spec_count = 0;
uint32_t g_last_action_ms = 0;

void json_escape(char* out, size_t out_size, const char* in) {
  if (out_size == 0) return;
  size_t o = 0;
  if (in == nullptr) in = "";
  for (size_t i = 0; in[i] != '\0' && o + 2 < out_size; ++i) {
    const char c = in[i];
    if ((c == '"' || c == '\\') && o + 3 < out_size) {
      out[o++] = '\\';
      out[o++] = c;
    } else if (static_cast<unsigned char>(c) >= 32) {
      out[o++] = c;
    }
  }
  out[o] = '\0';
}

CommandKind parse_kind(const char* body) {
  if (strncmp(body, "volume_down", 11) == 0) return CommandKind::volume_down;
  if (strncmp(body, "volume_up", 9) == 0) return CommandKind::volume_up;
  if (strncmp(body, "sound_toggle", 12) == 0) return CommandKind::sound_toggle;
  if (strncmp(body, "span_down", 9) == 0) return CommandKind::span_down;
  if (strncmp(body, "span_up", 7) == 0) return CommandKind::span_up;
  if (strncmp(body, "step_down", 9) == 0) return CommandKind::step_down;
  if (strncmp(body, "step_up", 7) == 0) return CommandKind::step_up;
  if (strncmp(body, "tune=", 5) == 0) return CommandKind::tune;
  if (strncmp(body, "open=", 5) == 0) return CommandKind::open;
  return CommandKind::none;
}

esp_err_t handle_root(httpd_req_t* req) {
  const size_t bytes = static_cast<size_t>(web_console_html_end - web_console_html_start);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, reinterpret_cast<const char*>(web_console_html_start),
                         bytes);
}

esp_err_t handle_badge(httpd_req_t* req) {
  const size_t bytes = static_cast<size_t>(orc_badge_end - orc_badge_start);
  httpd_resp_set_type(req, "image/png");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
  return httpd_resp_send(req, reinterpret_cast<const char*>(orc_badge_start), bytes);
}

esp_err_t handle_status(httpd_req_t* req) {
  Snapshot snap;
  portENTER_CRITICAL(&g_mux);
  snap = g_snapshot;
  portEXIT_CRITICAL(&g_mux);

  char ip[24], mode[24], clock[24], date[28], ps[20], rt[80], pi[12];
  json_escape(ip, sizeof(ip), snap.wifi_ip);
  json_escape(mode, sizeof(mode), snap.mode);
  json_escape(clock, sizeof(clock), snap.clock);
  json_escape(date, sizeof(date), snap.date);
  json_escape(ps, sizeof(ps), snap.program_service);
  json_escape(rt, sizeof(rt), snap.radio_text);
  json_escape(pi, sizeof(pi), snap.pi_code);

  char* recent = g_status_recent;
  recent[0] = '[';
  recent[1] = '\0';
  size_t used = 1;
  for (uint8_t i = 0; i < snap.recent_count && i < kRecentSlots; ++i) {
    char id[16], title[24];
    json_escape(id, sizeof(id), snap.recent_id[i]);
    json_escape(title, sizeof(title), snap.recent_title[i]);
    char item[56];
    const int n = snprintf(item, sizeof(item), "%s{\"id\":\"%s\",\"title\":\"%s\"}",
                           i ? "," : "", id, title);
    if (n < 0 || used + static_cast<size_t>(n) + 2 >= sizeof(g_status_recent)) break;
    memcpy(recent + used, item, static_cast<size_t>(n));
    used += static_cast<size_t>(n);
  }
  memcpy(recent + used, "]", 2);

  char* spec = g_status_spec;
  spec[0] = '[';
  spec[1] = '\0';
  used = 1;
  for (uint8_t i = 0; i < snap.spectrum_count && i < kSpectrumBins; ++i) {
    char item[8];
    const int n = snprintf(item, sizeof(item), "%s%u", i ? "," : "", snap.spectrum[i]);
    if (n < 0 || used + static_cast<size_t>(n) + 2 >= sizeof(g_status_spec)) break;
    memcpy(spec + used, item, static_cast<size_t>(n));
    used += static_cast<size_t>(n);
  }
  memcpy(spec + used, "]", 2);

  snprintf(g_status_json, sizeof(g_status_json),
           "{\"wifi_ip\":\"%s\",\"wifi_connected\":%s,\"usb_connected\":%s,"
           "\"rtl_ready\":%s,\"receiving\":%s,\"sound_enabled\":%s,\"stereo\":%s,"
           "\"rds_carrier\":%s,\"rds_locked\":%s,\"program_service\":\"%s\","
           "\"radio_text\":\"%s\",\"pi_code\":\"%s\",\"recording\":%s,\"mode\":\"%s\","
           "\"frequency_hz\":%lu,\"requested_frequency_hz\":%lu,\"span_hz\":%lu,"
           "\"step_hz\":%lu,\"filter_bandwidth_hz\":%lu,\"effective_sps\":%lu,"
           "\"battery_percent\":%ld,\"signal_dbfs\":%.1f,\"left_dbfs\":%.1f,"
           "\"right_dbfs\":%.1f,\"volume\":%u,\"clock\":\"%s\",\"date\":\"%s\","
           "\"recent\":%s,\"spectrum\":%s,\"web\":{\"enabled\":%s}}",
           ip, snap.wifi_connected ? "true" : "false",
           snap.usb_connected ? "true" : "false", snap.rtl_ready ? "true" : "false",
           snap.receiving ? "true" : "false", snap.sound_enabled ? "true" : "false",
           snap.stereo ? "true" : "false", snap.rds_carrier ? "true" : "false",
           snap.rds_locked ? "true" : "false", ps, rt, pi,
           snap.recording ? "true" : "false",
           mode, static_cast<unsigned long>(snap.frequency_hz),
           static_cast<unsigned long>(snap.requested_frequency_hz),
           static_cast<unsigned long>(snap.span_hz),
           static_cast<unsigned long>(snap.step_hz),
           static_cast<unsigned long>(snap.filter_bandwidth_hz),
           static_cast<unsigned long>(snap.effective_sps),
           static_cast<long>(snap.battery_percent),
           static_cast<double>(snap.signal_dbfs),
           static_cast<double>(snap.left_dbfs),
           static_cast<double>(snap.right_dbfs), snap.volume, clock, date, recent,
           spec, snap.enabled ? "true" : "false");

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, g_status_json, HTTPD_RESP_USE_STRLEN);
}

void write_wav(uint8_t* out, const int16_t* pcm, size_t samples) {
  const uint32_t data_bytes = static_cast<uint32_t>(samples * sizeof(int16_t));
  memcpy(out, "RIFF", 4);
  const uint32_t riff = 36 + data_bytes;
  memcpy(out + 4, &riff, 4);
  memcpy(out + 8, "WAVEfmt ", 8);
  const uint32_t fmt = 16;
  memcpy(out + 16, &fmt, 4);
  const uint16_t pcm_fmt = 1, ch = 1, bits = 16, block = 2;
  memcpy(out + 20, &pcm_fmt, 2);
  memcpy(out + 22, &ch, 2);
  memcpy(out + 24, &kAudioRate, 4);
  const uint32_t byterate = kAudioRate * 2;
  memcpy(out + 28, &byterate, 4);
  memcpy(out + 32, &block, 2);
  memcpy(out + 34, &bits, 2);
  memcpy(out + 36, "data", 4);
  memcpy(out + 40, &data_bytes, 4);
  if (samples != 0) memcpy(out + 44, pcm, data_bytes);
}

esp_err_t handle_audio(httpd_req_t* req) {
  size_t got = 0;
  for (int spin = 0; spin < 20 && got < kAudioMin; ++spin) {
    got += copy_audio(g_audio_clip + got, kAudioClip - got);
    if (got >= kAudioMin) break;
    vTaskDelay(pdMS_TO_TICKS(16));
  }
  if (got < kAudioMin) {
    memset(g_audio_clip + got, 0, (kAudioMin - got) * sizeof(int16_t));
    got = kAudioMin;
  }
  write_wav(g_wav_out, g_audio_clip, got);
  httpd_resp_set_type(req, "audio/wav");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Accept-Ranges", "none");
  return httpd_resp_send(req, reinterpret_cast<const char*>(g_wav_out),
                         static_cast<ssize_t>(44 + got * sizeof(int16_t)));
}

esp_err_t handle_spectrum(httpd_req_t* req) {
  uint8_t bins[kSpectrumBins];
  const size_t n = copy_spectrum(bins, kSpectrumBins);
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, reinterpret_cast<const char*>(bins), static_cast<ssize_t>(n));
}

esp_err_t handle_action(httpd_req_t* req) {
  char body[48]{};
  const int got = httpd_req_recv(req, body, sizeof(body) - 1);
  if (got <= 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "bad", 3);
  }
  Command command{};
  command.kind = parse_kind(body);
  if (command.kind == CommandKind::tune) command.value = static_cast<uint32_t>(atoi(body + 5));
  if (command.kind == CommandKind::open) strlcpy(command.id, body + 5, sizeof(command.id));
  if (command.kind == CommandKind::none) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "unknown", 7);
  }
  const uint32_t now = millis();
  if (now - g_last_action_ms < 350) {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "busy", 4);
  }
  g_last_action_ms = now;
  portENTER_CRITICAL(&g_mux);
  g_pending = command;
  portEXIT_CRITICAL(&g_mux);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "ok", 2);
}

void stop_mdns() {
  if (!g_mdns_started) return;
  mdns_free();
  g_mdns_started = false;
}

uint32_t dma_largest() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

void start_mdns() {
  if (g_mdns_started) return;
  const uint32_t largest = dma_largest();
  if (largest < 3072) {
    ESP_LOGI(kLogTag, "RTL_WEB_MDNS skipped dma_largest=%lu", static_cast<unsigned long>(largest));
    return;
  }
  if (mdns_init() != ESP_OK) {
    ESP_LOGI(kLogTag, "RTL_WEB_MDNS init_failed");
    return;
  }
  mdns_hostname_set("orcsdr");
  mdns_instance_name_set("OrcSDR");
  mdns_service_add("OrcSDR", "_http", "_tcp", 80, nullptr, 0);
  g_mdns_started = true;
  ESP_LOGI(kLogTag, "RTL_WEB_MDNS ok dma_largest=%lu", static_cast<unsigned long>(dma_largest()));
}

void stop_server() {
  if (g_server != nullptr) {
    httpd_stop(g_server);
    g_server = nullptr;
  }
  stop_mdns();
  if (g_listening) ESP_LOGI(kLogTag, "RTL_WEB_STOP");
  g_listening = false;
}

bool start_server() {
  if (g_server != nullptr) return true;
  ESP_LOGI(kLogTag, "RTL_WEB_DMA pre_start free=%lu largest=%lu",
                static_cast<unsigned long>(
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
                static_cast<unsigned long>(dma_largest()));
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  // 3 sockets are reserved; 5 leaves two clients (status + audio).
  config.max_open_sockets = 5;
  config.max_uri_handlers = 8;
  config.lru_purge_enable = true;
  config.stack_size = 8192;
  config.core_id = tskNO_AFFINITY;
  // Hosted SDIO asserts if this heap is empty. Keep the httpd stack in PSRAM.
  config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  if (httpd_start(&g_server, &config) != ESP_OK) {
    g_server = nullptr;
    ESP_LOGI(kLogTag, "RTL_WEB_ERROR start_failed");
    return false;
  }
  httpd_uri_t root{};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = handle_root;
  httpd_uri_t badge{};
  badge.uri = "/badge.png";
  badge.method = HTTP_GET;
  badge.handler = handle_badge;
  httpd_uri_t status{};
  status.uri = "/api/status";
  status.method = HTTP_GET;
  status.handler = handle_status;
  httpd_uri_t action{};
  action.uri = "/api/action";
  action.method = HTTP_POST;
  action.handler = handle_action;
  httpd_uri_t audio{};
  audio.uri = "/api/audio";
  audio.method = HTTP_GET;
  audio.handler = handle_audio;
  httpd_register_uri_handler(g_server, &root);
  httpd_register_uri_handler(g_server, &badge);
  httpd_register_uri_handler(g_server, &status);
  httpd_register_uri_handler(g_server, &action);
  httpd_uri_t spectrum{};
  spectrum.uri = "/api/spectrum";
  spectrum.method = HTTP_GET;
  spectrum.handler = handle_spectrum;
  httpd_uri_t audiowav = audio;
  audiowav.uri = "/api/audio.wav";
  httpd_register_uri_handler(g_server, &audio);
  httpd_register_uri_handler(g_server, &audiowav);
  httpd_register_uri_handler(g_server, &spectrum);
  start_mdns();
  g_listening = true;
  ESP_LOGI(kLogTag, "RTL_WEB_LISTEN port=80");
  return true;
}

}  // namespace

void set_enabled(bool enabled) { g_enabled.store(enabled, std::memory_order_release); }

bool enabled() { return g_enabled.load(std::memory_order_acquire); }

bool listening() { return g_listening; }

void poll(bool wifi_connected) {
  const bool want = g_enabled.load(std::memory_order_acquire) && wifi_connected;
  if (!want) {
    g_wifi_up_ms = 0;
    if (g_listening) stop_server();
    return;
  }
  if (g_wifi_up_ms == 0) g_wifi_up_ms = millis();
  if (g_listening) return;
  if (millis() - g_wifi_up_ms < 2500) return;
  start_server();
}

void update(const Snapshot& snapshot) {
  portENTER_CRITICAL(&g_mux);
  g_snapshot = snapshot;
  portEXIT_CRITICAL(&g_mux);
}

bool take_command(Command* command) {
  if (command == nullptr) return false;
  portENTER_CRITICAL(&g_mux);
  const bool have = g_pending.kind != CommandKind::none;
  if (have) {
    *command = g_pending;
    g_pending = {};
  }
  portEXIT_CRITICAL(&g_mux);
  return have;
}

void tap_audio(const int16_t* samples, size_t frames, size_t stride) {
  if (!g_enabled.load(std::memory_order_relaxed) || samples == nullptr || frames < 3)
    return;
  if (stride == 0) stride = 1;
  uint32_t w = g_audio_w.load(std::memory_order_relaxed);
  for (size_t i = 0; i + 2 < frames; i += 3) {
    g_audio_ring[w % kAudioRing] = samples[i * stride];
    ++w;
  }
  g_audio_w.store(w, std::memory_order_release);
}

void update_spectrum(const float* levels, size_t count) {
  if (levels == nullptr || count < 2) return;
  float samples[kSpectrumBins]{};
  float sum = 0.0f;
  for (size_t i = 0; i < kSpectrumBins; ++i) {
    const size_t source = i * count / kSpectrumBins;
    samples[i] = levels[source];
    sum += samples[i];
  }
  const float floor = sum / static_cast<float>(kSpectrumBins) - 4.0f;
  for (size_t i = 0; i < kSpectrumBins; ++i) {
    const float norm = (samples[i] - floor) / 24.0f;
    const float clamped = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    g_spec[i] = static_cast<uint8_t>(clamped * 255.0f);
  }
  g_spec_count = kSpectrumBins;
}

size_t copy_spectrum(uint8_t* output, size_t max_bins) {
  if (output == nullptr || max_bins == 0) return 0;
  const size_t n = g_spec_count < max_bins ? g_spec_count : max_bins;
  memcpy(output, g_spec, n);
  return n;
}

size_t copy_audio(int16_t* output, size_t max_samples) {
  if (output == nullptr || max_samples == 0) return 0;
  const uint32_t w = g_audio_w.load(std::memory_order_acquire);
  uint32_t r = g_audio_r.load(std::memory_order_relaxed);
  uint32_t avail = w - r;
  if (avail > kAudioRing) {
    r = w - static_cast<uint32_t>(max_samples);
    avail = max_samples;
  }
  if (avail > max_samples) {
    r = w - static_cast<uint32_t>(max_samples);
    avail = static_cast<uint32_t>(max_samples);
  }
  for (uint32_t i = 0; i < avail; ++i)
    output[i] = g_audio_ring[(r + i) % kAudioRing];
  g_audio_r.store(r + avail, std::memory_order_release);
  return avail;
}

void format_url(char* out, size_t out_size, const char* ip) {
  if (out == nullptr || out_size == 0) return;
  if (ip == nullptr || ip[0] == '\0' || strcmp(ip, "0.0.0.0") == 0) {
    out[0] = '\0';
    return;
  }
  snprintf(out, out_size, "http://%s/", ip);
}

bool self_check() {
  char url[48];
  format_url(url, sizeof(url), "192.0.2.10");
  if (strcmp(url, "http://192.0.2.10/") != 0) return false;
  format_url(url, sizeof(url), "");
  if (url[0] != '\0') return false;
  const size_t html = static_cast<size_t>(web_console_html_end - web_console_html_start);
  return html > 800 && html < 98304;
}

}  // namespace orcsdr::web_console
