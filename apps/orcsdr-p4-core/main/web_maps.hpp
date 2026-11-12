#pragma once
// Basemap delivery to the OrcSDR Web page (see web_maps.cpp). Signed POST routes under /api/v1/map/.
#include "esp_http_server.h"

namespace web_maps {
bool is_route(const char* route);                     // "map/status", "map/read"
esp_err_t handle(httpd_req_t* req, const char* route);  // call only after the request's signature has been verified
}  // namespace web_maps
