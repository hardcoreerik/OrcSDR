#pragma once
// SD card file transfer over the serial console: list, chunked download with SHA-256, chunked upload to a .part file
// that is checked and renamed into place (old file kept as .bak until the rename succeeds), remove, abort.
// Moved unchanged from main.cpp. The commands themselves are parsed in serial_sd.cpp; the console, the card and the
// radio-busy rule come in through Hooks, so every byte written is what main.cpp's Serial calls wrote before.
// Device-only (orcsdr::storage, mbedTLS); the path rules are in sd_paths.cpp and host-tested.
#include <cstddef>
#include <cstdint>

namespace orcsdr::storage {
class FileSystem;
}

namespace orcsdr::sd_transfer {

constexpr size_t kPutChunkBytes = 16 * 1024;
// USB Serial/JTAG has a 4 KiB TX queue; smaller reads avoid producer deadlock.
constexpr size_t kGetChunkBytes = 2 * 1024;
constexpr uint64_t kMaxBytes = 64ULL * 1024ULL * 1024ULL;

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;                          // Serial.print(text)
  void (*write_line)(const char* text) = nullptr;                     // Serial.println(text)
  void (*write_hex)(const uint8_t* value, size_t size) = nullptr;     // print_hex()
  size_t (*write_bytes)(const uint8_t* data, size_t size) = nullptr;  // Serial.writeBytes()
  size_t (*read_bytes)(uint8_t* out, size_t size, uint32_t timeout_ms) = nullptr;   // Serial.readBytes()
  bool (*decode_hex)(const char* text, uint8_t* out, size_t size) = nullptr;
  bool (*decode_hex_text)(const char* text, char* out, size_t size) = nullptr;
  bool (*radio_busy)() = nullptr;               // a capture is queued/running or audio is recording
  void (*end_splash)() = nullptr;               // end the boot splash if it is showing (it reads the card)
  bool (*ensure_sd)() = nullptr;                // mount the card if needed
  orcsdr::storage::FileSystem* (*fs)() = nullptr;   // the mounted card (valid after ensure_sd() returned true)
};

void list(const Hooks& hooks);
void get_begin(const Hooks& hooks, const char* path_hex);
void get_chunk(const Hooks& hooks);
void get_abort(const Hooks& hooks, const char* reason);
void remove(const Hooks& hooks, const char* path_hex);
void put_begin(const Hooks& hooks, char* arguments);   // "<bytes> <sha256 hex> <path hex>", split in place
void put_chunk(const Hooks& hooks, const char* length_text);
void put_abort(const Hooks& hooks, const char* reason);

bool get_active();
bool put_active();
// True from the start of a get/put until it ends; the radio driver task waits while it is set.
bool active();

}  // namespace orcsdr::sd_transfer
