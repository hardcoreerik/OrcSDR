#include "wifi_service.hpp"

#include <cstring>
#include <atomic>

extern "C" {
#include <esp_event.h>
#include <esp_hosted.h>
#include <esp_netif.h>
#include <esp_wifi.h>
}

namespace orcsdr::wifi {
namespace {
bool g_started = false;
bool g_connected = false;
bool g_failed = false;
bool g_versions_match = false;
bool g_hosted_transport_ready = false;
std::atomic<bool> g_scan_done{false};
esp_netif_t* g_sta_netif = nullptr;
const char* g_failure_stage = "none";
int32_t g_failure_code = ESP_OK;
char g_ssid[33]{};
char g_ip[16]{"0.0.0.0"};

void on_wifi_event(void*, esp_event_base_t base, int32_t id, void*) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    g_connected = false;
    g_failed = true;
  }
  if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    g_scan_done.store(true, std::memory_order_release);
  }
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    g_connected = true;
    g_failed = false;
    auto* event = static_cast<ip_event_got_ip_t*>(nullptr);
    (void)event;
  }
}

void on_ip_event(void*, esp_event_base_t, int32_t, void* data) {
  const auto* event = static_cast<ip_event_got_ip_t*>(data);
  snprintf(g_ip, sizeof(g_ip), IPSTR, IP2STR(&event->ip_info.ip));
  g_connected = true;
  g_failed = false;
}
}

bool start() {
  if (g_started) return true;
  g_failure_stage = "none";
  g_failure_code = ESP_OK;
  const esp_err_t netif = esp_netif_init();
  if (netif != ESP_OK && netif != ESP_ERR_INVALID_STATE) {
    g_failure_stage = "netif"; g_failure_code = netif; return false;
  }
  const esp_err_t loop = esp_event_loop_create_default();
  if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE) {
    g_failure_stage = "event_loop"; g_failure_code = loop; return false;
  }
  if (g_sta_netif == nullptr) {
    g_sta_netif = esp_netif_create_default_wifi_sta();
    if (g_sta_netif == nullptr) { g_failure_stage = "sta_netif"; g_failure_code = ESP_FAIL; return false; }
  }
  const esp_err_t hosted_init = esp_hosted_init();
  if (hosted_init != ESP_OK) { g_failure_stage = "hosted_init"; g_failure_code = hosted_init; return false; }
  const esp_err_t hosted_connect = esp_hosted_connect_to_slave();
  if (hosted_connect != ESP_OK) {
    g_failure_stage = "hosted_connect"; g_failure_code = hosted_connect; return false;
  }
  g_hosted_transport_ready = true;
  esp_hosted_coprocessor_fwver_t cp{};
  const esp_err_t version = esp_hosted_get_coprocessor_fwversion(&cp);
  g_versions_match = version == ESP_OK &&
                     cp.major1 == 3 && cp.minor1 == 0 && cp.patch1 == 6;
  if (!g_versions_match) { g_failure_stage = "version"; g_failure_code = version; return false; }
  wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
  const esp_err_t wifi_init = esp_wifi_init(&wifi_cfg);
  if (wifi_init != ESP_OK && wifi_init != ESP_ERR_WIFI_INIT_STATE) {
    g_failure_stage = "wifi_init"; g_failure_code = wifi_init; return false;
  }
  const esp_err_t wifi_event = esp_event_handler_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr);
  if (wifi_event != ESP_OK) { g_failure_stage = "wifi_event"; g_failure_code = wifi_event; return false; }
  const esp_err_t ip_event = esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, nullptr);
  if (ip_event != ESP_OK) { g_failure_stage = "ip_event"; g_failure_code = ip_event; return false; }
  const esp_err_t wifi_mode = esp_wifi_set_mode(WIFI_MODE_STA);
  if (wifi_mode != ESP_OK) { g_failure_stage = "wifi_mode"; g_failure_code = wifi_mode; return false; }
  const esp_err_t wifi_start = esp_wifi_start();
  if (wifi_start != ESP_OK) { g_failure_stage = "wifi_start"; g_failure_code = wifi_start; return false; }
  g_started = true;
  return true;
}

void stop() { if (g_started) { esp_wifi_disconnect(); esp_wifi_stop(); } g_started = false; g_connected = false; }
bool begin_scan() {
  if (!g_started) return false;
  g_scan_done.store(false, std::memory_order_release);
  return esp_wifi_scan_start(nullptr, false) == ESP_OK;
}
int scan_results(ScanResult* results, size_t capacity) {
  uint16_t count = 0;
  if (!g_started || !g_scan_done.load(std::memory_order_acquire) ||
      esp_wifi_scan_get_ap_num(&count) != ESP_OK) return -1;
  if (results == nullptr || capacity == 0) return count;
  const uint16_t take = static_cast<uint16_t>(count < capacity ? count : capacity);
  wifi_ap_record_t records[16]{};
  uint16_t received = take < 16 ? take : 16;
  if (esp_wifi_scan_get_ap_records(&received, records) != ESP_OK) return -1;
  g_scan_done.store(false, std::memory_order_release);
  for (uint16_t i = 0; i < received; ++i) {
    strlcpy(results[i].ssid, reinterpret_cast<const char*>(records[i].ssid), sizeof(results[i].ssid));
    results[i].rssi = records[i].rssi; results[i].secure = records[i].authmode != WIFI_AUTH_OPEN;
  }
  return received;
}
bool connect(const char* network, const char* password) {
  if (!g_started || !network || !*network) return false;
  wifi_config_t config{};
  strlcpy(reinterpret_cast<char*>(config.sta.ssid), network, sizeof(config.sta.ssid));
  strlcpy(reinterpret_cast<char*>(config.sta.password), password ? password : "", sizeof(config.sta.password));
  g_failed = false; g_connected = false; strlcpy(g_ssid, network, sizeof(g_ssid));
  return esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK && esp_wifi_connect() == ESP_OK;
}
void disconnect() { esp_wifi_disconnect(); g_connected = false; }
bool connected() { return g_connected; }
bool connect_failed() { return g_failed; }
const char* ssid() { return g_ssid; }
const char* ip() { return g_ip; }
int16_t rssi() { wifi_ap_record_t ap{}; return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0; }
bool hosted_versions_match() { return g_versions_match; }
bool hosted_transport_ready() { return g_hosted_transport_ready; }
const char* hosted_failure_stage() { return g_failure_stage; }
int32_t hosted_failure_code() { return g_failure_code; }
}  // namespace orcsdr::wifi
