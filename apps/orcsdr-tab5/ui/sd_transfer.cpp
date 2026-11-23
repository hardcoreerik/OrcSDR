#include "sd_transfer.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_attr.h>
#include <mbedtls/sha256.h>

#include "orcsdr_storage.hpp"
#include "sd_paths.hpp"

namespace orcsdr::sd_transfer {
namespace {

struct SdPutState {
  File file;
  bool active = false;
  uint64_t expected = 0;
  uint64_t received = 0;
  char target[sd_paths::kMaxPath]{};
  char temporary[136]{};
  uint8_t expected_sha[32]{};
  mbedtls_sha256_context sha;
};
SdPutState g_sd_put;
struct SdGetState {
  File file;
  bool active = false;
  uint64_t size = 0;
  uint64_t sent = 0;
  char path[sd_paths::kMaxPath]{};
  mbedtls_sha256_context sha;
};
SdGetState g_sd_get;
std::atomic<bool> g_sd_transfer_active{false};
// PSRAM: only the serial SD get/put commands use it, and the serial link,
// not memory, sets their speed.
EXT_RAM_BSS_ATTR uint8_t g_sd_put_chunk[kPutChunkBytes];

// Serial.printf() in main.cpp: format, then one write.
__attribute__((format(printf, 2, 3))) void say(const Hooks& hooks, const char* format, ...) {
  char line[320];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  hooks.write(line);
}

void reset_sd_get_state() {
  g_sd_get.active = false;
  g_sd_get.size = 0;
  g_sd_get.sent = 0;
  g_sd_get.path[0] = '\0';
}

void reset_sd_put_state() {
  g_sd_put.active = false;
  g_sd_put.expected = 0;
  g_sd_put.received = 0;
  g_sd_put.target[0] = '\0';
  g_sd_put.temporary[0] = '\0';
  std::memset(g_sd_put.expected_sha, 0, sizeof(g_sd_put.expected_sha));
}

bool sd_put_commit(const Hooks& hooks) {
  orcsdr::storage::FileSystem* fs = hooks.fs();
  uint8_t digest[32];
  if (mbedtls_sha256_finish(&g_sd_put.sha, digest) != 0) {
    put_abort(hooks, "sha_finish");
    return false;
  }
  mbedtls_sha256_free(&g_sd_put.sha);
  g_sd_put.active = false;
  const bool flushed = g_sd_put.file.flush();
  const bool closed = g_sd_put.file.close();
  if (!flushed || !closed) {
    fs->remove(g_sd_put.temporary);
    hooks.write_line(flushed ? "SD_PUT_ERROR close_failed" : "SD_PUT_ERROR flush_failed");
    reset_sd_put_state();
    g_sd_transfer_active.store(false, std::memory_order_release);
    return false;
  }

  uint8_t difference = 0;
  for (size_t i = 0; i < sizeof(digest); ++i) {
    difference |= digest[i] ^ g_sd_put.expected_sha[i];
  }
  if (difference != 0) {
    fs->remove(g_sd_put.temporary);
    hooks.write_line("SD_PUT_ERROR sha_mismatch");
    reset_sd_put_state();
    g_sd_transfer_active.store(false, std::memory_order_release);
    return false;
  }

  char backup[136];
  snprintf(backup, sizeof(backup), "%s.bak", g_sd_put.target);
  fs->remove(backup);
  const bool had_target = fs->exists(g_sd_put.target);
  if (had_target && !fs->rename(g_sd_put.target, backup)) {
    fs->remove(g_sd_put.temporary);
    hooks.write_line("SD_PUT_ERROR backup_failed");
    reset_sd_put_state();
    g_sd_transfer_active.store(false, std::memory_order_release);
    return false;
  }
  if (!fs->rename(g_sd_put.temporary, g_sd_put.target)) {
    const bool restored = !had_target || fs->rename(backup, g_sd_put.target);
    fs->remove(g_sd_put.temporary);
    hooks.write_line(restored ? "SD_PUT_ERROR rename_failed"
                              : "SD_PUT_ERROR rename_and_restore_failed");
    reset_sd_put_state();
    g_sd_transfer_active.store(false, std::memory_order_release);
    return false;
  }
  if (had_target) fs->remove(backup);
  say(hooks, "SD_PUT_DONE bytes=%llu sha256=", static_cast<unsigned long long>(g_sd_put.received));
  hooks.write_hex(digest, sizeof(digest));
  say(hooks, " path=\"%s\"\n", g_sd_put.target);
  reset_sd_put_state();
  g_sd_transfer_active.store(false, std::memory_order_release);
  return true;
}

}  // namespace

bool get_active() { return g_sd_get.active; }
bool put_active() { return g_sd_put.active; }
bool active() { return g_sd_transfer_active.load(std::memory_order_acquire); }

void list(const Hooks& hooks) {
  if (hooks.radio_busy()) {
    hooks.write_line("SD_LIST_ERROR radio_busy");
    return;
  }
  if (g_sd_put.active || g_sd_get.active || g_sd_transfer_active.load(std::memory_order_acquire)) {
    hooks.write_line("SD_LIST_ERROR transfer_busy");
    return;
  }
  hooks.end_splash();
  if (!hooks.ensure_sd()) {
    hooks.write_line("SD_LIST_ERROR sd_unavailable");
    return;
  }
  File directory = hooks.fs()->open("/orcsdr");
  if (!directory || !directory.isDirectory()) {
    hooks.write_line("SD_LIST_ERROR open_failed");
    return;
  }
  size_t count = 0;
  size_t skipped = 0;
  size_t visited = 0;
  bool truncated = false;
  for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (visited >= kMaxListEntries) {
      truncated = true;
      entry.close();
      break;
    }
    ++visited;
    if (!entry.isDirectory()) {
      char path[sd_paths::kMaxPath];
      const char* name = entry.name();
      bool fits = false;
      if (name[0] == '/') {
        fits = strlen(name) < sizeof(path);
        if (fits) strlcpy(path, name, sizeof(path));
      } else {
        const int written = snprintf(path, sizeof(path), "/orcsdr/%s", name);
        fits = written > 0 && static_cast<size_t>(written) < sizeof(path);
      }
      if (fits) {
        say(hooks, "SD_LIST_ENTRY bytes=%llu modified=%llu pathhex=",
            static_cast<unsigned long long>(entry.size()),
            static_cast<unsigned long long>(entry.getLastWrite()));
        hooks.write_hex(reinterpret_cast<const uint8_t*>(path), strlen(path));
        hooks.write("\n");   // Serial.println()
        ++count;
      } else {
        ++skipped;
      }
    }
    entry.close();
  }
  directory.close();
  if (skipped > 0 && truncated) {
    say(hooks, "SD_LIST_DONE count=%u skipped=%u truncated=1\n",
        static_cast<unsigned>(count), static_cast<unsigned>(skipped));
  } else if (skipped > 0) {
    say(hooks, "SD_LIST_DONE count=%u skipped=%u\n",
        static_cast<unsigned>(count), static_cast<unsigned>(skipped));
  } else if (truncated) {
    say(hooks, "SD_LIST_DONE count=%u truncated=1\n", static_cast<unsigned>(count));
  } else {
    say(hooks, "SD_LIST_DONE count=%u\n", static_cast<unsigned>(count));
  }
}

void get_abort(const Hooks& hooks, const char* reason) {
  if (g_sd_get.file) g_sd_get.file.close();
  if (g_sd_get.active) mbedtls_sha256_free(&g_sd_get.sha);
  reset_sd_get_state();
  say(hooks, "SD_GET_ERROR %s\n", reason ? reason : "aborted");
  g_sd_transfer_active.store(false, std::memory_order_release);
}

void get_begin(const Hooks& hooks, const char* path_hex) {
  if (g_sd_get.active || g_sd_put.active) {
    hooks.write_line("SD_GET_ERROR transfer_busy");
    return;
  }
  g_sd_transfer_active.store(true, std::memory_order_release);
  char path[sizeof(g_sd_get.path)];
  const bool path_ok = hooks.decode_hex_text(path_hex, path, sizeof(path)) && sd_paths::transfer_allowed(path);
  // Active ACARS archive logs are append-only, so reading one while the receiver runs is safe: the read stops at the size seen here.
  const bool acars_log = path_ok && sd_paths::is_acars_log(path);
  if (hooks.radio_busy() && !acars_log) {
    hooks.write_line("SD_GET_ERROR radio_busy");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  if (!path_ok) {
    hooks.write_line("SD_GET_ERROR invalid_path");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  hooks.end_splash();
  if (!hooks.ensure_sd()) {
    hooks.write_line("SD_GET_ERROR sd_unavailable");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  g_sd_get.file = hooks.fs()->open(path, FILE_READ);
  if (!g_sd_get.file || g_sd_get.file.isDirectory()) {
    if (g_sd_get.file) g_sd_get.file.close();
    reset_sd_get_state();
    hooks.write_line("SD_GET_ERROR open_failed");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  g_sd_get.size = g_sd_get.file.size();
  if (g_sd_get.size == 0 || g_sd_get.size > kMaxBytes) {
    g_sd_get.file.close();
    reset_sd_get_state();
    hooks.write_line("SD_GET_ERROR invalid_size");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  strlcpy(g_sd_get.path, path, sizeof(g_sd_get.path));
  mbedtls_sha256_init(&g_sd_get.sha);
  if (mbedtls_sha256_starts(&g_sd_get.sha, 0) != 0) {
    g_sd_get.active = true;
    get_abort(hooks, "sha_start");
    return;
  }
  g_sd_get.active = true;
  say(hooks, "SD_GET_READY chunk=%u bytes=%llu path=\"%s\"\n",
      static_cast<unsigned>(kGetChunkBytes),
      static_cast<unsigned long long>(g_sd_get.size), g_sd_get.path);
}

void get_chunk(const Hooks& hooks) {
  if (!g_sd_get.active) {
    hooks.write_line("SD_GET_ERROR not_active");
    return;
  }
  const uint64_t remaining = g_sd_get.size - g_sd_get.sent;
  const size_t wanted = static_cast<size_t>(
      remaining < kGetChunkBytes ? remaining : kGetChunkBytes);
  const size_t got = g_sd_get.file.read(g_sd_put_chunk, wanted);
  if (got != wanted) {
    get_abort(hooks, "read_failed");
    return;
  }
  if (mbedtls_sha256_update(&g_sd_get.sha, g_sd_put_chunk, got) != 0) {
    get_abort(hooks, "sha_update");
    return;
  }
  say(hooks, "SD_GET_DATA bytes=%u\n", static_cast<unsigned>(got));
  if (hooks.write_bytes(g_sd_put_chunk, got) != got) {
    get_abort(hooks, "serial_write");
    return;
  }
  g_sd_get.sent += got;
  if (g_sd_get.sent != g_sd_get.size) return;

  uint8_t digest[32];
  if (mbedtls_sha256_finish(&g_sd_get.sha, digest) != 0) {
    get_abort(hooks, "sha_finish");
    return;
  }
  mbedtls_sha256_free(&g_sd_get.sha);
  g_sd_get.file.close();
  g_sd_get.active = false;
  say(hooks, "SD_GET_DONE bytes=%llu sha256=", static_cast<unsigned long long>(g_sd_get.sent));
  hooks.write_hex(digest, sizeof(digest));
  say(hooks, " path=\"%s\"\n", g_sd_get.path);
  reset_sd_get_state();
  g_sd_transfer_active.store(false, std::memory_order_release);
}

void remove(const Hooks& hooks, const char* path_hex) {
  if (hooks.radio_busy()) {
    hooks.write_line("SD_REMOVE_ERROR radio_busy");
    return;
  }
  if (g_sd_put.active || g_sd_get.active || g_sd_transfer_active.load(std::memory_order_acquire)) {
    hooks.write_line("SD_REMOVE_ERROR transfer_busy");
    return;
  }
  char path[sizeof(g_sd_put.target)];
  if (!hooks.decode_hex_text(path_hex, path, sizeof(path)) || !sd_paths::remove_allowed(path)) {
    hooks.write_line("SD_REMOVE_ERROR invalid_path");
    return;
  }
  hooks.end_splash();
  if (!hooks.ensure_sd()) {
    hooks.write_line("SD_REMOVE_ERROR sd_unavailable");
    return;
  }
  orcsdr::storage::FileSystem* fs = hooks.fs();
  if (!fs->exists(path)) {
    say(hooks, "SD_REMOVE_MISSING path=\"%s\"\n", path);
    return;
  }
  say(hooks, fs->remove(path) ? "SD_REMOVE_DONE path=\"%s\"\n"
                              : "SD_REMOVE_ERROR remove_failed path=\"%s\"\n",
      path);
}

void put_abort(const Hooks& hooks, const char* reason) {
  if (g_sd_put.file) g_sd_put.file.close();
  if (g_sd_put.temporary[0]) hooks.fs()->remove(g_sd_put.temporary);
  if (g_sd_put.active) mbedtls_sha256_free(&g_sd_put.sha);
  reset_sd_put_state();
  say(hooks, "SD_PUT_ERROR %s\n", reason ? reason : "aborted");
  g_sd_transfer_active.store(false, std::memory_order_release);
}

void put_begin(const Hooks& hooks, char* arguments) {
  if (g_sd_put.active || g_sd_get.active) {
    hooks.write_line("SD_PUT_ERROR already_active");
    return;
  }
  g_sd_transfer_active.store(true, std::memory_order_release);
  if (hooks.radio_busy()) {
    hooks.write_line("SD_PUT_ERROR radio_busy");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  char* sha_text = strchr(arguments, ' ');
  if (sha_text == nullptr) {
    hooks.write_line("SD_PUT_ERROR invalid_begin");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  *sha_text++ = '\0';
  char* path_hex = strchr(sha_text, ' ');
  if (path_hex == nullptr) {
    hooks.write_line("SD_PUT_ERROR invalid_begin");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  *path_hex++ = '\0';
  char target[sizeof(g_sd_put.target)];
  const uint64_t size = strtoull(arguments, nullptr, 10);
  uint8_t digest[32];
  if (size == 0 || size > kMaxBytes || !hooks.decode_hex(sha_text, digest, sizeof(digest)) ||
      !hooks.decode_hex_text(path_hex, target, sizeof(target)) || !sd_paths::transfer_allowed(target)) {
    hooks.write_line("SD_PUT_ERROR invalid_begin");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  /* Stop the SD-backed loading animation before taking exclusive card ownership. */
  hooks.end_splash();
  if (!hooks.ensure_sd() ||
      (!hooks.fs()->exists("/orcsdr") && !hooks.fs()->mkdir("/orcsdr"))) {
    hooks.write_line("SD_PUT_ERROR sd_unavailable");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  orcsdr::storage::FileSystem* fs = hooks.fs();

  strlcpy(g_sd_put.target, target, sizeof(g_sd_put.target));
  snprintf(g_sd_put.temporary, sizeof(g_sd_put.temporary), "%s.part", target);
  fs->remove(g_sd_put.temporary);
  g_sd_put.file = fs->open(g_sd_put.temporary, FILE_WRITE);
  if (!g_sd_put.file) {
    reset_sd_put_state();
    hooks.write_line("SD_PUT_ERROR open_failed");
    g_sd_transfer_active.store(false, std::memory_order_release);
    return;
  }
  g_sd_put.expected = size;
  memcpy(g_sd_put.expected_sha, digest, sizeof(digest));
  mbedtls_sha256_init(&g_sd_put.sha);
  if (mbedtls_sha256_starts(&g_sd_put.sha, 0) != 0) {
    g_sd_put.active = true;
    put_abort(hooks, "sha_start");
    return;
  }
  g_sd_put.active = true;
  say(hooks, "SD_PUT_READY chunk=%u bytes=%llu path=\"%s\"\n",
      static_cast<unsigned>(kPutChunkBytes),
      static_cast<unsigned long long>(size), target);
}

void put_chunk(const Hooks& hooks, const char* length_text) {
  if (!g_sd_put.active) {
    hooks.write_line("SD_PUT_ERROR not_active");
    return;
  }
  const size_t length = static_cast<size_t>(strtoul(length_text, nullptr, 10));
  if (length == 0 || length > kPutChunkBytes ||
      g_sd_put.received + length > g_sd_put.expected) {
    put_abort(hooks, "invalid_chunk");
    return;
  }
  say(hooks, "SD_PUT_DATA bytes=%u\n", static_cast<unsigned>(length));
  const size_t got = hooks.read_bytes(g_sd_put_chunk, length, 3000);
  if (got != length) {
    put_abort(hooks, "chunk_timeout");
    return;
  }
  if (g_sd_put.file.write(g_sd_put_chunk, length) != length) {
    put_abort(hooks, "write_failed");
    return;
  }
  if (mbedtls_sha256_update(&g_sd_put.sha, g_sd_put_chunk, length) != 0) {
    put_abort(hooks, "sha_update");
    return;
  }
  g_sd_put.received += length;
  say(hooks, "SD_PUT_ACK bytes=%llu\n", static_cast<unsigned long long>(g_sd_put.received));
  if (g_sd_put.received == g_sd_put.expected) (void)sd_put_commit(hooks);
}

}  // namespace orcsdr::sd_transfer
