#include "wifi_app.hpp"
#include <atomic>
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_store.hpp"

namespace wifi_app {
namespace {
constexpr char TAG[] = "WIFI_APP";
std::atomic<bool> g_scan_pending{false}, g_connect_pending{false}, g_disconnect_pending{false}, g_started{false};
std::atomic<bool> g_scanning{false}, g_connecting{false};
char g_pending_ssid[33], g_pending_pass[65];
bool g_pending_remember = false;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
orcsdr::wifi::ScanResult g_results[16];
int g_result_count = 0;
char g_saved_ssid[33];
char g_saved_pass[65];
char g_stage[24];
int g_code = 0;

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
  load_saved();
  ESP_LOGI(TAG, "starting the ESP32-C6 link (SDIO, reset GPIO 54)");
  const bool ok = orcsdr::wifi::start();
  std::snprintf(g_stage, sizeof(g_stage), "%s", orcsdr::wifi::hosted_failure_stage());
  g_code = orcsdr::wifi::hosted_failure_code();
  if (!ok) {
    ESP_LOGE(TAG, "Wi-Fi unavailable: stage=%s code=0x%x c6=%s", g_stage, g_code, orcsdr::wifi::hosted_c6_version());
    vTaskDelete(nullptr);
    return;
  }
  g_started = true;
  ESP_LOGI(TAG, "Wi-Fi driver up, C6 firmware %s", orcsdr::wifi::hosted_c6_version());
  int64_t last_try = 0;
  bool first = true;
  for (;;) {
    if (g_scan_pending.exchange(false)) {
      g_scanning = true;
      if (orcsdr::wifi::begin_scan()) {
        vTaskDelay(pdMS_TO_TICKS(2500));
        orcsdr::wifi::ScanResult tmp[16];
        const int n = orcsdr::wifi::scan_results(tmp, 16);
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
    if ((first || now - last_try > 30000000) && g_saved_ssid[0] && !orcsdr::wifi::connected() && !g_connecting) {
      first = false;
      last_try = now;
      std::snprintf(g_pending_ssid, sizeof(g_pending_ssid), "%s", g_saved_ssid);
      std::snprintf(g_pending_pass, sizeof(g_pending_pass), "%s", g_saved_pass);
      g_pending_remember = false;
      g_connect_pending = true;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
}  // namespace

void start() { xTaskCreatePinnedToCore(task, "wifi_app", 8192, nullptr, 2, nullptr, 0); }

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
