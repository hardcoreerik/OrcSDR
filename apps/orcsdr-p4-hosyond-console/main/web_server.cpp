#include "web_server.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include "commands.hpp"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_store.hpp"
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
  return (w.ip[0] && std::strcmp(host, w.ip) == 0) || strcasecmp(host, "orcsdr.local") == 0;
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

// ---- server lifecycle --------------------------------------------------------------------------------------------

void stop_server() {
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
  config.max_open_sockets = 4;
  config.max_uri_handlers = 4;
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
  httpd_register_uri_handler(g_server, &api);
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

void manager(void*) {
  int64_t wifi_up_since = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    const wifi_app::Status w = wifi_app::status();
    const bool link = w.connected && w.ip[0];
    if (!link) wifi_up_since = 0;
    else if (!wifi_up_since) wifi_up_since = esp_timer_get_time();
    const bool want = g_enabled.load() && link && esp_timer_get_time() - wifi_up_since > 2500000;
    if (want && !g_listening.load()) start_server(w.ip);
    else if (!want && g_listening.load()) stop_server();
  }
}
}  // namespace

void start() {
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
