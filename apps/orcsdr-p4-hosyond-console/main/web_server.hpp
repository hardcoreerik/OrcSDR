#pragma once
#include <cstdint>
#include "esp_http_server.h"
#include "web_server_state.hpp"

namespace web_server {
void start();
bool set_enabled(bool on);  // false leaves the runtime state unchanged if persistence fails
Status status();            // bounded coherent snapshot safe for concurrent readers
bool same_site_request(httpd_req_t* req);
uint32_t peer_address(httpd_req_t* req);
}  // namespace web_server
