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
#include "wifi_app.hpp"

namespace web_server {
namespace {
constexpr char TAG[] = "WEB";
constexpr size_t kJsonBytes = 2560;

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
  size_t n = static_cast<size_t>(std::snprintf(g_json, kJsonBytes, "{\"ok\":true,\"api\":\"/api/v1\",\"read_only\":true,\"commands\":["));
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

esp_err_t handle_root(httpd_req_t* req) {
  char host_header[80];
  if (!host_allowed(req, host_header, sizeof(host_header))) return reject(req, "403 Forbidden", "host_rejected");
  if (!origin_allowed(req, host_header)) return reject(req, "403 Forbidden", "origin_rejected");
  return handle_index(req);
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

  const orcsdr::commands::Command* cmd = orcsdr::commands::find_path(name);
  if (!cmd) return reject(req, "404 Not Found", "unknown_route");

  orcsdr::commands::Args args;
  char query[48];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char value[8];
    if (httpd_query_key_value(query, "rx", value, sizeof(value)) == ESP_OK) {
      char* end = nullptr;
      const long rx = std::strtol(value, &end, 10);
      if (end == value || *end != '\0' || rx < 0 || rx > 3) return reject(req, "400 Bad Request", "bad_rx");
      args.rx = static_cast<int>(rx);
    }
  }

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
  std::snprintf(g_url, sizeof(g_url), "http://%s/", ip);
  g_listening = true;
  ESP_LOGI(TAG, "RTL_WEB_LISTEN port=80 url=%s", g_url);
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
