#pragma once
#include <cstdint>
#include "esp_http_server.h"

namespace web_server {
void start();
void set_enabled(bool on);
bool enabled();
bool listening();
const char* url();
bool same_site_request(httpd_req_t* req);
uint32_t peer_address(httpd_req_t* req);
}  // namespace web_server
