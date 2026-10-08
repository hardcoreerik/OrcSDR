#include "weather_report_store.hpp"

#include "orcsdr_storage.hpp"

#include <mbedtls/sha256.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::weather {
namespace {
constexpr char kRoot[] = "/orcsdr/weather";
constexpr char kReports[] = "/orcsdr/weather/reports";
constexpr char kHistory[] = "/orcsdr/weather/history.jsonl";
constexpr char kHistoryPart[] = "/orcsdr/weather/history.jsonl.part";
constexpr char kHistoryBackup[] = "/orcsdr/weather/history.jsonl.bak";
constexpr size_t kHistoryBufferBytes = 6144;

char* psram_buffer(size_t bytes) {
  return static_cast<char*>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void set_error(ReportSaveResult* out, const char* text) {
  if (!out) return;
  out->ok = false;
  std::snprintf(out->error, sizeof(out->error), "%s", text ? text : "error");
}

bool write_file(storage::FileSystem& fs, const char* path, const char* data) {
  auto file = fs.open(path, FILE_WRITE, true);
  if (!file) return false;
  const size_t bytes = std::strlen(data);
  const bool wrote =
      file.write(reinterpret_cast<const uint8_t*>(data), bytes) == bytes;
  const bool flushed = wrote && file.flush();
  const bool closed = file.close();
  return wrote && flushed && closed;
}

void sha256_hex(const char* data, char output[65]) {
  uint8_t digest[32]{};
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  const bool ok =
      mbedtls_sha256_starts(&sha, 0) == 0 &&
      mbedtls_sha256_update(
          &sha, reinterpret_cast<const unsigned char*>(data),
          std::strlen(data)) == 0 &&
      mbedtls_sha256_finish(&sha, digest) == 0;
  mbedtls_sha256_free(&sha);
  if (!ok) {
    output[0] = '\0';
    return;
  }
  for (size_t i = 0; i < sizeof(digest); ++i)
    std::snprintf(output + i * 2, 3, "%02x", digest[i]);
  output[64] = '\0';
}

bool ensure_dirs(storage::FileSystem& fs) {
  return fs.mkdir("/orcsdr") && fs.mkdir(kRoot) && fs.mkdir(kReports);
}

bool rewrite_history(storage::FileSystem& fs, const char* row) {
  char* content = psram_buffer(kHistoryBufferBytes);
  if (!content) return false;

  size_t used = 0;
  if (fs.exists(kHistory)) {
    auto source = fs.open(kHistory, FILE_READ);
    if (source) {
      used = std::min(source.size(), kHistoryBufferBytes - 1);
      used = source.read(content, used);
      (void)source.close();
    }
  }
  content[used] = '\0';

  const size_t row_bytes = std::strlen(row);
  while (used + row_bytes + 2 >= kHistoryBufferBytes) {
    const char* first_newline = std::strchr(content, '\n');
    if (!first_newline) {
      heap_caps_free(content);
      return false;
    }
    const size_t discard =
        static_cast<size_t>(first_newline - content + 1);
    std::memmove(content, content + discard, used - discard + 1);
    used -= discard;
  }
  std::memcpy(content + used, row, row_bytes);
  used += row_bytes;
  content[used++] = '\n';
  content[used] = '\0';

  (void)fs.remove(kHistoryPart);
  if (!write_file(fs, kHistoryPart, content)) {
    heap_caps_free(content);
    return false;
  }
  heap_caps_free(content);

  (void)fs.remove(kHistoryBackup);
  const bool had_history = fs.exists(kHistory);
  if (had_history && !fs.rename(kHistory, kHistoryBackup)) {
    (void)fs.remove(kHistoryPart);
    return false;
  }
  if (!fs.rename(kHistoryPart, kHistory)) {
    if (had_history) (void)fs.rename(kHistoryBackup, kHistory);
    (void)fs.remove(kHistoryPart);
    return false;
  }
  if (had_history) (void)fs.remove(kHistoryBackup);
  return true;
}
}  // namespace

bool save_report_bundle(storage::FileSystem& fs, const ReportSnapshot& report,
                        ReportSaveResult* result) {
  if (result) *result = {};
  if (!ensure_dirs(fs)) {
    set_error(result, "mkdir failed");
    return false;
  }

  char id[40]{};
  if (report.wallclock_valid && report.created_utc) {
    std::snprintf(id, sizeof(id), "weather-%lu",
                  static_cast<unsigned long>(report.created_utc));
  } else {
    std::snprintf(id, sizeof(id), "weather-up-%lu",
                  static_cast<unsigned long>(report.created_uptime_ms));
  }

  char directory[96]{};
  std::snprintf(directory, sizeof(directory), "%s/%s", kReports, id);
  if (!fs.mkdir(directory)) {
    set_error(result, "report mkdir failed");
    return false;
  }

  char* json = psram_buffer(2048);
  char* csv = psram_buffer(2048);
  char* html = psram_buffer(4096);
  if (!json || !csv || !html) {
    if (json) heap_caps_free(json);
    if (csv) heap_caps_free(csv);
    if (html) heap_caps_free(html);
    set_error(result, "PSRAM allocation failed");
    return false;
  }

  const bool encoded =
      encode_report_json(report, json, 2048) &&
      encode_report_csv(report, csv, 2048) &&
      encode_report_html(report, html, 4096);
  if (!encoded) {
    heap_caps_free(json);
    heap_caps_free(csv);
    heap_caps_free(html);
    set_error(result, "report encode failed");
    return false;
  }

  char json_path[128]{};
  char csv_path[128]{};
  char html_path[128]{};
  char events_path[128]{};
  char alerts_path[128]{};
  char manifest_path[128]{};
  std::snprintf(json_path, sizeof(json_path), "%s/session.json", directory);
  std::snprintf(csv_path, sizeof(csv_path), "%s/observations.csv", directory);
  std::snprintf(html_path, sizeof(html_path), "%s/report.html", directory);
  std::snprintf(events_path, sizeof(events_path), "%s/rf_events.csv", directory);
  std::snprintf(alerts_path, sizeof(alerts_path), "%s/alerts.json", directory);
  std::snprintf(manifest_path, sizeof(manifest_path),
                "%s/manifest.sha256", directory);

  char events[256]{};
  std::snprintf(
      events, sizeof(events),
      "source,frequency_hz,relative_dbfs,valid\r\nRF,%lu,%.2f,%d\r\n",
      static_cast<unsigned long>(report.noaa_frequency_hz),
      static_cast<double>(report.noaa_dbfs), report.noaa_valid ? 1 : 0);
  constexpr char alerts[] =
      "{\"alerts\":[],\"same_decoder\":"
      "\"not_implemented_in_foundation\"}\n";

  bool ok =
      write_file(fs, json_path, json) &&
      write_file(fs, csv_path, csv) &&
      write_file(fs, html_path, html) &&
      write_file(fs, events_path, events) &&
      write_file(fs, alerts_path, alerts);

  char json_hash[65]{}, csv_hash[65]{}, html_hash[65]{};
  char events_hash[65]{}, alerts_hash[65]{};
  sha256_hex(json, json_hash);
  sha256_hex(csv, csv_hash);
  sha256_hex(html, html_hash);
  sha256_hex(events, events_hash);
  sha256_hex(alerts, alerts_hash);
  char manifest[768]{};
  std::snprintf(
      manifest, sizeof(manifest),
      "%s  session.json\n%s  observations.csv\n%s  report.html\n"
      "%s  rf_events.csv\n%s  alerts.json\n",
      json_hash, csv_hash, html_hash, events_hash, alerts_hash);
  ok = ok && json_hash[0] && csv_hash[0] && html_hash[0] &&
       events_hash[0] && alerts_hash[0] &&
       write_file(fs, manifest_path, manifest);

  char history[512]{};
  std::snprintf(
      history, sizeof(history),
      "{\"id\":\"%s\",\"wallclock_valid\":%s,"
      "\"created_utc\":%lu,\"created_uptime_ms\":%lu,"
      "\"noaa_valid\":%s,\"frequency_hz\":%lu}",
      id, report.wallclock_valid ? "true" : "false",
      static_cast<unsigned long>(report.created_utc),
      static_cast<unsigned long>(report.created_uptime_ms),
      report.noaa_valid ? "true" : "false",
      static_cast<unsigned long>(report.noaa_frequency_hz));
  ok = ok && rewrite_history(fs, history);

  heap_caps_free(json);
  heap_caps_free(csv);
  heap_caps_free(html);

  if (!ok) {
    set_error(result, "write failed");
    return false;
  }
  if (result) {
    result->ok = true;
    std::snprintf(result->id, sizeof(result->id), "%s", id);
  }
  return true;
}

size_t report_history_count(storage::FileSystem& fs) {
  if (!fs.exists(kHistory)) return 0;
  auto file = fs.open(kHistory, FILE_READ);
  if (!file) return 0;
  size_t count = 0;
  char line[256]{};
  while (file.available()) {
    const size_t bytes =
        file.readBytesUntil('\n', line, sizeof(line) - 1);
    if (bytes) ++count;
  }
  (void)file.close();
  return count;
}

bool latest_report_id(storage::FileSystem& fs, char* output,
                      size_t capacity) {
  if (!output || capacity == 0 || !fs.exists(kHistory)) return false;
  output[0] = '\0';
  auto file = fs.open(kHistory, FILE_READ);
  if (!file) return false;

  char line[512]{};
  while (file.available()) {
    const size_t bytes =
        file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[bytes] = '\0';
    const char* id = std::strstr(line, "\"id\":\"");
    if (!id) continue;
    id += 6;
    const char* end = std::strchr(id, '"');
    if (!end) continue;
    const size_t length =
        std::min(static_cast<size_t>(end - id), capacity - 1);
    std::memcpy(output, id, length);
    output[length] = '\0';
  }
  (void)file.close();
  return output[0] != '\0';
}

}  // namespace orcsdr::weather
