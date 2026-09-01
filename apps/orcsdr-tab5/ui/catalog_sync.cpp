#include "catalog_sync.hpp"

#include "wifi_service.hpp"
#include <esp_crt_bundle.h>
#include <esp_app_desc.h>
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>

#include <cstring>
#include <algorithm>

namespace orcsdr::catalog {
namespace {

constexpr char kTag[] = "catalog_sync";

constexpr char kCatalogUrl[] =
    "https://github.com/hardcoreerik/OrcSDR/releases/download/data-catalog-v1/catalog-v1.json";
constexpr char kSignatureUrl[] =
    "https://github.com/hardcoreerik/OrcSDR/releases/download/data-catalog-v1/catalog-v1.sig";
constexpr char kDataRoot[] = "/orcsdr/data";
constexpr size_t kManifestLimit = 16 * 1024;
constexpr size_t kSignatureLimit = 512;
constexpr size_t kChunkBytes = 4096;
constexpr size_t kWriteBatchBytes = kChunkBytes;
constexpr size_t kYieldBytes = 64 * 1024;
constexpr uint8_t kMaxRedirects = 4;
constexpr char kUserAgent[] = "OrcSDR/0.2 (+https://github.com/hardcoreerik/OrcSDR)";

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
orcsdr::storage::FileSystem* g_fs = nullptr;
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
  Pack packs[kPackCount]{};
  bool installed[kPackCount]{};
  bool update_available[kPackCount]{};
  portENTER_CRITICAL(&g_lock);
  memcpy(packs, g_packs, sizeof(packs));
  portEXIT_CRITICAL(&g_lock);
  for (uint8_t i = 0; i < kPackCount; ++i) {
    const auto& pack = packs[i];
    installed[i] = pack.available && pack.runtime.destination[0] &&
                   g_fs->exists(pack.runtime.destination);
    if (installed[i]) {
      char version_path[112]{}, local[sizeof(pack.version)]{};
      snprintf(version_path, sizeof(version_path), "%s.ver", pack.runtime.destination);
      File version = g_fs->open(version_path, FILE_READ);
      if (version) {
        const size_t used = version.readBytesUntil('\n', local, sizeof(local) - 1);
        local[used] = '\0';
        version.close();
        update_available[i] = strcmp(local, pack.version) != 0;
      } else {
        update_available[i] = true;
      }
    }
  }
  portENTER_CRITICAL(&g_lock);
  for (uint8_t i = 0; i < kPackCount; ++i) {
    auto& view = g_state.packs[i];
    view.installed = installed[i];
    view.update_available = update_available[i];
    if (view.installed && !view.update_available) strlcpy(view.status, "INSTALLED", sizeof(view.status));
    if (!view.installed && packs[i].available) strlcpy(view.status, "AVAILABLE", sizeof(view.status));
  }
  portEXIT_CRITICAL(&g_lock);
}

bool open_get(esp_http_client_handle_t client, int64_t* declared, int* status,
              esp_err_t* open_result) {
  if (client == nullptr || declared == nullptr || status == nullptr || open_result == nullptr) return false;
  for (uint8_t redirect = 0; redirect <= kMaxRedirects; ++redirect) {
    *open_result = esp_http_client_open(client, 0);
    if (*open_result != ESP_OK) return false;
    *declared = esp_http_client_fetch_headers(client);
    *status = esp_http_client_get_status_code(client);
    if (*status < 300 || *status > 308) return true;
    if (redirect == kMaxRedirects || esp_http_client_set_redirection(client) != ESP_OK) return false;
    esp_http_client_close(client);
  }
  return false;
}

bool http_read_all(const char* url, uint8_t* output, size_t capacity, size_t* received) {
  if (!url || !output || !received) return false;
  esp_http_client_config_t config{};
  config.url = url;
  config.timeout_ms = 15000;
  config.buffer_size_tx = 2048;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = true;
  config.user_agent = kUserAgent;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;
  esp_err_t open_result = ESP_FAIL;
  int64_t declared = -1;
  int status = -1;
  bool ok = open_get(client, &declared, &status, &open_result);
  if (!ok || (declared > 0 && static_cast<uint64_t>(declared) > capacity)) {
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
  const bool complete = esp_http_client_is_complete_data_received(client);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  *received = total;
  ESP_LOGI(kTag, "fetch open=%s status=%d declared=%lld received=%u",
           esp_err_to_name(open_result), status, static_cast<long long>(declared),
           static_cast<unsigned>(total));
  return ok && complete && status == 200 && total > 0 &&
         (declared <= 0 || total == static_cast<size_t>(declared));
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

bool download_artifact(const Artifact& artifact, uint8_t pack_index,
                       uint8_t progress_base, uint8_t progress_span) {
  if (g_fs == nullptr || artifact.url[0] == '\0') return false;
  if (g_free_bytes < artifact.bytes + kChunkBytes) { set_message("Not enough SD space"); return false; }
  ESP_LOGI(kTag, "download stage=start pack=%u kind=%s bytes=%u", pack_index,
           artifact.archive ? "archive" : "runtime", static_cast<unsigned>(artifact.bytes));
  char temporary[112]{};
  snprintf(temporary, sizeof(temporary), "%s.part", artifact.destination);
  g_fs->remove(temporary);
  File file = g_fs->open(temporary, FILE_WRITE, true);
  if (!file) { set_message("Cannot create SD staging file"); return false; }
  ESP_LOGI(kTag, "download stage=staging_open");

  esp_http_client_config_t config{};
  config.url = artifact.url;
  config.timeout_ms = 20000;
  config.buffer_size_tx = 2048;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.user_agent = kUserAgent;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_err_t open_result = ESP_FAIL;
  int64_t declared = -1;
  int status = -1;
  bool ok = client && open_get(client, &declared, &status, &open_result);
  if (!ok || (declared > 0 && declared != artifact.bytes) || status != 200) ok = false;
  ESP_LOGI(kTag, "download stage=http_open ok=%d status=%d declared=%lld", ok ? 1 : 0,
           status, static_cast<long long>(declared));
  uint8_t* write_batch = static_cast<uint8_t*>(heap_caps_malloc(kWriteBatchBytes, MALLOC_CAP_SPIRAM));
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (ok && (!write_batch || mbedtls_sha256_starts(&sha, 0) != 0)) ok = false;
  uint32_t total = 0;
  uint32_t next_yield = kYieldBytes;
  size_t batch_used = 0;
  uint8_t header[8]{};
  size_t header_size = 0;
  while (ok && total < artifact.bytes) {
    const size_t room = kWriteBatchBytes - batch_used;
    uint8_t* const target = write_batch + batch_used;
    const int got = esp_http_client_read(
        client, reinterpret_cast<char*>(target),
        std::min<size_t>(room, static_cast<size_t>(artifact.bytes - total)));
    if (got <= 0) { ESP_LOGE(kTag, "download stage=http_read_failed got=%d", got); ok = false; break; }
    batch_used += static_cast<size_t>(got);
    const size_t header_take = std::min(sizeof(header) - header_size, static_cast<size_t>(got));
    if (header_take) {
      memcpy(header + header_size, target, header_take);
      header_size += header_take;
    }
    if (mbedtls_sha256_update(&sha, target, static_cast<size_t>(got)) != 0) { ESP_LOGE(kTag, "download stage=hash_failed"); ok = false; break; }
    total += static_cast<uint32_t>(got);
    if (batch_used == kWriteBatchBytes || total == artifact.bytes) {
      const size_t wrote = file.write(write_batch, batch_used);
      if (wrote != batch_used) { ESP_LOGE(kTag, "download stage=sd_write_failed wrote=%u expected=%u", static_cast<unsigned>(wrote), static_cast<unsigned>(batch_used)); ok = false; break; }
      batch_used = 0;
    }
    if (total == static_cast<uint32_t>(got) || total == artifact.bytes) {
      ESP_LOGI(kTag, "download stage=sd_write_progress bytes=%u", static_cast<unsigned>(total));
    }
    set_busy(g_requested, progress_base + static_cast<uint8_t>((static_cast<uint64_t>(total) * progress_span) / artifact.bytes));
    if (total >= next_yield) {
      vTaskDelay(1);
      next_yield += kYieldBytes;
    }
  }
  uint8_t digest[32]{};
  if (ok && (total != artifact.bytes || mbedtls_sha256_finish(&sha, digest) != 0 ||
             !hash_matches(digest, artifact.sha256))) ok = false;
  mbedtls_sha256_free(&sha);
  if (write_batch) heap_caps_free(write_batch);
  if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
  file.close();
  if (!ok) { g_fs->remove(temporary); set_message("Download hash or network failure"); return false; }

  const bool schema_ok = artifact.archive
      ? (memcmp(header, "PK\003\004", 4) == 0 || memcmp(header, "PK\005\006", 4) == 0)
      : pack_index == 0 ? memcmp(header, "ORCADSB1", 8) == 0
      : pack_index == 4 ? memcmp(header, "ORCMAP1\n", 8) == 0
                        : memcmp(header, "ORCCAT1\n", 8) == 0;
  if (!schema_ok) { g_fs->remove(temporary); set_message("Downloaded file schema rejected"); return false; }
  ESP_LOGI(kTag, "download stage=validated");
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
    bool manifest_ok = false;
    bool signature_ok = false;
    bool signature_verified = false;
    if (!http_read_all(kCatalogUrl, manifest, kManifestLimit, &manifest_size)) {
      set_message("Could not fetch catalog manifest");
    } else {
      manifest_ok = true;
      ESP_LOGI(kTag, "check stage=manifest_ok bytes=%u", static_cast<unsigned>(manifest_size));
    }
    if (manifest_ok) {
      if (!http_read_all(kSignatureUrl, signature, kSignatureLimit, &signature_size)) {
        set_message("Could not fetch catalog signature");
      } else {
        signature_ok = true;
        ESP_LOGI(kTag, "check stage=signature_ok bytes=%u", static_cast<unsigned>(signature_size));
      }
    }
    if (signature_ok) {
      signature_verified = verify_signature(manifest, manifest_size, signature, signature_size);
      if (!signature_verified) set_message("Catalog signature rejected");
      else ESP_LOGI(kTag, "check stage=signature_verified");
    }
    if (signature_verified) {
      const bool manifest_parsed = parse_manifest(manifest, manifest_size);
      ESP_LOGI(kTag, "check stage=manifest_parsed ok=%d", manifest_parsed ? 1 : 0);
      if (!manifest_parsed) {
        set_message("Catalog manifest rejected");
      } else {
        ok = true;
        set_message("Catalog verified");
      }
    }
    if (ok) {
      refresh_installed();
      ESP_LOGI(kTag, "check stage=installed_refreshed");
    }
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
    if (ok) {
      portENTER_CRITICAL(&g_lock);
      auto& view = g_state.packs[pack_index];
      view.installed = true;
      view.update_available = false;
      strlcpy(view.status, "INSTALLED", sizeof(view.status));
      portEXIT_CRITICAL(&g_lock);
    }
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
  if (g_fs == nullptr) {
    set_message("SD storage unavailable");
    return false;
  }
  if (g_worker != nullptr) {
    set_message("Catalog operation already running");
    return false;
  }
  if (needs_wifi && !orcsdr::wifi::connected()) {
    set_message("Connect Wi-Fi before downloading");
    return false;
  }
  if ((!g_fs->exists("/orcsdr") && !g_fs->mkdir("/orcsdr")) ||
      (!g_fs->exists(kDataRoot) && !g_fs->mkdir(kDataRoot))) {
    set_message("Could not create SD data directory");
    return false;
  }
  g_requested = operation;
  g_requested_pack = pack_index;
  set_busy(operation, 0);
  set_message(operation == Operation::check ? "Catalog check queued" : "Data operation queued");
  if (xTaskCreatePinnedToCoreWithCaps(worker, "catalog_sync", 12288, nullptr, 3,
                                      &g_worker, 1,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    g_requested = Operation::none;
    set_busy(Operation::none, 0);
    set_message("Catalog worker creation failed");
    return false;
  }
  return true;
}

}  // namespace

void begin(orcsdr::storage::FileSystem* filesystem, uint64_t free_bytes) {
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

void poll(bool) {
  // SD checks belong to the catalog worker. Calling refresh_installed() from
  // the UI loop races the worker's File operations as soon as a manifest is
  // accepted, which can reset the shared SDMMC host.
}
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
