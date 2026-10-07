#include "weather_report_store.hpp"

#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>

namespace orcsdr::weather::report_store {
namespace {

struct HashEntry {
  const char* name;
  uint8_t digest[32]{};
};

void set_error(char* error, size_t capacity, const char* value) {
  if (error && capacity) std::snprintf(error, capacity, "%s", value ? value : "weather report error");
}

void hex(const uint8_t digest[32], char output[65]) {
  for (size_t i = 0; i < 32; ++i) std::snprintf(output + i * 2, 3, "%02x", digest[i]);
  output[64] = '\0';
}

bool start_hash(mbedtls_sha256_context* sha) {
  mbedtls_sha256_init(sha);
  return mbedtls_sha256_starts(sha, 0) == 0;
}

bool write_hash(File& file, mbedtls_sha256_context* sha, const char* value, size_t length) {
  return value && file.write(reinterpret_cast<const uint8_t*>(value), length) == length &&
         mbedtls_sha256_update(sha, reinterpret_cast<const uint8_t*>(value), length) == 0;
}

bool finish_hash(File& file, mbedtls_sha256_context* sha, uint8_t digest[32]) {
  const bool flushed = file.flush();
  const bool closed = file.close();
  const bool hashed = mbedtls_sha256_finish(sha, digest) == 0;
  mbedtls_sha256_free(sha);
  return flushed && closed && hashed;
}

bool write_text_hashed(storage::FileSystem& fs, const char* path, const char* text,
                       HashEntry* entry) {
  File file = fs.open(path, FILE_WRITE, true);
  if (!file) file = fs.open(path, FILE_WRITE);
  if (!file || !entry) return false;
  mbedtls_sha256_context sha;
  if (!start_hash(&sha)) { file.close(); return false; }
  const size_t length = std::strlen(text);
  const bool wrote = write_hash(file, &sha, text, length);
  if (!wrote) {
    mbedtls_sha256_free(&sha);
    file.close();
    return false;
  }
  return finish_hash(file, &sha, entry->digest);
}

bool write_observations(storage::FileSystem& fs, const char* path, const SaveRequest& request,
                        HashEntry* entry) {
  File file = fs.open(path, FILE_WRITE, true);
  if (!file) file = fs.open(path, FILE_WRITE);
  if (!file || !entry) return false;
  mbedtls_sha256_context sha;
  if (!start_hash(&sha)) { file.close(); return false; }
  constexpr char header[] = "kind,value,source,observed_utc,age_seconds,source_label\n";
  bool ok = write_hash(file, &sha, header, sizeof(header) - 1);
  char line[320]{};
  for (size_t i = 0; ok && i < request.observation_count; ++i) {
    if (!request.observations || !request.observations[i].valid) continue;
    ok = report::encode_observation_csv(request.observations[i], line, sizeof(line));
    if (ok) {
      const size_t len = std::strlen(line);
      ok = write_hash(file, &sha, line, len) && write_hash(file, &sha, "\n", 1);
    }
  }
  if (!ok) {
    mbedtls_sha256_free(&sha);
    file.close();
    return false;
  }
  return finish_hash(file, &sha, entry->digest);
}

bool make_dir(storage::FileSystem& fs, const char* path) {
  return fs.exists(path) || fs.mkdir(path);
}

bool valid_id(const char* id) {
  if (!id || !*id || std::strlen(id) >= 48) return false;
  for (const char* p = id; *p; ++p) {
    const char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  }
  return true;
}

}  // namespace

bool save_snapshot(storage::FileSystem& fs, const SaveRequest& request,
                   SaveResult* result, char* error, size_t error_capacity) {
  if (result) *result = {};
  if (!valid_id(request.session.id)) {
    set_error(error, error_capacity, "invalid Weather report id");
    return false;
  }
  if (!make_dir(fs, "/orcsdr") || !make_dir(fs, kRoot) || !make_dir(fs, kReportsRoot)) {
    set_error(error, error_capacity, "cannot create Weather report directories");
    return false;
  }

  char final_dir[128]{}, staged_dir[136]{};
  std::snprintf(final_dir, sizeof(final_dir), "%s/%s", kReportsRoot, request.session.id);
  std::snprintf(staged_dir, sizeof(staged_dir), "%s.part", final_dir);
  if (fs.exists(final_dir) || fs.exists(staged_dir) || !fs.mkdir(staged_dir)) {
    set_error(error, error_capacity, "Weather report id already exists");
    return false;
  }

  HashEntry hashes[] = {{"report.html"}, {"session.json"}, {"observations.csv"},
                        {"rf_events.csv"}, {"alerts.json"}};
  char path[176]{};
  char text[1536]{};
  bool ok = true;

  std::snprintf(text, sizeof(text),
      "<!doctype html><meta charset=\"utf-8\"><title>OrcSDR Weather snapshot</title>"
      "<h1>OrcSDR Weather snapshot</h1><p>Session: %s</p><p>Location: %.40s</p>"
      "<p>NOAA RF state: %u; selected channel: %u; strongest Hz: %lu.</p>"
      "<p>Source classes and ages are preserved in observations.csv. This report does not claim hardware verification.</p>",
      request.session.id, request.location, static_cast<unsigned>(request.rf.rf_state),
      static_cast<unsigned>(request.rf.selected_channel + 1),
      static_cast<unsigned long>(request.rf.strongest_frequency_hz));
  std::snprintf(path, sizeof(path), "%s/%s", staged_dir, hashes[0].name);
  ok = write_text_hashed(fs, path, text, &hashes[0]);

  if (ok) {
    ok = report::encode_session_json(request.session, text, sizeof(text));
    std::snprintf(path, sizeof(path), "%s/%s", staged_dir, hashes[1].name);
    ok = ok && write_text_hashed(fs, path, text, &hashes[1]);
  }
  if (ok) {
    std::snprintf(path, sizeof(path), "%s/%s", staged_dir, hashes[2].name);
    ok = write_observations(fs, path, request, &hashes[2]);
  }
  if (ok) {
    std::snprintf(text, sizeof(text),
                  "frequency_hz,event,samples,strongest_frequency_hz\n%lu,snapshot,%u,%lu\n",
                  static_cast<unsigned long>(request.rf.active_frequency_hz),
                  static_cast<unsigned>(request.rf.scan_samples),
                  static_cast<unsigned long>(request.rf.strongest_frequency_hz));
    std::snprintf(path, sizeof(path), "%s/%s", staged_dir, hashes[3].name);
    ok = write_text_hashed(fs, path, text, &hashes[3]);
  }
  if (ok) {
    std::snprintf(path, sizeof(path), "%s/%s", staged_dir, hashes[4].name);
    ok = write_text_hashed(fs, path, "[]\n", &hashes[4]);
  }
  if (!ok) {
    set_error(error, error_capacity, "Weather report artifact write failed");
    return false;
  }

  std::snprintf(path, sizeof(path), "%s/manifest.sha256", staged_dir);
  File manifest = fs.open(path, FILE_WRITE, true);
  if (!manifest) manifest = fs.open(path, FILE_WRITE);
  if (!manifest) {
    set_error(error, error_capacity, "Weather report manifest open failed");
    return false;
  }
  for (const auto& entry : hashes) {
    char digest[65]{};
    hex(entry.digest, digest);
    if (manifest.printf("%s  %s\n", digest, entry.name) == 0) ok = false;
  }
  ok = ok && manifest.flush() && manifest.close();
  if (!ok || !fs.rename(staged_dir, final_dir)) {
    set_error(error, error_capacity, "Weather report commit failed");
    return false;
  }

  char history[512]{};
  if (!report::encode_history_jsonl(request.session, "snapshot", history, sizeof(history))) {
    set_error(error, error_capacity, "Weather history serialization failed");
    return false;
  }
  File history_file = fs.open(kHistoryPath, FILE_APPEND, true);
  if (!history_file) history_file = fs.open(kHistoryPath, FILE_APPEND);
  const bool history_ok = history_file &&
                          history_file.print(history) == std::strlen(history) &&
                          history_file.flush() && history_file.close();
  if (result) {
    result->ok = true;
    result->history_recorded = history_ok;
    std::snprintf(result->report_path, sizeof(result->report_path), "%s", final_dir);
  }
  if (!history_ok) {
    set_error(error, error_capacity, "Weather report saved; history append failed");
    return false;
  }
  return true;
}

size_t count_history(storage::FileSystem& fs, size_t limit) {
  File file = fs.open(kHistoryPath, FILE_READ);
  if (!file) return 0;
  const size_t total = file.size();
  size_t count = 0;
  char line[512]{};
  while (file.position() < total && count < limit) {
    const size_t used = file.readBytesUntil('\n', line, sizeof(line) - 1);
    line[used] = '\0';
    if (used && std::strstr(line, "\"schema\":1") && std::strstr(line, "\"id\":")) ++count;
    if (used == 0 && file.position() >= total) break;
  }
  file.close();
  return count;
}

}  // namespace orcsdr::weather::report_store
