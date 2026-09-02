#include "wifi_service.hpp"

#include <cstring>
#include <atomic>

extern "C" {
#include <esp_event.h>
#include <esp_hosted.h>
#include <esp_hosted_transport_config.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
}

namespace orcsdr::wifi {
namespace {
bool g_started = false;
std::atomic<bool> g_connected{false};
std::atomic<bool> g_failed{false};
bool g_versions_match = false;
char g_c6_version[16]{"unknown"};
bool g_hosted_transport_ready = false;
std::atomic<bool> g_scan_done{false};
esp_netif_t* g_sta_netif = nullptr;
const char* g_failure_stage = "none";
int32_t g_failure_code = ESP_OK;
char g_ssid[33]{};
std::atomic<uint32_t> g_ip_addr{0};

const char* security_name(wifi_auth_mode_t authmode) {
  switch (authmode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK: return "WAPI";
    case WIFI_AUTH_OWE: return "OWE";
    case WIFI_AUTH_WPA3_ENT_192: return "WPA3-ENT";
    case WIFI_AUTH_DPP: return "DPP";
    case WIFI_AUTH_WPA3_ENTERPRISE: return "WPA3-ENT";
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return "WPA2/3-ENT";
    case WIFI_AUTH_WPA_ENTERPRISE: return "WPA-ENT";
    default: return "UNKNOWN";
  }
}

void format_phy(const wifi_ap_record_t& record, char* out, size_t size) {
  char* cursor = out;
  size_t remaining = size;
  const auto append = [&](const char* value) {
    const int written = snprintf(cursor, remaining, "%s%s", cursor == out ? "" : "/", value);
    if (written > 0 && static_cast<size_t>(written) < remaining) {
      cursor += written;
      remaining -= static_cast<size_t>(written);
    }
  };
  if (record.phy_11b) append("b");
  if (record.phy_11g) append("g");
  if (record.phy_11n) append("n");
  if (record.phy_11ax) append("ax");
  if (cursor == out) strlcpy(out, "unknown", size);
}

void on_wifi_event(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto* event = static_cast<const wifi_event_sta_disconnected_t*>(data);
    ESP_LOGW("orcsdr_wifi", "station disconnected reason=%u", event ? event->reason : 0u);
    g_connected.store(false, std::memory_order_release);
    g_failed.store(true, std::memory_order_release);
  }
  if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    g_scan_done.store(true, std::memory_order_release);
    ESP_LOGI("orcsdr_wifi", "scan done event");
  }
}

void on_ip_event(void*, esp_event_base_t, int32_t, void* data) {
  const auto* event = static_cast<ip_event_got_ip_t*>(data);
  g_ip_addr.store(event->ip_info.ip.addr, std::memory_order_release);
  g_failed.store(false, std::memory_order_release);
  g_connected.store(true, std::memory_order_release);
}
}

bool start() {
  if (g_started) return true;
  g_failure_stage = "none";
  g_failure_code = ESP_OK;
  g_versions_match = false;
  strlcpy(g_c6_version, "unknown", sizeof(g_c6_version));
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
  if (version == ESP_OK)
    snprintf(g_c6_version, sizeof(g_c6_version), "%lu.%lu.%lu",
             static_cast<unsigned long>(cp.major1), static_cast<unsigned long>(cp.minor1),
             static_cast<unsigned long>(cp.patch1));
  else
    strlcpy(g_c6_version, "unavailable", sizeof(g_c6_version));
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

void stop() { if (g_started) { esp_wifi_disconnect(); esp_wifi_stop(); } g_started = false; g_connected.store(false, std::memory_order_release); }
bool begin_scan() {
  if (!g_started) return false;
  g_scan_done.store(false, std::memory_order_release);
  const esp_err_t result = esp_wifi_scan_start(nullptr, false);
  if (result == ESP_OK) return true;
  ESP_LOGE("orcsdr_wifi", "scan start failed: %s", esp_err_to_name(result));
  return false;
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
    memcpy(results[i].bssid, records[i].bssid, sizeof(results[i].bssid));
    results[i].rssi = records[i].rssi;
    results[i].channel = records[i].primary;
    results[i].secondary_channel_offset = records[i].second == WIFI_SECOND_CHAN_ABOVE ? 4 :
                                          records[i].second == WIFI_SECOND_CHAN_BELOW ? -4 : 0;
    strlcpy(results[i].security, security_name(records[i].authmode), sizeof(results[i].security));
    format_phy(records[i], results[i].phy, sizeof(results[i].phy));
    results[i].secure = records[i].authmode != WIFI_AUTH_OPEN;
  }
  return received;
}
bool connect(const char* network, const char* password) {
  if (!g_started || !network || !*network) return false;
  wifi_config_t config{};
  strlcpy(reinterpret_cast<char*>(config.sta.ssid), network, sizeof(config.sta.ssid));
  strlcpy(reinterpret_cast<char*>(config.sta.password), password ? password : "", sizeof(config.sta.password));
  g_failed.store(false, std::memory_order_release);
  g_connected.store(false, std::memory_order_release);
  strlcpy(g_ssid, network, sizeof(g_ssid));
  const esp_err_t set_config = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (set_config != ESP_OK) {
    ESP_LOGE("orcsdr_wifi", "set_config failed: 0x%x", static_cast<unsigned>(set_config));
    return false;
  }
  const esp_err_t connect = esp_wifi_connect();
  if (connect != ESP_OK)
    ESP_LOGE("orcsdr_wifi", "connect request failed: 0x%x", static_cast<unsigned>(connect));
  return connect == ESP_OK;
}
void disconnect() { esp_wifi_disconnect(); g_connected.store(false, std::memory_order_release); }
bool connected() { return g_connected.load(std::memory_order_acquire); }
bool connect_failed() { return g_failed.load(std::memory_order_acquire); }
const char* ssid() { return g_ssid; }
const char* ip() {
  static char snapshot[16];
  esp_ip4_addr_t address{};
  address.addr = g_ip_addr.load(std::memory_order_acquire);
  snprintf(snapshot, sizeof(snapshot), IPSTR, IP2STR(&address));
  return snapshot;
}
int16_t rssi() { wifi_ap_record_t ap{}; return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0; }
bool hosted_versions_match() { return g_versions_match; }
const char* hosted_c6_version() { return g_c6_version; }
bool hosted_transport_ready() { return g_hosted_transport_ready; }
const char* hosted_failure_stage() { return g_failure_stage; }
int32_t hosted_failure_code() { return g_failure_code; }
}  // namespace orcsdr::wifi
