#include "web_server.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include "commands.hpp"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_store.hpp"
#include "eth_app.hpp"
#include "web_auth.hpp"
#include "web_maps.hpp"
#include "web_control.hpp"
#include "web_iq.hpp"
#include "web_compat.hpp"
#include "wifi_app.hpp"

#ifndef ORCSDR_HAS_WEB_UI
#define ORCSDR_HAS_WEB_UI 0
#endif

namespace web_server {
namespace {
constexpr char TAG[] = "WEB";
constexpr size_t kJsonBytes = 4096;
constexpr size_t kLegacyBytes = kJsonBytes;  // the page's receivers/report routes are larger than a command record

#if ORCSDR_HAS_WEB_UI
// The OrcSDR Web UI bundle, embedded at build time from ORCSDR_WEB_UI_DIR (see main/CMakeLists.txt). The .gz files are sent as they are
// with Content-Encoding: gzip; splash.jpg is sent plain.
#define WEB_ASSET(name, sym)                                                 \
  extern const uint8_t name##_start[] asm("_binary_" sym "_start");          \
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

struct Asset {
  const char* path;
  const uint8_t* start;
  const uint8_t* end;
  const char* type;
  bool gzip;
};
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
    {"/m5/orclogin.js", a_login_start, a_login_end, "application/javascript", false},  // node login, loaded by the page
};
#endif

httpd_handle_t g_server = nullptr;
std::atomic<bool> g_enabled{false}, g_listening{false};
orcsdr::commands::Result* g_result = nullptr;  // PSRAM; the server task handles one request at a time
char* g_json = nullptr;                        // PSRAM
char g_url[40] = "offline";

// ---- request checks ----------------------------------------------------------------------------------------------

// Only the device's own address (or its mDNS name) is accepted as Host, which defeats DNS-rebinding pages that point a
// hostile name at this address. `host` receives the header without a port.
bool host_allowed(httpd_req_t* req, char* host_header, size_t cap) {
  if (httpd_req_get_hdr_value_str(req, "Host", host_header, cap) != ESP_OK) return false;
  char host[64];
  std::snprintf(host, sizeof(host), "%s", host_header);
  if (char* colon = std::strchr(host, ':')) *colon = '\0';
  const wifi_app::Status w = wifi_app::status();
  const eth_app::Status e = eth_app::status();
  return (w.ip[0] && std::strcmp(host, w.ip) == 0) || (e.got_ip && e.ip[0] && std::strcmp(host, e.ip) == 0) || strcasecmp(host, "orcsdr.local") == 0;
}

// A browser sends Origin on cross-site requests; accept it only when it names this same host (CSRF belt and braces).
bool origin_allowed(httpd_req_t* req, const char* host_header) {
  const size_t len = httpd_req_get_hdr_value_len(req, "Origin");
  if (!len) return true;
  char origin[96], expect[96];
  if (len >= sizeof(origin) || httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) return false;
  std::snprintf(expect, sizeof(expect), "http://%s", host_header);
  return std::strcmp(origin, expect) == 0;
}

esp_err_t reject(httpd_req_t* req, const char* status, const char* message) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  char body[96];
  const int n = std::snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", message);
  return httpd_resp_send(req, body, n);
}

esp_err_t send_challenge(httpd_req_t* req);

// ---- routes ------------------------------------------------------------------------------------------------------

const char* access_name(orcsdr::commands::Access a) {
  switch (a) {
    case orcsdr::commands::Access::read: return "read";
    case orcsdr::commands::Access::control: return "control";
    default: return "admin";
  }
}

esp_err_t send_json(httpd_req_t* req, size_t n) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  return httpd_resp_send(req, g_json, static_cast<ssize_t>(n));
}

esp_err_t handle_index(httpd_req_t* req) {
  size_t count = 0;
  const orcsdr::commands::Command* list = orcsdr::commands::commands(&count);
  size_t n = static_cast<size_t>(std::snprintf(g_json, kJsonBytes, "{\"ok\":true,\"api\":\"/api/v1\",\"read_only\":true,\"ui\":%s,\"commands\":[",
                                               ORCSDR_HAS_WEB_UI ? "true" : "false"));
  bool first = true;
  for (size_t i = 0; i < count && n + 120 < kJsonBytes; ++i) {
    if (!list[i].http_path) continue;  // serial-only alias
    n += static_cast<size_t>(std::snprintf(g_json + n, kJsonBytes - n, "%s{\"path\":\"%s\",\"serial\":\"%s\",\"access\":\"%s\"}", first ? "" : ",",
                                           list[i].http_path, list[i].serial_line, access_name(list[i].access)));
    first = false;
  }
  n += static_cast<size_t>(std::snprintf(g_json + n, kJsonBytes - n, "]}"));
  return send_json(req, n);
}

#if ORCSDR_HAS_WEB_UI
esp_err_t send_asset(httpd_req_t* req, const Asset& a) {
  httpd_resp_set_type(req, a.type);
  if (a.gzip) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  return httpd_resp_send(req, reinterpret_cast<const char*>(a.start), static_cast<ssize_t>(a.end - a.start));
}

// The page itself and its files: "/" and "/m5" are the same page; "/m5/<file>" are its scripts, WebAssembly and splash image.
esp_err_t handle_ui(httpd_req_t* req) {
  char host_header[80];
  if (!host_allowed(req, host_header, sizeof(host_header))) return reject(req, "403 Forbidden", "host_rejected");
  if (!origin_allowed(req, host_header)) return reject(req, "403 Forbidden", "origin_rejected");
  char path[40];
  size_t i = 0;
  while (req->uri[i] && req->uri[i] != '?' && i + 1 < sizeof(path)) {
    path[i] = req->uri[i];
    ++i;
  }
  path[i] = '\0';
  if (i > 3 && path[i - 1] == '/') path[i - 1] = '\0';  // "/m5/" is "/m5"
  if (std::strcmp(path, "/") == 0) return send_asset(req, kAssets[0]);
  for (const Asset& a : kAssets)
    if (std::strcmp(a.path, path) == 0) return send_asset(req, a);
  return reject(req, "404 Not Found", "unknown_route");
}
#endif

esp_err_t handle_root(httpd_req_t* req) {
#if ORCSDR_HAS_WEB_UI
  return handle_ui(req);  // the web UI is the default page on every board that includes the bundle
#else
  char host_header[80];
  if (!host_allowed(req, host_header, sizeof(host_header))) return reject(req, "403 Forbidden", "host_rejected");
  if (!origin_allowed(req, host_header)) return reject(req, "403 Forbidden", "origin_rejected");
  return handle_index(req);
#endif
}

esp_err_t handle_api(httpd_req_t* req) {
  char host_header[80];
  if (!host_allowed(req, host_header, sizeof(host_header))) return reject(req, "403 Forbidden", "host_rejected");
  if (!origin_allowed(req, host_header)) return reject(req, "403 Forbidden", "origin_rejected");

  const char* uri = req->uri;  // "/api/v1", "/api/v1/", "/api/v1/mem?rx=0"
  const char* path = uri + std::strlen("/api/v1");
  if (*path == '/') ++path;
  char name[24];
  size_t i = 0;
  while (path[i] && path[i] != '?' && path[i] != '/' && i + 1 < sizeof(name)) {
    name[i] = path[i];
    ++i;
  }
  name[i] = '\0';
  if (std::strncmp(path, "auth/challenge", 14) == 0 && (path[14] == '\0' || path[14] == '?')) return send_challenge(req);
  if (path[i] != '\0' && path[i] != '?') return reject(req, "404 Not Found", "unknown_route");
  if (i == 0) return handle_index(req);

  // The receiver is chosen with `rx=` (command table) or `index=` (the web page's own routes).
  orcsdr::commands::Args args;
  char query[48];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char value[8];
    if (httpd_query_key_value(query, "rx", value, sizeof(value)) == ESP_OK || httpd_query_key_value(query, "index", value, sizeof(value)) == ESP_OK) {
      char* end = nullptr;
      const long rx = std::strtol(value, &end, 10);
      if (end == value || *end != '\0' || rx < 0 || rx > 3) return reject(req, "400 Bad Request", "bad_rx");
      args.rx = static_cast<int>(rx);
    }
  }

  // Routes the browser page already calls (receivers, fm, report, tuner, spectrum, time), in its own JSON shapes.
  {
    size_t len = 0;
    web_compat::Kind kind = web_compat::Kind::json;
    if (web_compat::get(name, args.rx, g_json, kLegacyBytes, &len, &kind)) {
      if (len == 0) return reject(req, "500 Internal Server Error", "response_too_large");
      httpd_resp_set_type(req, kind == web_compat::Kind::binary ? "application/octet-stream" : "application/json");
      httpd_resp_set_hdr(req, "Cache-Control", "no-store");
      httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
      return httpd_resp_send(req, g_json, static_cast<ssize_t>(len));
    }
  }

  const orcsdr::commands::Command* cmd = orcsdr::commands::find_path(name);
  if (!cmd) return reject(req, "404 Not Found", "unknown_route");

  // Everything served here is read tier. A caller is never authenticated on this path, so any non-read command would be
  // refused by the dispatcher; nothing is wired to the control tier yet.
  const orcsdr::commands::Caller caller{orcsdr::commands::Transport::http, false};
  const orcsdr::commands::Status status = orcsdr::commands::run(*cmd, caller, args, *g_result);
  if (status == orcsdr::commands::Status::auth_required) return reject(req, "401 Unauthorized", "auth_required");
  if (status == orcsdr::commands::Status::forbidden) return reject(req, "403 Forbidden", "forbidden");
  if (status != orcsdr::commands::Status::ok) return reject(req, "500 Internal Server Error", "command_failed");
  const size_t n = orcsdr::commands::to_json(*g_result, g_json, kJsonBytes);
  if (n == 0) return reject(req, "500 Internal Server Error", "response_too_large");
  return send_json(req, n);
}

// ---- login and the control tier ----------------------------------------------------------------------------------

uint32_t peer_address_impl(httpd_req_t* req) {
  const int fd = httpd_req_to_sockfd(req);
  sockaddr_storage addr{};
  socklen_t len = sizeof(addr);
  if (fd < 0 || getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
  if (addr.ss_family == AF_INET) return reinterpret_cast<sockaddr_in*>(&addr)->sin_addr.s_addr;
  uint32_t h = 0;  // IPv6 or mapped: fold the address so one client keeps one throttle slot
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&reinterpret_cast<sockaddr_in6*>(&addr)->sin6_addr);
  for (size_t i = 0; i < 16; ++i) h = h * 31 + b[i];
  return h;
}

esp_err_t send_challenge(httpd_req_t* req) {
  char nonce[33], salt[33];
  if (!web_auth::issue_challenge(nonce)) return reject(req, "503 Service Unavailable", "challenge_busy");
  web_auth::salt_hex(salt);
  const int n = std::snprintf(g_json, kJsonBytes, "{\"ok\":true,\"user\":\"admin\",\"nonce\":\"%s\",\"salt\":\"%s\",\"iterations\":%u,\"default_password\":%s}", nonce, salt,
                              static_cast<unsigned>(web_auth::kIterations), web_auth::default_password() ? "true" : "false");
  return send_json(req, static_cast<size_t>(n));
}

esp_err_t handle_login(httpd_req_t* req, const char* body) {
  char nonce[40] = {}, proof[80] = {};
  if (httpd_query_key_value(body, "nonce", nonce, sizeof(nonce)) != ESP_OK || httpd_query_key_value(body, "proof", proof, sizeof(proof)) != ESP_OK)
    return reject(req, "400 Bad Request", "bad_login_request");
  web_auth::LoginOut out{};
  switch (web_auth::login(peer_address_impl(req), nonce, proof, out)) {
    case web_auth::Login::ok: {
      const int n = std::snprintf(g_json, kJsonBytes, "{\"ok\":true,\"session\":\"%s\",\"device\":\"%s\",\"idle_s\":300,\"default_password\":%s}", out.sid, out.device_proof,
                                  web_auth::default_password() ? "true" : "false");
      return send_json(req, static_cast<size_t>(n));
    }
    case web_auth::Login::bad_request: return reject(req, "400 Bad Request", "bad_login_request");
    case web_auth::Login::bad_nonce: return reject(req, "401 Unauthorized", "bad_nonce");
    case web_auth::Login::bad_proof: return reject(req, "401 Unauthorized", "login_failed");
    case web_auth::Login::throttled: {
      char text[40];
      std::snprintf(text, sizeof(text), "throttled retry_s=%u", static_cast<unsigned>(out.retry_s));
      return reject(req, "429 Too Many Requests", text);
    }
    default: return reject(req, "503 Service Unavailable", "sessions_full");
  }
}

// POST /api/v1/auth/login (nonce + proof), /api/v1/auth/logout (signed), and signed control commands from the table.
esp_err_t handle_post(httpd_req_t* req) {
  char host_header[80];
  if (!host_allowed(req, host_header, sizeof(host_header))) return reject(req, "403 Forbidden", "host_rejected");
  if (!origin_allowed(req, host_header)) return reject(req, "403 Forbidden", "origin_rejected");

  char route[40];
  const char* p = req->uri + std::strlen("/api/v1");
  if (*p == '/') ++p;
  size_t i = 0;
  while (p[i] && p[i] != '?' && i + 1 < sizeof(route)) {
    route[i] = p[i];
    ++i;
  }
  route[i] = '\0';
  if (p[i] != '\0' && p[i] != '?') return reject(req, "404 Not Found", "unknown_route");

  constexpr size_t kBodyMax = 160;
  if (req->content_len > kBodyMax) return reject(req, "413 Payload Too Large", "body_too_large");
  char body[kBodyMax + 1];
  size_t got = 0;
  while (got < req->content_len) {
    const int r = httpd_req_recv(req, body + got, req->content_len - got);
    if (r <= 0) return ESP_FAIL;
    got += static_cast<size_t>(r);
  }
  body[got] = '\0';

  if (std::strcmp(route, "auth/login") == 0) return handle_login(req, body);

  // Everything below needs a signed session. The signature covers the sequence number, method, the exact target and the body.
  char sid[24], seq[24], sig[72];
  if (httpd_req_get_hdr_value_str(req, "X-Orc-Session", sid, sizeof(sid)) != ESP_OK || httpd_req_get_hdr_value_str(req, "X-Orc-Seq", seq, sizeof(seq)) != ESP_OK ||
      httpd_req_get_hdr_value_str(req, "X-Orc-Sig", sig, sizeof(sig)) != ESP_OK)
    return reject(req, "401 Unauthorized", "session_required");
  switch (web_auth::verify(sid, seq, sig, "POST", req->uri, reinterpret_cast<const uint8_t*>(body), got)) {
    case web_auth::Verify::ok: break;
    case web_auth::Verify::no_session:
    case web_auth::Verify::expired: return reject(req, "401 Unauthorized", "session_required");
    case web_auth::Verify::bad_seq: return reject(req, "401 Unauthorized", "bad_sequence");
    default: return reject(req, "401 Unauthorized", "bad_signature");
  }
  if (std::strcmp(route, "auth/logout") == 0) {
    web_auth::logout(sid);
    return send_json(req, static_cast<size_t>(std::snprintf(g_json, kJsonBytes, "{\"ok\":true}")));
  }

  if (web_maps::is_route(route)) return web_maps::handle(req, route);  // basemap delivery (signed like everything below)
  // The page's own control routes (receivers/tune, tuner/gain, fm, time ...), same signed session.
  {
    char q[96] = {};
    httpd_req_get_url_query_str(req, q, sizeof(q));
    const web_control::Result r = web_control::run(route, q, peer_address_impl(req), g_json, kJsonBytes);
    if (r != web_control::Result::unknown_route) {
      if (r == web_control::Result::bad_request) httpd_resp_set_status(req, "400 Bad Request");
      else if (r == web_control::Result::not_available) httpd_resp_set_status(req, "501 Not Implemented");
      return send_json(req, std::strlen(g_json));
    }
  }
  const orcsdr::commands::Command* cmd = orcsdr::commands::find_path(route);
  if (!cmd || cmd->access != orcsdr::commands::Access::control) return reject(req, "404 Not Found", "unknown_route");
  orcsdr::commands::Args args;
  char query[48];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char value[16];
    const char* key = std::strcmp(route, "freq_set") == 0 ? "hz" : "level";
    if (httpd_query_key_value(query, key, value, sizeof(value)) == ESP_OK) {
      char* end = nullptr;
      const long long v = std::strtoll(value, &end, 10);
      if (end == value || *end != '\0') return reject(req, "400 Bad Request", "bad_value");
      args.value = v;
    }
  }
  const orcsdr::commands::Caller caller{orcsdr::commands::Transport::http, true};
  const orcsdr::commands::Status status = orcsdr::commands::run(*cmd, caller, args, *g_result);
  if (status == orcsdr::commands::Status::invalid) return reject(req, "400 Bad Request", "invalid_value");
  if (status != orcsdr::commands::Status::ok) return reject(req, "403 Forbidden", "forbidden");
  const size_t n = orcsdr::commands::to_json(*g_result, g_json, kJsonBytes);
  if (n == 0) return reject(req, "500 Internal Server Error", "response_too_large");
  return send_json(req, n);
}

// ---- server lifecycle --------------------------------------------------------------------------------------------

void stop_server() {
  web_iq::shutdown();  // the port-81 server and any channel stream go with the web server
  if (g_server) {
    httpd_stop(g_server);  // waits for a request in flight
    g_server = nullptr;
  }
  heap_caps_free(g_result);
  heap_caps_free(g_json);
  g_result = nullptr;
  g_json = nullptr;
  g_listening = false;
  std::snprintf(g_url, sizeof(g_url), "offline");
  ESP_LOGI(TAG, "RTL_WEB_STOPPED");
}

bool start_server(const char* ip) {
  g_result = static_cast<orcsdr::commands::Result*>(heap_caps_calloc(1, sizeof(orcsdr::commands::Result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_json = static_cast<char*>(heap_caps_malloc(kJsonBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_result || !g_json) {
    ESP_LOGE(TAG, "buffer allocation failed");
    stop_server();
    return false;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.core_id = 0;
  config.stack_size = 8192;
  // The page polls over several connections at once; with too few sockets the server's LRU purge closes a client's connection mid-request. 7 needs the
  // 16-socket lwip setting (sdkconfig.net-throughput): 7 here + listener and control socket + the rtl_tcp listener, client and waiting client.
  config.max_open_sockets = CONFIG_LWIP_MAX_SOCKETS >= 14 ? 7 : 4;
  config.max_uri_handlers = 6;
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
  httpd_uri_t api{};
  api.uri = "/api/v1*";
  api.method = HTTP_GET;
  api.handler = handle_api;
  httpd_uri_t root{};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = handle_root;
  httpd_uri_t post{};
  post.uri = "/api/v1*";
  post.method = HTTP_POST;
  post.handler = handle_post;
  httpd_register_uri_handler(g_server, &api);
  httpd_register_uri_handler(g_server, &post);
  httpd_register_uri_handler(g_server, &root);
#if ORCSDR_HAS_WEB_UI
  httpd_uri_t ui{};
  ui.uri = "/m5*";
  ui.method = HTTP_GET;
  ui.handler = handle_ui;
  httpd_register_uri_handler(g_server, &ui);
#endif
  std::snprintf(g_url, sizeof(g_url), "http://%s/", ip);
  g_listening = true;
  ESP_LOGI(TAG, "RTL_WEB_LISTEN port=80 url=%s web_ui=%s", g_url, ORCSDR_HAS_WEB_UI ? "included" : "not included (JSON API only)");
  return true;
}

// The address to serve on: Wi-Fi when it is connected, otherwise the wired link. Empty when neither has an address.
void current_ip(char* out, size_t cap) {
  out[0] = '\0';
  const wifi_app::Status w = wifi_app::status();
  if (w.connected && w.ip[0]) {
    std::snprintf(out, cap, "%s", w.ip);
    return;
  }
  const eth_app::Status e = eth_app::status();
  if (e.got_ip && e.ip[0]) std::snprintf(out, cap, "%s", e.ip);
}

void manager(void*) {
  int64_t link_up_since = 0;
  char serving_ip[16] = {};
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    char ip[16];
    current_ip(ip, sizeof(ip));
    if (!ip[0]) link_up_since = 0;
    else if (!link_up_since) link_up_since = esp_timer_get_time();
    const bool want = g_enabled.load() && ip[0] && esp_timer_get_time() - link_up_since > 2500000;
    // The server is bound to all interfaces, but the advertised address and the Host check follow the active link: restart if it changed.
    if (g_listening.load() && want && std::strcmp(serving_ip, ip) != 0) stop_server();
    if (want && !g_listening.load()) {
      if (start_server(ip)) std::snprintf(serving_ip, sizeof(serving_ip), "%s", ip);
    } else if (!want && g_listening.load()) {
      stop_server();
    }
  }
}
}  // namespace

bool same_site_request(httpd_req_t* req) {
  char host_header[80];
  if (!host_allowed(req, host_header, sizeof(host_header))) return false;
  const size_t len = httpd_req_get_hdr_value_len(req, "Origin");
  if (!len) return true;
  char origin[96];
  if (len >= sizeof(origin) || httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) return false;
  if (std::strncmp(origin, "http://", 7) != 0) return false;
  char host[96], want[96];
  std::snprintf(host, sizeof(host), "%s", origin + 7);
  std::snprintf(want, sizeof(want), "%s", host_header);
  if (char* c = std::strchr(host, ':')) *c = '\0';  // the page is on port 80, the socket on 81: compare the host only
  if (char* c = std::strchr(want, ':')) *c = '\0';
  return std::strcmp(host, want) == 0;
}
uint32_t peer_address(httpd_req_t* req) { return peer_address_impl(req); }

void start() {
  web_auth::init();
  orcsdr::NvsStore s;
  if (s.begin("orcsdr_web", true)) {
    g_enabled = s.get_bool("enabled", false);  // off unless the owner turned it on
    s.end();
  }
  xTaskCreatePinnedToCore(manager, "web_mgr", 4096, nullptr, 2, nullptr, 0);
}

void set_enabled(bool on) {
  g_enabled = on;
  orcsdr::NvsStore s;
  if (s.begin("orcsdr_web", false)) {
    s.put_bool("enabled", on);
    s.end();
  }
}
bool enabled() { return g_enabled.load(); }
bool listening() { return g_listening.load(); }
const char* url() { return g_url; }
}  // namespace web_server
