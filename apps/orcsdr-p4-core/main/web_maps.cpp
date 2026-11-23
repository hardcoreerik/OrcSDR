// OrcSDR Web basemap delivery: the page asks the board for its basemap (the PMTiles archive on the SD card) in bounded chunks.
//
//   POST /api/v1/map/status                      -> {"ok":true,"sd":bool,"present":bool,"bytes":N}
//   POST /api/v1/map/read?offset=N&len=M         -> M bytes (application/octet-stream), X-Orc-Total: file size
//
// Both are signed control-tier requests like every other POST; the signature covers the exact target, so an offset or length cannot be
// changed in flight. A read is bounded (kChunkMax) and never reaches past the end of the file: an offset at or beyond the end is a 416.
// The file is read-only here and only this one path is ever opened; the page cannot name a path.
#include "web_maps.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"
#include "orcsdr_storage.hpp"

namespace web_maps {
namespace {
constexpr const char* kArchive = "/sd/orcmaps/basemap.pmtiles";
constexpr size_t kChunkMax = 32768;

esp_err_t fail(httpd_req_t* req, const char* status, const char* error) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  char body[64];
  const int n = std::snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", error);
  return httpd_resp_send(req, body, n);
}

bool archive_size(uint64_t* size) {
  struct stat st {};
  if (!orcsdr::storage::mounted() || stat(kArchive, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) return false;
  *size = static_cast<uint64_t>(st.st_size);
  return true;
}

bool parse_u64(const char* text, uint64_t* out) {
  if (!text || !*text) return false;
  char* end = nullptr;
  const unsigned long long v = std::strtoull(text, &end, 10);
  if (end == text || *end != '\0') return false;
  *out = v;
  return true;
}
}  // namespace

bool is_route(const char* route) { return std::strncmp(route, "map/", 4) == 0; }

esp_err_t handle(httpd_req_t* req, const char* route) {
  uint64_t size = 0;
  const bool present = archive_size(&size);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");

  if (std::strcmp(route, "map/status") == 0) {
    char body[96];
    const int n = std::snprintf(body, sizeof(body), "{\"ok\":true,\"sd\":%s,\"present\":%s,\"bytes\":%llu}", orcsdr::storage::mounted() ? "true" : "false",
                                present ? "true" : "false", static_cast<unsigned long long>(present ? size : 0));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, n);
  }
  if (std::strcmp(route, "map/read") != 0) return fail(req, "404 Not Found", "unknown_route");
  if (!present) return fail(req, "404 Not Found", "no_basemap");

  char query[64];
  char value[24];
  uint64_t offset = 0, length = 0;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK || httpd_query_key_value(query, "offset", value, sizeof(value)) != ESP_OK || !parse_u64(value, &offset) ||
      httpd_query_key_value(query, "len", value, sizeof(value)) != ESP_OK || !parse_u64(value, &length) || length == 0 || length > kChunkMax)
    return fail(req, "400 Bad Request", "bad_range");
  if (offset >= size) return fail(req, "416 Range Not Satisfiable", "past_end");
  if (length > size - offset) length = size - offset;

  uint8_t* chunk = static_cast<uint8_t*>(heap_caps_malloc(kChunkMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!chunk) return fail(req, "503 Service Unavailable", "no_memory");
  size_t got = 0;
  if (FILE* f = std::fopen(kArchive, "rb")) {
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0) got = std::fread(chunk, 1, static_cast<size_t>(length), f);
    std::fclose(f);
  }
  if (got != length) {
    heap_caps_free(chunk);
    return fail(req, "500 Internal Server Error", "read_failed");
  }
  char total[24];
  std::snprintf(total, sizeof(total), "%llu", static_cast<unsigned long long>(size));
  httpd_resp_set_hdr(req, "X-Orc-Total", total);
  httpd_resp_set_type(req, "application/octet-stream");
  const esp_err_t r = httpd_resp_send(req, reinterpret_cast<const char*>(chunk), static_cast<ssize_t>(got));
  heap_caps_free(chunk);
  return r;
}
}  // namespace web_maps
