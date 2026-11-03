#include "web_server.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "home_app.hpp"
#include "lwip/sockets.h"
#include "nvs_store.hpp"
#include "sdkconfig.h"
#include "web_auth.hpp"
#include "web_control.hpp"
#include "wifi_app.hpp"

#ifndef ORCSDR_HAS_WEB_UI
#define ORCSDR_HAS_WEB_UI 0
#endif

namespace web_server {
namespace {
constexpr char TAG[] = "WEB";
constexpr size_t kJsonBytes = 4096;
constexpr size_t kPostBytes = 512;
constexpr int64_t kLinkSettleUs = 1500000;

#if ORCSDR_HAS_WEB_UI
#define WEB_ASSET(name, sym)                                                   \
  extern const uint8_t name##_start[] asm("_binary_" sym "_start");            \
  extern const uint8_t name##_end[] asm("_binary_" sym "_end");
WEB_ASSET(a_html, "m5_html_gz")
WEB_ASSET(a_app, "app_js_gz")
WEB_ASSET(a_core_js, "core_js_gz")
WEB_ASSET(a_core_wasm, "core_wasm_gz")
WEB_ASSET(a_decoders, "decoders_js_gz")
WEB_ASSET(a_worker, "decoder_worker_js_gz")
WEB_ASSET(a_orcdec_js, "orcdec_js_gz")
WEB_ASSET(a_orcdec_wasm, "orcdec_wasm_gz")
WEB_ASSET(a_splash, "splash_jpg")
WEB_ASSET(a_login, "orclogin_js")
struct Asset { const char* path; const uint8_t* begin; const uint8_t* end; const char* mime; bool gzip; };
const Asset kAssets[] = {
    {"/m5", a_html_start, a_html_end, "text/html", true},
    {"/m5/app.js", a_app_start, a_app_end, "application/javascript", true},
    {"/m5/core.js", a_core_js_start, a_core_js_end, "application/javascript", true},
    {"/m5/core.wasm", a_core_wasm_start, a_core_wasm_end, "application/wasm", true},
    {"/m5/decoders.js", a_decoders_start, a_decoders_end, "application/javascript", true},
    {"/m5/decoder_worker.js", a_worker_start, a_worker_end, "application/javascript", true},
    {"/m5/orcdec.js", a_orcdec_js_start, a_orcdec_js_end, "application/javascript", true},
    {"/m5/orcdec.wasm", a_orcdec_wasm_start, a_orcdec_wasm_end, "application/wasm", true},
    {"/m5/splash.jpg", a_splash_start, a_splash_end, "image/jpeg", false},
    {"/m5/orclogin.js", a_login_start, a_login_end, "application/javascript", false},
};
#endif

httpd_handle_t g_server = nullptr;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_listening{false};
char* g_json = nullptr;  // PSRAM; ESP HTTPD serializes requests for this server.
char g_url[40] = "offline";

bool current_ip(char out[16]) {
  out[0] = '\0';
  const wifi_app::Status w = wifi_app::status();
  if (w.connected && w.ip[0]) {
    std::snprintf(out, 16, "%s", w.ip);
    return true;
  }
  return false;
}

bool host_allowed(httpd_req_t* req, char* host_out, size_t cap) {
  if (!cap || httpd_req_get_hdr_value_str(req, "Host", host_out, cap) != ESP_OK) return false;
  char host[64];
  std::snprintf(host, sizeof(host), "%s", host_out);
  char* colon = std::strchr(host, ':');
  if (colon) *colon = '\0';
  char ip[16];
  const bool ip_match = current_ip(ip) && std::strcmp(host, ip) == 0;
  const bool mdns_match = strcasecmp(host, "orcsdr.local") == 0;
  return ip_match || mdns_match;
}

bool origin_allowed(httpd_req_t* req, const char* host) {
  const size_t length = httpd_req_get_hdr_value_len(req, "Origin");
  if (!length) return true;
  char origin[96], expected[96];
  if (length >= sizeof(origin) || httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) return false;
  std::snprintf(expected, sizeof(expected), "http://%s", host);
  return std::strcmp(origin, expected) == 0;
}

esp_err_t reject(httpd_req_t* req, const char* status, const char* error) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  char body[128];
  const int n = std::snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", error);
  return httpd_resp_send(req, body, n > 0 ? n : 0);
}

esp_err_t send_json(httpd_req_t* req, const char* body, size_t length) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  return httpd_resp_send(req, body, static_cast<ssize_t>(length));
}

esp_err_t check_site(httpd_req_t* req) {
  char host[80];
  if (!host_allowed(req, host, sizeof(host))) return reject(req, "403 Forbidden", "host_rejected");
  if (!origin_allowed(req, host)) return reject(req, "403 Forbidden", "origin_rejected");
  return ESP_OK;
}

uint32_t peer_address_impl(httpd_req_t* req) {
  const int fd = httpd_req_to_sockfd(req);
  sockaddr_storage addr{};
  socklen_t size = sizeof(addr);
  if (fd < 0 || getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &size) != 0) return 0;
  if (addr.ss_family == AF_INET) return reinterpret_cast<sockaddr_in*>(&addr)->sin_addr.s_addr;
  // Stable per-address throttle identity for IPv6 peers, including IPv4-mapped addresses.
  const auto* bytes = reinterpret_cast<const uint8_t*>(&reinterpret_cast<sockaddr_in6*>(&addr)->sin6_addr);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < 16; ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash ? hash : 1;
}

bool route_and_query(httpd_req_t* req, char route[64], char query[256]) {
  constexpr size_t kPrefixLength = sizeof("/api/v1/") - 1;
  if (std::strncmp(req->uri, "/api/v1/", kPrefixLength) != 0) return false;
  const char* path = req->uri + kPrefixLength;
  const char* end = std::strchr(path, '?');
  const size_t n = end ? static_cast<size_t>(end - path) : std::strlen(path);
  if (n == 0 || n >= 64) return false;
  std::memcpy(route, path, n);
  route[n] = '\0';
  query[0] = '\0';
  if (httpd_req_get_url_query_str(req, query, 256) != ESP_OK && end) return false;
  return true;
}

bool read_body(httpd_req_t* req, char body[kPostBytes + 1]) {
  if (req->content_len > kPostBytes) return false;
  size_t received = 0;
  while (received < req->content_len) {
    const int n = httpd_req_recv(req, body + received, req->content_len - received);
    if (n <= 0) return false;
    received += static_cast<size_t>(n);
  }
  body[received] = '\0';
  return true;
}

esp_err_t send_challenge(httpd_req_t* req) {
  char nonce[33], salt[33];
  if (!web_auth::issue_challenge(nonce)) return reject(req, "503 Service Unavailable", "challenge_busy");
  web_auth::salt_hex(salt);
  const int n = std::snprintf(g_json, kJsonBytes,
      "{\"ok\":true,\"user\":\"admin\",\"nonce\":\"%s\",\"salt\":\"%s\",\"iterations\":%u,\"default_password\":%s}",
      nonce, salt, static_cast<unsigned>(web_auth::kIterations), web_auth::default_password() ? "true" : "false");
  return send_json(req, g_json, n > 0 ? static_cast<size_t>(n) : 0);
}

esp_err_t handle_home_get(httpd_req_t* req) {
  const home_app::Status s = home_app::status();
  // Strings are local, bounded values owned by Home; escape them before emitting JSON.
  auto escape = [](const char* input, char* output, size_t capacity) {
    size_t used = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(input); *p && used + 2 < capacity; ++p) {
      if (*p == '"' || *p == '\\') output[used++] = '\\';
      if (*p >= 0x20) output[used++] = static_cast<char>(*p);
    }
    output[used] = '\0';
  };
  char band[64], mode[32];
  escape(s.band, band, sizeof(band));
  escape(s.mode, mode, sizeof(mode));
  const int n = std::snprintf(g_json, kJsonBytes,
      "{\"ok\":true,\"ui_ready\":%s,\"ui_failed\":%s,\"home_active\":%s,\"frequency_hz\":%u,\"applied_hz\":%u,\"band\":\"%s\",\"mode\":\"%s\",\"step_hz\":%u,\"span_hz\":%u,\"volume\":null,\"mute\":null,\"filter_bandwidth_hz\":%u,\"step_adjustable\":%s,\"mode_choice\":%u,\"mode_active\":%u,\"mode_suggested\":%u,\"mode_selectable\":%s,\"web_action_sequence\":%u,\"web_action_id\":%u,\"web_action_result\":\"%s\"}",
      s.ui_ready ? "true" : "false", s.ui_failed ? "true" : "false", s.home_active ? "true" : "false",
      static_cast<unsigned>(s.frequency_hz), static_cast<unsigned>(s.applied_hz), band, mode,
      static_cast<unsigned>(s.step_hz), static_cast<unsigned>(s.span_hz),
      static_cast<unsigned>(s.filter_bandwidth_hz), s.step_adjustable ? "true" : "false",
      static_cast<unsigned>(s.mode_choice), static_cast<unsigned>(s.mode_active),
      static_cast<unsigned>(s.mode_suggested), s.mode_selectable ? "true" : "false",
      static_cast<unsigned>(s.web_action_sequence), static_cast<unsigned>(s.web_action_id),
      home_app::web_actions::result_name(s.web_action_result));
  return send_json(req, g_json, n > 0 ? static_cast<size_t>(n) : 0);
}

bool parse_receipt_id(const char* query, uint32_t* id) {
  if (!query || !id || std::strncmp(query, "id=", 3) != 0 || !query[3]) return false;
  uint64_t value = 0;
  for (const char* p = query + 3; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    const unsigned digit = static_cast<unsigned>(*p - '0');
    if (value > (UINT32_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  *id = static_cast<uint32_t>(value);
  return *id != 0;
}

esp_err_t handle_home_receipt_get(httpd_req_t* req, const char* query) {
  uint32_t id = 0;
  if (!parse_receipt_id(query, &id)) return reject(req, "400 Bad Request", "bad_request_id");
  home_app::ActionReceipt receipt{};
  if (!home_app::web_action_receipt(id, &receipt)) {
    const int n = std::snprintf(g_json, kJsonBytes,
        "{\"ok\":true,\"request_id\":%u,\"known\":false,\"state\":\"pending_or_expired_or_unknown\"}", unsigned(id));
    return send_json(req, g_json, n > 0 ? static_cast<size_t>(n) : 0);
  }
  const int n = std::snprintf(g_json, kJsonBytes,
      "{\"ok\":true,\"request_id\":%u,\"known\":true,\"state\":\"dispatch_complete\",\"dispatch_result\":\"%s\"}",
      unsigned(receipt.request_id), home_app::web_actions::result_name(receipt.dispatch_result));
  return send_json(req, g_json, n > 0 ? static_cast<size_t>(n) : 0);
}

bool known_read_not_integrated(const char* route) {
  static constexpr const char* routes[] = {"receivers", "fm", "report", "tuner", "spectrum", "time", "settings", "wifi"};
  for (const char* candidate : routes)
    if (std::strcmp(route, candidate) == 0) return true;
  return false;
}

esp_err_t handle_index(httpd_req_t* req) {
  const char* json = ORCSDR_HAS_WEB_UI
      ? "{\"ok\":true,\"api\":\"/api/v1\",\"read_only\":false,\"ui\":true,\"note\":\"P4 web endpoints are being integrated\"}"
      : "{\"ok\":true,\"api\":\"/api/v1\",\"read_only\":false,\"ui\":false,\"note\":\"Web UI bundle is not packaged\"}";
  return send_json(req, json, std::strlen(json));
}

esp_err_t handle_api_get(httpd_req_t* req) {
  if (const esp_err_t check = check_site(req); check != ESP_OK) return check;
  if (std::strcmp(req->uri, "/api/v1/auth/challenge") == 0) return send_challenge(req);
  if (std::strcmp(req->uri, "/api/v1") == 0 || std::strcmp(req->uri, "/api/v1/") == 0) return handle_index(req);

  char route[64], query[256];
  if (!route_and_query(req, route, query)) return reject(req, "400 Bad Request", "bad_route");
  if (std::strcmp(route, "home") == 0) return handle_home_get(req);
  if (std::strcmp(route, "home/action") == 0) return handle_home_receipt_get(req, query);
  if (known_read_not_integrated(route)) return reject(req, "501 Not Implemented", "endpoint_not_integrated");
  return reject(req, "404 Not Found", "unknown_route");
}

esp_err_t handle_login(httpd_req_t* req, const char* body) {
  char nonce[40]{}, proof[80]{};
  if (httpd_query_key_value(body, "nonce", nonce, sizeof(nonce)) != ESP_OK ||
      httpd_query_key_value(body, "proof", proof, sizeof(proof)) != ESP_OK)
    return reject(req, "400 Bad Request", "bad_login_request");
  web_auth::LoginOut out{};
  switch (web_auth::login(peer_address_impl(req), nonce, proof, out)) {
    case web_auth::Login::ok: {
      const int n = std::snprintf(g_json, kJsonBytes,
          "{\"ok\":true,\"session\":\"%s\",\"device\":\"%s\",\"idle_s\":300,\"default_password\":%s}",
          out.sid, out.device_proof, web_auth::default_password() ? "true" : "false");
      return send_json(req, g_json, n > 0 ? static_cast<size_t>(n) : 0);
    }
    case web_auth::Login::bad_request: return reject(req, "400 Bad Request", "bad_login_request");
    case web_auth::Login::bad_nonce: return reject(req, "401 Unauthorized", "bad_nonce");
    case web_auth::Login::bad_proof: return reject(req, "401 Unauthorized", "login_failed");
    case web_auth::Login::throttled: return reject(req, "429 Too Many Requests", "login_throttled");
    default: return reject(req, "503 Service Unavailable", "login_unavailable");
  }
}

esp_err_t handle_api_post(httpd_req_t* req) {
  if (const esp_err_t check = check_site(req); check != ESP_OK) return check;
  if (std::strcmp(req->uri, "/api/v1/auth/login") != 0 && std::strncmp(req->uri, "/api/v1/", 8) != 0)
    return reject(req, "404 Not Found", "unknown_route");

  char body[kPostBytes + 1];
  if (!read_body(req, body)) return reject(req, "413 Payload Too Large", "body_too_large_or_incomplete");
  if (std::strcmp(req->uri, "/api/v1/auth/login") == 0) return handle_login(req, body);

  char route[64], query[256];
  if (!route_and_query(req, route, query)) return reject(req, "400 Bad Request", "bad_route");
  char sid[24]{}, sequence[24]{}, signature[72]{};
  if (httpd_req_get_hdr_value_str(req, "X-Orc-Session", sid, sizeof(sid)) != ESP_OK ||
      httpd_req_get_hdr_value_str(req, "X-Orc-Seq", sequence, sizeof(sequence)) != ESP_OK ||
      httpd_req_get_hdr_value_str(req, "X-Orc-Sig", signature, sizeof(signature)) != ESP_OK)
    return reject(req, "401 Unauthorized", "session_required");
  switch (web_auth::verify(sid, sequence, signature, "POST", req->uri,
                           reinterpret_cast<const uint8_t*>(body), req->content_len)) {
    case web_auth::Verify::ok: break;
    case web_auth::Verify::no_session:
    case web_auth::Verify::expired: return reject(req, "401 Unauthorized", "session_required");
    case web_auth::Verify::bad_seq: return reject(req, "401 Unauthorized", "bad_sequence");
    default: return reject(req, "401 Unauthorized", "bad_signature");
  }
  if (std::strcmp(route, "auth/logout") == 0) {
    web_auth::logout(sid);
    return send_json(req, "{\"ok\":true}", sizeof("{\"ok\":true}") - 1);
  }

  const web_control::Result result = web_control::run(route, query, peer_address_impl(req), g_json, kJsonBytes);
  if (result == web_control::Result::unknown_route) return reject(req, "404 Not Found", "unknown_route");
  if (result == web_control::Result::bad_request) httpd_resp_set_status(req, "400 Bad Request");
  else if (result == web_control::Result::not_available) httpd_resp_set_status(req, "501 Not Implemented");
  return send_json(req, g_json, std::strlen(g_json));
}

esp_err_t handle_root(httpd_req_t* req) {
  if (const esp_err_t check = check_site(req); check != ESP_OK) return check;
#if ORCSDR_HAS_WEB_UI
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/m5");
  return httpd_resp_send(req, nullptr, 0);
#else
  return reject(req, "503 Service Unavailable", "web_ui_not_packaged");
#endif
}

#if ORCSDR_HAS_WEB_UI
esp_err_t handle_ui(httpd_req_t* req) {
  if (const esp_err_t check = check_site(req); check != ESP_OK) return check;
  const char* query = std::strchr(req->uri, '?');
  const size_t path_length = query ? static_cast<size_t>(query - req->uri) : std::strlen(req->uri);
  for (const Asset& asset : kAssets) {
    if (std::strlen(asset.path) != path_length || std::memcmp(req->uri, asset.path, path_length) != 0) continue;
    httpd_resp_set_type(req, asset.mime);
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    if (asset.gzip) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, reinterpret_cast<const char*>(asset.begin), static_cast<ssize_t>(asset.end - asset.begin));
  }
  return reject(req, "404 Not Found", "unknown_asset");
}
#endif

void stop_server() {
  if (g_server) {
    httpd_stop(g_server);
    g_server = nullptr;
  }
  heap_caps_free(g_json);
  g_json = nullptr;
  g_listening = false;
  std::snprintf(g_url, sizeof(g_url), "offline");
  ESP_LOGI(TAG, "RTL_WEB_STOPPED");
}

bool start_server(const char* ip) {
  g_json = static_cast<char*>(heap_caps_malloc(kJsonBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_json) {
    ESP_LOGE(TAG, "web response buffer allocation failed");
    return false;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.stack_size = 6144;
  config.max_open_sockets = CONFIG_LWIP_MAX_SOCKETS >= 8 ? 5 : 3;
  config.max_uri_handlers = ORCSDR_HAS_WEB_UI ? 4 : 3;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 5;
  config.send_wait_timeout = 5;
  config.uri_match_fn = httpd_uri_match_wildcard;
  if (httpd_start(&g_server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    g_server = nullptr;
    stop_server();
    return false;
  }
  httpd_uri_t root{};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = handle_root;
  httpd_uri_t get{};
  get.uri = "/api/v1*";
  get.method = HTTP_GET;
  get.handler = handle_api_get;
  httpd_uri_t post{};
  post.uri = "/api/v1*";
  post.method = HTTP_POST;
  post.handler = handle_api_post;
  if (httpd_register_uri_handler(g_server, &root) != ESP_OK ||
      httpd_register_uri_handler(g_server, &get) != ESP_OK ||
      httpd_register_uri_handler(g_server, &post) != ESP_OK) {
    stop_server();
    return false;
  }
#if ORCSDR_HAS_WEB_UI
  httpd_uri_t ui{};
  ui.uri = "/m5*";
  ui.method = HTTP_GET;
  ui.handler = handle_ui;
  if (httpd_register_uri_handler(g_server, &ui) != ESP_OK) {
    stop_server();
    return false;
  }
#endif
  std::snprintf(g_url, sizeof(g_url), "http://%s/", ip);
  g_listening = true;
  ESP_LOGI(TAG, "RTL_WEB_LISTEN port=80 url=%s web_ui=%s", g_url, ORCSDR_HAS_WEB_UI ? "included" : "not included");
  return true;
}

void manager(void*) {
  int64_t link_up_since = 0;
  char serving_ip[16]{};
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    char ip[16];
    const bool link = current_ip(ip);
    if (!link) link_up_since = 0;
    else if (!link_up_since) link_up_since = esp_timer_get_time();
    const bool ready = g_enabled.load() && link && esp_timer_get_time() - link_up_since >= kLinkSettleUs;
    if (g_listening.load() && (!ready || std::strcmp(serving_ip, ip) != 0)) {
      stop_server();
      serving_ip[0] = '\0';
    }
    if (ready && !g_listening.load() && start_server(ip)) std::snprintf(serving_ip, sizeof(serving_ip), "%s", ip);
  }
}
}  // namespace

bool same_site_request(httpd_req_t* req) {
  char host[80];
  return host_allowed(req, host, sizeof(host)) && origin_allowed(req, host);
}

uint32_t peer_address(httpd_req_t* req) { return peer_address_impl(req); }

void start() {
  web_auth::init();
  orcsdr::NvsStore store;
  if (store.begin("orcsdr_web", true)) {
    g_enabled = store.get_bool("enabled", false);
    store.end();
  }
  BaseType_t result = xTaskCreate(manager, "web_mgr", 4096, nullptr, 3, nullptr);
  if (result != pdPASS) ESP_LOGE(TAG, "manager task creation failed; server remains unavailable");
}

void set_enabled(bool on) {
  orcsdr::NvsStore store;
  if (!store.begin("orcsdr_web", false) || !store.put_bool("enabled", on)) {
    store.end();
    ESP_LOGE(TAG, "could not persist RTL_WEB setting");
    return;
  }
  store.end();
  g_enabled = on;
}

bool enabled() { return g_enabled.load(); }
bool listening() { return g_listening.load(); }
const char* url() { return g_url; }
}  // namespace web_server
