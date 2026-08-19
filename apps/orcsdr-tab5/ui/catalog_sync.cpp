#include "catalog_sync.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_app_desc.h>
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>

#include <cstring>
#include <algorithm>

namespace orcsdr::catalog {
namespace {

constexpr char kCatalogUrl[] =
    "https://github.com/hardcoreerik/OrcSDR/releases/download/data-catalog-v1/catalog-v1.json";
constexpr char kSignatureUrl[] =
    "https://github.com/hardcoreerik/OrcSDR/releases/download/data-catalog-v1/catalog-v1.sig";
constexpr char kDataRoot[] = "/orcsdr/data";
constexpr size_t kManifestLimit = 16 * 1024;
constexpr size_t kSignatureLimit = 512;
constexpr size_t kChunkBytes = 4096;

extern const uint8_t catalog_public_key_pem_start[] asm("_binary_catalog_public_key_pem_start");
extern const uint8_t catalog_public_key_pem_end[] asm("_binary_catalog_public_key_pem_end");

struct Artifact {
  char url[224]{};
  char sha256[65]{};
  char destination[96]{};
  uint32_t bytes = 0;
  bool archive = false;
};

struct Pack {
  Artifact runtime;
  Artifact archive;
  char version[16]{};
  bool available = false;
};

EXT_RAM_BSS_ATTR State g_state;
EXT_RAM_BSS_ATTR Pack g_packs[kPackCount];
fs::FS* g_fs = nullptr;
uint64_t g_free_bytes = 0;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_worker = nullptr;
Operation g_requested = Operation::none;
uint8_t g_requested_pack = 0;

constexpr const char* kIds[kPackCount] = {
    "faa_aircraft", "faa_aviation", "noaa_weather", "fcc_broadcast", "lane_county_map"};
constexpr const char* kTitles[kPackCount] = {
    "FAA AIRCRAFT", "FAA AVIATION", "NOAA WEATHER", "FCC FM / AM", "LANE COUNTY MAP"};

int pack_index(const char* id) {
  if (id == nullptr) return -1;
  for (uint8_t index = 0; index < kPackCount; ++index)
    if (strcmp(id, kIds[index]) == 0) return index;
  return -1;
}

bool safe_text(const cJSON* item, char* output, size_t output_size) {
  if (!cJSON_IsString(item) || item->valuestring == nullptr) return false;
  strlcpy(output, item->valuestring, output_size);
  return output[0] != '\0';
}

bool valid_hash(const char* value) {
  if (strlen(value) != 64) return false;
  for (size_t i = 0; i < 64; ++i)
    if (!((value[i] >= '0' && value[i] <= '9') ||
          (value[i] >= 'a' && value[i] <= 'f') ||
          (value[i] >= 'A' && value[i] <= 'F'))) return false;
  return true;
}

bool valid_destination(const char* value) {
  return strncmp(value, kDataRoot, strlen(kDataRoot)) == 0 &&
         strstr(value, "..") == nullptr && strlen(value) < 95;
}

struct FirmwareVersion {
  uint16_t major = 0;
  uint16_t minor = 0;
  uint16_t patch = 0;
  int16_t alpha = -1;  // A final release sorts after its alpha builds.
};

bool parse_firmware_version(const char* text, FirmwareVersion* output) {
  if (text == nullptr || output == nullptr) return false;
  const char* value = *text == 'v' ? text + 1 : text;
  unsigned major = 0, minor = 0, patch = 0, alpha = 0;
  if (sscanf(value, "%u.%u.%u-alpha.%u", &major, &minor, &patch, &alpha) == 4) {
    if (major > UINT16_MAX || minor > UINT16_MAX || patch > UINT16_MAX || alpha > INT16_MAX) return false;
    *output = {static_cast<uint16_t>(major), static_cast<uint16_t>(minor),
               static_cast<uint16_t>(patch), static_cast<int16_t>(alpha)};
    return true;
  }
  if (sscanf(value, "%u.%u.%u", &major, &minor, &patch) != 3 ||
      major > UINT16_MAX || minor > UINT16_MAX || patch > UINT16_MAX) return false;
  *output = {static_cast<uint16_t>(major), static_cast<uint16_t>(minor),
             static_cast<uint16_t>(patch), -1};
  return true;
}

bool firmware_supports(const char* minimum) {
  FirmwareVersion required{}, current{};
  const esp_app_desc_t* app = esp_app_get_description();
  if (app == nullptr || !parse_firmware_version(minimum, &required) ||
      !parse_firmware_version(app->version, &current)) return false;
  if (current.major != required.major) return current.major > required.major;
  if (current.minor != required.minor) return current.minor > required.minor;
  if (current.patch != required.patch) return current.patch > required.patch;
  if (current.alpha < 0) return true;
  return required.alpha >= 0 && current.alpha >= required.alpha;
}

void set_message(const char* value) {
  portENTER_CRITICAL(&g_lock);
  strlcpy(g_state.message, value, sizeof(g_state.message));
  portEXIT_CRITICAL(&g_lock);
}

void set_busy(Operation operation, uint8_t progress) {
  portENTER_CRITICAL(&g_lock);
  g_state.busy = operation != Operation::none;
  g_state.operation = operation;
  g_state.progress_percent = progress;
  portEXIT_CRITICAL(&g_lock);
}

void refresh_installed() {
  if (g_fs == nullptr) return;
  portENTER_CRITICAL(&g_lock);
  for (uint8_t i = 0; i < kPackCount; ++i) {
    auto& view = g_state.packs[i];
    const auto& pack = g_packs[i];
    view.installed = pack.available && pack.runtime.destination[0] &&
                     g_fs->exists(pack.runtime.destination);
    view.update_available = false;
    if (view.installed) {
      char version_path[112]{}, local[sizeof(pack.version)]{};
      snprintf(version_path, sizeof(version_path), "%s.ver", pack.runtime.destination);
      File version = g_fs->open(version_path, FILE_READ);
      if (version) {
        const size_t used = version.readBytesUntil('\n', local, sizeof(local) - 1);
        local[used] = '\0';
        version.close();
        view.update_available = strcmp(local, pack.version) != 0;
      } else {
        view.update_available = true;
      }
    }
    if (view.installed && !view.update_available) strlcpy(view.status, "INSTALLED", sizeof(view.status));
    if (!view.installed && pack.available) strlcpy(view.status, "AVAILABLE", sizeof(view.status));
  }
  portEXIT_CRITICAL(&g_lock);
}

bool http_read_all(const char* url, uint8_t* output, size_t capacity, size_t* received) {
  if (!url || !output || !received) return false;
  esp_http_client_config_t config{};
  config.url = url;
  config.timeout_ms = 15000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = true;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;
  bool ok = esp_http_client_open(client, 0) == ESP_OK;
  const int64_t declared = ok ? esp_http_client_fetch_headers(client) : -1;
  if (!ok || declared < 0 || static_cast<uint64_t>(declared) > capacity) {
    esp_http_client_cleanup(client);
    return false;
  }
  size_t total = 0;
  while (total < capacity) {
    const int got = esp_http_client_read(client, reinterpret_cast<char*>(output + total),
                                         capacity - total);
    if (got < 0) { ok = false; break; }
    if (got == 0) break;
    total += static_cast<size_t>(got);
  }
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  *received = total;
  return ok && status == 200 && total > 0 && (declared == 0 || total == static_cast<size_t>(declared));
}

bool verify_signature(const uint8_t* manifest, size_t manifest_size,
                      const uint8_t* signature_text, size_t signature_size) {
  uint8_t signature[kSignatureLimit]{};
  size_t signature_bytes = 0;
  if (mbedtls_base64_decode(signature, sizeof(signature), &signature_bytes,
                            signature_text, signature_size) != 0 || signature_bytes == 0) return false;
  uint8_t digest[32]{};
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  const bool digest_ok = mbedtls_sha256_starts(&sha, 0) == 0 &&
                         mbedtls_sha256_update(&sha, manifest, manifest_size) == 0 &&
                         mbedtls_sha256_finish(&sha, digest) == 0;
  mbedtls_sha256_free(&sha);
  if (!digest_ok) return false;
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  const int parse = mbedtls_pk_parse_public_key(
      &key, catalog_public_key_pem_start,
      static_cast<size_t>(catalog_public_key_pem_end - catalog_public_key_pem_start));
  const int verify = parse == 0 ? mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest,
                                                     sizeof(digest), signature, signature_bytes) : -1;
  mbedtls_pk_free(&key);
  return verify == 0;
}

bool parse_artifact(const cJSON* object, bool archive, Artifact* output) {
  if (!cJSON_IsObject(object) || output == nullptr) return false;
  Artifact artifact{};
  if (!safe_text(cJSON_GetObjectItemCaseSensitive(object, "url"), artifact.url, sizeof(artifact.url)) ||
      !safe_text(cJSON_GetObjectItemCaseSensitive(object, "sha256"), artifact.sha256, sizeof(artifact.sha256)) ||
      !safe_text(cJSON_GetObjectItemCaseSensitive(object, "destination"), artifact.destination, sizeof(artifact.destination))) return false;
  const cJSON* bytes = cJSON_GetObjectItemCaseSensitive(object, "bytes");
  if (!cJSON_IsNumber(bytes) || bytes->valuedouble <= 0 || bytes->valuedouble > UINT32_MAX ||
      strncmp(artifact.url, "https://", 8) != 0 || !valid_hash(artifact.sha256) ||
      !valid_destination(artifact.destination)) return false;
  artifact.bytes = static_cast<uint32_t>(bytes->valuedouble);
  artifact.archive = archive;
  *output = artifact;
  return true;
}

bool parse_manifest(const uint8_t* data, size_t size) {
  cJSON* root = cJSON_ParseWithLength(reinterpret_cast<const char*>(data), size);
  if (!root) return false;
  bool ok = false;
  do {
    const cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const cJSON* date = cJSON_GetObjectItemCaseSensitive(root, "generated_at");
    const cJSON* minimum = cJSON_GetObjectItemCaseSensitive(root, "minimum_firmware");
    const cJSON* packs = cJSON_GetObjectItemCaseSensitive(root, "packs");
    char catalog_date[sizeof(g_state.catalog_date)]{};
    const int pack_count = cJSON_IsArray(packs) ? cJSON_GetArraySize(packs) : 0;
    if (!cJSON_IsString(schema) || strcmp(schema->valuestring, "catalog-v1") != 0 ||
        !safe_text(date, catalog_date, sizeof(catalog_date)) || !cJSON_IsString(minimum) ||
        !firmware_supports(minimum->valuestring) || pack_count < 1 ||
        pack_count > kPackCount) break;
    Pack parsed[kPackCount]{};
    PackView views[kPackCount]{};
    bool seen[kPackCount]{};
    for (uint8_t index = 0; index < kPackCount; ++index) {
      strlcpy(views[index].id, kIds[index], sizeof(views[index].id));
      strlcpy(views[index].title, kTitles[index], sizeof(views[index].title));
      strlcpy(views[index].status, "NOT PUBLISHED", sizeof(views[index].status));
    }
    for (int node_index = 0; node_index < pack_count; ++node_index) {
      const cJSON* node = cJSON_GetArrayItem(packs, node_index);
      if (!cJSON_IsObject(node)) break;
      const cJSON* id = cJSON_GetObjectItemCaseSensitive(node, "id");
      const cJSON* artifacts = cJSON_GetObjectItemCaseSensitive(node, "artifacts");
      const int index = cJSON_IsString(id) ? pack_index(id->valuestring) : -1;
      if (index < 0 || seen[index] || !cJSON_IsObject(artifacts)) break;
      const cJSON* runtime = cJSON_GetObjectItemCaseSensitive(artifacts, "runtime");
      const cJSON* archive = cJSON_GetObjectItemCaseSensitive(artifacts, "archive");
      if (!parse_artifact(runtime, false, &parsed[index].runtime) ||
          !parse_artifact(archive, true, &parsed[index].archive) ||
          !safe_text(cJSON_GetObjectItemCaseSensitive(node, "version"), views[index].version,
                     sizeof(views[index].version)) ||
          !safe_text(cJSON_GetObjectItemCaseSensitive(node, "source_date"), views[index].source_date,
                     sizeof(views[index].source_date))) break;
      parsed[index].available = true;
      strlcpy(parsed[index].version, views[index].version, sizeof(parsed[index].version));
      seen[index] = true;
      views[index].runtime_bytes = parsed[index].runtime.bytes;
      views[index].archive_bytes = parsed[index].archive.bytes;
      strlcpy(views[index].status, "AVAILABLE", sizeof(views[index].status));
      if (node_index + 1 == pack_count) ok = true;
    }
    if (ok) {
      portENTER_CRITICAL(&g_lock);
      memcpy(g_packs, parsed, sizeof(g_packs));
      memcpy(g_state.packs, views, sizeof(views));
      strlcpy(g_state.catalog_date, catalog_date, sizeof(g_state.catalog_date));
      g_state.ready = true;
      portEXIT_CRITICAL(&g_lock);
    }
  } while (false);
  cJSON_Delete(root);
  return ok;
}

bool hash_matches(const uint8_t digest[32], const char* expected) {
  char actual[65]{};
  for (uint8_t i = 0; i < 32; ++i) snprintf(actual + i * 2, 3, "%02x", digest[i]);
  return strcasecmp(actual, expected) == 0;
}

bool starts_with(File& file, const char* expected, size_t expected_size) {
  uint8_t actual[16]{};
  return expected_size <= sizeof(actual) && file.seek(0) &&
         file.read(actual, expected_size) == expected_size &&
         memcmp(actual, expected, expected_size) == 0;
}

bool validate_staged_artifact(const Artifact& artifact, uint8_t pack_index) {
  char temporary[112]{};
  snprintf(temporary, sizeof(temporary), "%s.part", artifact.destination);
  File file = g_fs ? g_fs->open(temporary, FILE_READ) : File{};
  if (!file || file.size() != artifact.bytes) return false;
  bool valid = false;
  if (artifact.archive) {
    // Every published source artifact is a ZIP. Checking the local header is
    // intentionally bounded; SHA-256 already proves the downloaded bytes.
    valid = starts_with(file, "PK\003\004", 4) || starts_with(file, "PK\005\006", 4);
  } else if (pack_index == 0) {
    valid = starts_with(file, "ORCADSB1", 8);
  } else if (pack_index == 4) {
    valid = starts_with(file, "ORCMAP1", 7);
  } else {
    // Non-ADS-B indexes use the common line-oriented catalog header.
    valid = starts_with(file, "ORCCAT1\n", 8);
  }
  file.close();
  return valid;
}

bool download_artifact(const Artifact& artifact, uint8_t pack_index,
                       uint8_t progress_base, uint8_t progress_span) {
  if (g_fs == nullptr || artifact.url[0] == '\0') return false;
  if (g_free_bytes < artifact.bytes + kChunkBytes) { set_message("Not enough SD space"); return false; }
  char temporary[112]{};
  snprintf(temporary, sizeof(temporary), "%s.part", artifact.destination);
  g_fs->remove(temporary);
  File file = g_fs->open(temporary, FILE_WRITE, true);
  if (!file) { set_message("Cannot create SD staging file"); return false; }

  esp_http_client_config_t config{};
  config.url = artifact.url;
  config.timeout_ms = 20000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  bool ok = client && esp_http_client_open(client, 0) == ESP_OK;
  const int64_t declared = ok ? esp_http_client_fetch_headers(client) : -1;
  if (!ok || declared != artifact.bytes || esp_http_client_get_status_code(client) != 200) ok = false;
  uint8_t* chunk = static_cast<uint8_t*>(heap_caps_malloc(kChunkBytes, MALLOC_CAP_8BIT));
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (ok && (!chunk || mbedtls_sha256_starts(&sha, 0) != 0)) ok = false;
  uint32_t total = 0;
  while (ok && total < artifact.bytes) {
    const int got = esp_http_client_read(client, reinterpret_cast<char*>(chunk),
                                         std::min<uint32_t>(kChunkBytes, artifact.bytes - total));
    if (got <= 0 || file.write(chunk, static_cast<size_t>(got)) != static_cast<size_t>(got) ||
        mbedtls_sha256_update(&sha, chunk, static_cast<size_t>(got)) != 0) { ok = false; break; }
    total += static_cast<uint32_t>(got);
    set_busy(g_requested, progress_base + static_cast<uint8_t>((static_cast<uint64_t>(total) * progress_span) / artifact.bytes));
  }
  uint8_t digest[32]{};
  if (ok && (total != artifact.bytes || mbedtls_sha256_finish(&sha, digest) != 0 ||
             !hash_matches(digest, artifact.sha256))) ok = false;
  mbedtls_sha256_free(&sha);
  if (chunk) heap_caps_free(chunk);
  if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
  file.close();
  if (!ok) { g_fs->remove(temporary); set_message("Download hash or network failure"); return false; }

  if (!validate_staged_artifact(artifact, pack_index)) {
    g_fs->remove(temporary); set_message("Downloaded file schema rejected"); return false;
  }
  return true;
}

bool move_active_to_backup(const Artifact& artifact) {
  char backup[112]{};
  snprintf(backup, sizeof(backup), "%s.bak", artifact.destination);
  g_fs->remove(backup);
  if (g_fs->exists(artifact.destination) && !g_fs->rename(artifact.destination, backup)) {
    set_message("Could not preserve previous pack"); return false;
  }
  return true;
}

void rollback_artifact(const Artifact& artifact) {
  char temporary[112]{}, backup[112]{};
  snprintf(temporary, sizeof(temporary), "%s.part", artifact.destination);
  snprintf(backup, sizeof(backup), "%s.bak", artifact.destination);
  if (g_fs->exists(artifact.destination)) (void)g_fs->remove(artifact.destination);
  if (g_fs->exists(backup)) (void)g_fs->rename(backup, artifact.destination);
  if (g_fs->exists(temporary)) (void)g_fs->remove(temporary);
}

void discard_staged(const Artifact& artifact) {
  char temporary[112]{};
  snprintf(temporary, sizeof(temporary), "%s.part", artifact.destination);
  if (g_fs->exists(temporary)) (void)g_fs->remove(temporary);
}

bool activate_pack(const Pack& pack) {
  if (!move_active_to_backup(pack.runtime)) return false;
  if (!move_active_to_backup(pack.archive)) {
    rollback_artifact(pack.runtime);
    return false;
  }
  char runtime_part[112]{}, archive_part[112]{};
  snprintf(runtime_part, sizeof(runtime_part), "%s.part", pack.runtime.destination);
  snprintf(archive_part, sizeof(archive_part), "%s.part", pack.archive.destination);
  if (!g_fs->rename(runtime_part, pack.runtime.destination) ||
      !g_fs->rename(archive_part, pack.archive.destination)) {
    rollback_artifact(pack.runtime);
    rollback_artifact(pack.archive);
    set_message("Could not activate downloaded pack");
    return false;
  }
  char version_path[112]{};
  snprintf(version_path, sizeof(version_path), "%s.ver", pack.runtime.destination);
  File version = g_fs->open(version_path, FILE_WRITE, true);
  if (!version || version.print(pack.version) != strlen(pack.version)) {
    if (version) version.close();
    // The verified runtime/archive pair is already active. Keep it and expose
    // an update on the next check rather than reporting a false failed install.
    return true;
  }
  version.close();
  return true;
}

void worker(void*) {
  // Catalogs are manual operations. Keep their bounded transfer buffers out
  // of the small FreeRTOS task stack and release them after each request.
  auto* manifest = static_cast<uint8_t*>(
      heap_caps_malloc(kManifestLimit, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* signature = static_cast<uint8_t*>(
      heap_caps_malloc(kSignatureLimit, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  size_t manifest_size = 0, signature_size = 0;
  const Operation operation = g_requested;
  const uint8_t pack_index = g_requested_pack;
  bool ok = false;
  if (!manifest || !signature) {
    set_message("Catalog buffer allocation failed");
  } else if (operation == Operation::check) {
    set_message("Checking signed data catalog");
    ok = http_read_all(kCatalogUrl, manifest, kManifestLimit, &manifest_size) &&
         http_read_all(kSignatureUrl, signature, kSignatureLimit, &signature_size) &&
         verify_signature(manifest, manifest_size, signature, signature_size) &&
         parse_manifest(manifest, manifest_size);
    set_message(ok ? "Catalog verified" : "Catalog signature or format rejected");
    if (ok) refresh_installed();
  } else if (operation == Operation::install && pack_index < kPackCount && g_packs[pack_index].available) {
    const auto& pack = g_packs[pack_index];
    const uint64_t required = static_cast<uint64_t>(pack.runtime.bytes) + pack.archive.bytes + 2 * kChunkBytes;
    if (g_free_bytes < required) {
      set_message("Not enough SD space for pack");
    } else {
      set_message("Downloading runtime index");
      ok = download_artifact(pack.runtime, pack_index, 0, 45);
      if (ok) { set_message("Downloading source archive"); ok = download_artifact(pack.archive, pack_index, 45, 55); }
      if (ok) ok = activate_pack(pack);
      if (!ok) {
        discard_staged(pack.runtime);
        discard_staged(pack.archive);
      }
    }
    set_message(ok ? "Pack installed and verified" : g_state.message);
    if (ok) refresh_installed();
  } else if (operation == Operation::remove && pack_index < kPackCount && g_packs[pack_index].available) {
    const auto& pack = g_packs[pack_index];
    ok = true;
    if (g_fs && g_fs->exists(pack.runtime.destination)) ok &= g_fs->remove(pack.runtime.destination);
    if (g_fs && g_fs->exists(pack.archive.destination)) ok &= g_fs->remove(pack.archive.destination);
    set_message(ok ? "Pack removed; configuration preserved" : "Could not remove pack");
    refresh_installed();
  }
  if (manifest) heap_caps_free(manifest);
  if (signature) heap_caps_free(signature);
  set_busy(Operation::none, ok ? 100 : 0);
  g_requested = Operation::none;
  g_worker = nullptr;
  vTaskDelete(nullptr);
}

bool request(Operation operation, uint8_t pack_index, bool needs_wifi) {
  if (g_fs == nullptr || g_worker != nullptr || (needs_wifi && WiFi.status() != WL_CONNECTED)) return false;
  if (!g_fs->exists(kDataRoot) && !g_fs->mkdir(kDataRoot)) return false;
  g_requested = operation;
  g_requested_pack = pack_index;
  set_busy(operation, 0);
  set_message(operation == Operation::check ? "Catalog check queued" : "Data operation queued");
  return xTaskCreatePinnedToCore(worker, "catalog_sync", 12288, nullptr, 3, &g_worker, 1) == pdPASS;
}

}  // namespace

void begin(fs::FS* filesystem, uint64_t free_bytes) {
  g_fs = filesystem;
  g_free_bytes = free_bytes;
  portENTER_CRITICAL(&g_lock);
  g_state = {};
  for (uint8_t i = 0; i < kPackCount; ++i) {
    strlcpy(g_state.packs[i].id, kIds[i], sizeof(g_state.packs[i].id));
    strlcpy(g_state.packs[i].title, kTitles[i], sizeof(g_state.packs[i].title));
    strlcpy(g_state.packs[i].status, "CHECK CATALOG", sizeof(g_state.packs[i].status));
  }
  portEXIT_CRITICAL(&g_lock);
}

void poll(bool) { if (g_fs != nullptr && g_state.ready) refresh_installed(); }
bool request_check(bool wifi_connected) { return request(Operation::check, 0, wifi_connected); }
bool request_install(uint8_t pack_index, bool wifi_connected) { return request(Operation::install, pack_index, wifi_connected); }
bool request_remove(uint8_t pack_index) { return request(Operation::remove, pack_index, false); }

State state() {
  State copy{};
  portENTER_CRITICAL(&g_lock);
  copy = g_state;
  portEXIT_CRITICAL(&g_lock);
  return copy;
}

}  // namespace orcsdr::catalog
