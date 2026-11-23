#include "wifi_app.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_store.hpp"
#include "orcsdr_board_profile.h"

namespace wifi_app {
namespace {
constexpr char TAG[] = "WIFI_APP";
std::atomic<bool> g_scan_pending{false}, g_connect_pending{false}, g_disconnect_pending{false}, g_started{false};
std::atomic<bool> g_scanning{false}, g_connecting{false};
std::atomic<bool> g_power{true}, g_boot{true}, g_power_changed{false};
char g_pending_ssid[33], g_pending_pass[65];
bool g_pending_remember = false;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
orcsdr::wifi::ScanResult g_results[16];
int g_result_count = 0;
char g_saved_ssid[33];
char g_saved_pass[65];
char g_stage[24];
int g_code = 0;
int64_t g_retry_at_us = 0;
int g_losses = 0;

// When the SDIO link to the C6 breaks, the ESP-Hosted driver logs two errors per millisecond. At 115200 baud
// that stalls every task that prints (the whole UI crawled). This hook notices the flood, drops it and flags the
// link as lost so the Wi-Fi task can shut it down.
std::atomic<bool> g_sdio_lost{false};
int64_t g_window_start_us = 0;
int g_window_count = 0;
vprintf_like_t g_prev_vprintf = nullptr;

// The last log lines before the link broke, kept in PSRAM so the first failure is visible in the dump.
constexpr int kRing = 48, kLine = 112;
char* g_ring = nullptr;  // kRing * kLine
std::atomic<uint32_t> g_ring_next{0};
int g_sdio_stored = 0;

void ring_store(const char* text) {
  if (!g_ring) return;
  const uint32_t i = g_ring_next.fetch_add(1) % kRing;
  char* slot = g_ring + i * kLine;
  std::snprintf(slot, kLine, "%.*s", kLine - 1, text);
  for (char* c = slot; *c; ++c) if (*c == '\n' || *c == '\r') *c = ' ';
}

void ring_dump() {
  if (!g_ring) return;
  std::printf("WIFI_LINK_LOSS_DUMP uptime_s=%lld internal_free=%u internal_min_free=%u internal_largest=%u dma_free=%u\n",
              static_cast<long long>(esp_timer_get_time() / 1000000), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
              unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
              unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)));
  const uint32_t next = g_ring_next.load();
  for (int k = 0; k < kRing; ++k) {
    const char* line = g_ring + ((next + k) % kRing) * kLine;
    if (line[0]) std::printf("  | %s\n", line);
  }
}

int log_hook(const char* fmt, va_list args) {
  char head[96];
  va_list copy;
  va_copy(copy, args);
  vsnprintf(head, sizeof(head), fmt, copy);
  va_end(copy);
  if (std::strstr(head, "eh_sdio") || std::strstr(head, "sdmmc_io")) {
    const int64_t now = esp_timer_get_time();
    if (now - g_window_start_us > 1000000) {
      g_window_start_us = now;
      g_window_count = 0;
    }
    if (++g_window_count > 40) {
      g_sdio_lost = true;
      return 0;
    }
    if (g_sdio_stored < 6) {  // the first errors of an incident are the interesting ones
      ++g_sdio_stored;
      ring_store(head);
    }
  } else {
    ring_store(head);
  }
  return g_prev_vprintf ? g_prev_vprintf(fmt, args) : vprintf(fmt, args);
}

void load_saved() {
  orcsdr::NvsStore s;
  if (!s.begin("orcsdr_wifi", true)) return;
  std::snprintf(g_saved_ssid, sizeof(g_saved_ssid), "%s", s.get_string("ssid", "").c_str());
  std::snprintf(g_saved_pass, sizeof(g_saved_pass), "%s", s.get_string("pass", "").c_str());
  s.end();
}

void save(const char* ssid, const char* pass) {
  orcsdr::NvsStore s;
  if (!s.begin("orcsdr_wifi", false)) return;
  s.put_string("ssid", ssid);
  s.put_string("pass", pass);
  s.end();
  std::snprintf(g_saved_ssid, sizeof(g_saved_ssid), "%s", ssid);
  std::snprintf(g_saved_pass, sizeof(g_saved_pass), "%s", pass);
}

void task(void*) {
  // Let the display, touch and receiver come up first; a missing or mismatched C6 must never delay them.
  vTaskDelay(pdMS_TO_TICKS(4000));
  g_ring = static_cast<char*>(heap_caps_calloc(kRing, kLine, MALLOC_CAP_SPIRAM));
  g_prev_vprintf = esp_log_set_vprintf(log_hook);
  load_saved();
  {
    orcsdr::NvsStore s;
    if (s.begin("orcsdr_wifi", true)) {
      g_power = s.get_bool("power", true);
      g_boot = s.get_bool("boot", true);
      s.end();
    }
  }
  int64_t last_try = 0;
  bool first = true;
  bool link_attempted = false;
  for (;;) {
    // Bring the C6 link up once Wi-Fi is allowed; a failure is reported, not retried in a loop.
    if (g_power.load() && !g_started.load() && !link_attempted) {
      link_attempted = true;
      ESP_LOGI(TAG, "starting the ESP32-C6 link (SDIO, reset GPIO 54)");
      const bool ok = orcsdr::wifi::start();
      std::snprintf(g_stage, sizeof(g_stage), "%s", orcsdr::wifi::hosted_failure_stage());
      g_code = orcsdr::wifi::hosted_failure_code();
      if (!ok) {
        ESP_LOGE(TAG, "Wi-Fi unavailable: stage=%s code=0x%x c6=%s", g_stage, g_code, orcsdr::wifi::hosted_c6_version());
      } else {
        g_started = true;
        ESP_LOGI(TAG, "Wi-Fi driver up, C6 firmware %s", orcsdr::wifi::hosted_c6_version());
      }
    }
    if (g_power_changed.exchange(false)) {
      if (!g_power.load() && g_started.load()) {
        orcsdr::wifi::stop();
        g_started = false;
        link_attempted = true;  // the C6 link stays initialised; power-on restarts only the radio below
        ESP_LOGI(TAG, "Wi-Fi powered off");
      } else if (g_power.load() && !g_started.load() && link_attempted) {
        const bool ok = orcsdr::wifi::start();
        if (ok) g_started = true;
        ESP_LOGI(TAG, "Wi-Fi powered on: %s", ok ? "ok" : "failed");
      }
    }
    if (g_sdio_lost.exchange(false) && g_started.load()) {
      ESP_LOGE(TAG, "SDIO link to the C6 lost: shutting Wi-Fi down");
      ring_dump();
      orcsdr::wifi::stop();
      g_started = false;
      g_connecting = false;
      std::snprintf(g_stage, sizeof(g_stage), "sdio_link_lost");
      ++g_losses;
      g_sdio_stored = 0;
      if (g_losses <= 5) g_retry_at_us = esp_timer_get_time() + 15000000LL * g_losses;  // the restart pulses the C6 reset
    }
    if (!g_started.load() && g_retry_at_us && esp_timer_get_time() > g_retry_at_us && g_power.load()) {
      g_retry_at_us = 0;
      g_sdio_lost = false;
      const bool ok = orcsdr::wifi::start();
      if (ok) {
        g_started = true;
        g_stage[0] = 0;
      }
      ESP_LOGI(TAG, "Wi-Fi restart after link loss: %s", ok ? "ok" : "failed");
    }
    if (!g_started.load()) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    if (g_scan_pending.exchange(false)) {
      g_scanning = true;
      if (orcsdr::wifi::begin_scan()) {
        // scan_results() is -1 until the scan-done event arrives. A 2.4 GHz-only coprocessor finishes in about 2.5 s; a
        // dual-band ESP32-C5 took about 7 s on the bench, so poll instead of waiting a fixed time (15 s at most).
        orcsdr::wifi::ScanResult tmp[16];
        int n = -1;
        for (int waited = 0; waited < 60 && n < 0; ++waited) {
          vTaskDelay(pdMS_TO_TICKS(250));
          n = orcsdr::wifi::scan_results(tmp, 16);
        }
        portENTER_CRITICAL(&g_lock);
        g_result_count = n > 0 ? n : 0;
        for (int i = 0; i < g_result_count; ++i) g_results[i] = tmp[i];
        portEXIT_CRITICAL(&g_lock);
        ESP_LOGI(TAG, "scan: %d networks", n);
      }
      g_scanning = false;
    }
    if (g_disconnect_pending.exchange(false)) orcsdr::wifi::disconnect();
    if (g_connect_pending.exchange(false)) {
      g_connecting = true;
      ESP_LOGI(TAG, "connecting to \"%s\"", g_pending_ssid);
      const bool began = orcsdr::wifi::connect(g_pending_ssid, g_pending_pass);
      for (int i = 0; i < 150 && began && !orcsdr::wifi::connected() && !orcsdr::wifi::connect_failed(); ++i) vTaskDelay(pdMS_TO_TICKS(100));
      if (orcsdr::wifi::connected()) {
        ESP_LOGI(TAG, "connected: ip=%s rssi=%d", orcsdr::wifi::ip(), orcsdr::wifi::rssi());
        if (g_pending_remember) save(g_pending_ssid, g_pending_pass);
      } else {
        ESP_LOGW(TAG, "could not join \"%s\"", g_pending_ssid);
      }
      g_connecting = false;
    }
    const int64_t now = esp_timer_get_time();
    if (g_boot.load() && (first || now - last_try > 30000000) && g_saved_ssid[0] && !orcsdr::wifi::connected() && !g_connecting) {
      first = false;
      last_try = now;
      std::snprintf(g_pending_ssid, sizeof(g_pending_ssid), "%s", g_saved_ssid);
      std::snprintf(g_pending_pass, sizeof(g_pending_pass), "%s", g_saved_pass);
      g_pending_remember = false;
      g_connect_pending = true;
    }
    static int64_t last_mem_us = 0;
    if (now - last_mem_us > 600000000LL) {  // every 10 min: lets a log show the trend before a failure
      last_mem_us = now;
      ESP_LOGI(TAG, "mem: internal_free=%u min_free=%u dma_free=%u", unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
               unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)));
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
}  // namespace

void start() {
  // A board with no Wi-Fi coprocessor (the profile says so) never starts the ESP-Hosted link: nothing would answer.
  if (!orcsdr_board_profile()->has_hosted_c6) {
    ESP_LOGW(TAG, "board %s has no Wi-Fi radio; Wi-Fi is not started", orcsdr_board_profile()->name);
    std::snprintf(g_stage, sizeof(g_stage), "%s", "no Wi-Fi radio");
    return;
  }
  xTaskCreatePinnedToCore(task, "wifi_app", 8192, nullptr, 2, nullptr, 0);
}

Status status() {
  Status s;
  s.started = g_started.load();
  s.link_failed = orcsdr::wifi::link_failed();
  s.connected = s.started && orcsdr::wifi::connected();
  s.connecting = g_connecting.load();
  if (s.connected) {
    std::snprintf(s.ssid, sizeof(s.ssid), "%s", orcsdr::wifi::ssid());
    std::snprintf(s.ip, sizeof(s.ip), "%s", orcsdr::wifi::ip());
    s.rssi = orcsdr::wifi::rssi();
  }
  std::snprintf(s.c6_version, sizeof(s.c6_version), "%s", s.started || orcsdr::wifi::hosted_transport_ready() ? orcsdr::wifi::hosted_c6_version() : "");
  std::snprintf(s.failure_stage, sizeof(s.failure_stage), "%s", g_stage);
  s.failure_code = g_code;
  s.power = g_power.load();
  s.start_at_boot = g_boot.load();
  s.saved = g_saved_ssid[0] != 0;
  std::snprintf(s.saved_ssid, sizeof(s.saved_ssid), "%s", g_saved_ssid);
  return s;
}

bool request_scan() {
  if (!g_started.load()) return false;
  g_scan_pending = true;
  return true;
}
bool scanning() { return g_scanning.load(); }
int scan_results(orcsdr::wifi::ScanResult* out, size_t capacity) {
  portENTER_CRITICAL(&g_lock);
  const int n = std::min<int>(g_result_count, static_cast<int>(capacity));
  for (int i = 0; i < n; ++i) out[i] = g_results[i];
  portEXIT_CRITICAL(&g_lock);
  return n;
}
bool connect(const char* ssid, const char* password, bool remember) {
  if (!g_started.load() || !ssid || !ssid[0]) return false;
  std::snprintf(g_pending_ssid, sizeof(g_pending_ssid), "%s", ssid);
  std::snprintf(g_pending_pass, sizeof(g_pending_pass), "%s", password ? password : "");
  g_pending_remember = remember;
  g_connect_pending = true;
  return true;
}
void disconnect() { g_disconnect_pending = true; }
void simulate_link_loss() { g_sdio_lost = true; }
void set_power(bool enabled) {
  g_power = enabled;
  g_power_changed = true;
  orcsdr::NvsStore s;
  if (s.begin("orcsdr_wifi", false)) {
    s.put_bool("power", enabled);
    s.end();
  }
}
void set_start_at_boot(bool enabled) {
  g_boot = enabled;
  orcsdr::NvsStore s;
  if (s.begin("orcsdr_wifi", false)) {
    s.put_bool("boot", enabled);
    s.end();
  }
}
void forget() {
  orcsdr::NvsStore s;
  if (s.begin("orcsdr_wifi", false)) {
    s.remove("ssid");
    s.remove("pass");
    s.end();
  }
  g_saved_ssid[0] = 0;
  g_saved_pass[0] = 0;
}
}  // namespace wifi_app
