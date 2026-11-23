#pragma once
// Serial IQ transfers on the PSRAM capture buffer: download of a finished capture with SHA-256 (RTL_IQ_GET_*), and
// upload of an IQ recording that is checked and queued for the native LoRa decoder (RTL_LORA_REPLAY_*).
// Moved unchanged from main.cpp. The commands are parsed in serial_iq_transfer.cpp; the console, the capture buffer and
// the decoder queue come in through Hooks. Device-only (mbedTLS).
#include <cstddef>
#include <cstdint>

namespace orcsdr::iq_transfer_engine {

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;                          // Serial.print(text)
  void (*write_line)(const char* text) = nullptr;                     // Serial.println(text)
  void (*write_hex)(const uint8_t* value, size_t size) = nullptr;     // print_hex()
  size_t (*write_bytes)(const uint8_t* data, size_t size) = nullptr;  // Serial.writeBytes()
  size_t (*read_bytes)(uint8_t* out, size_t size, uint32_t timeout_ms) = nullptr;   // Serial.readBytes()
  bool (*decode_hex)(const char* text, uint8_t* out, size_t size) = nullptr;
  // Download: the finished capture. capture_ready() reads the size first, then checks that no capture or decode is
  // running, the buffer exists, it was not an automatic trigger, and the size is not 0.
  bool (*capture_ready)(size_t* bytes) = nullptr;
  size_t (*capture_bytes)() = nullptr;
  const uint8_t* (*capture_data)() = nullptr;
  // Replay upload.
  size_t (*replay_max_bytes)() = nullptr;        // kIqRecMaxBytes
  uint32_t (*sample_rate)() = nullptr;           // the only accepted rate
  bool (*replay_busy)() = nullptr;               // radio, IQ capture or decoder busy
  bool (*prepare_decoder)() = nullptr;           // capture buffers allocated and the native decoder running
  uint8_t* (*replay_buffer)() = nullptr;         // where the upload is written
  // Hand the uploaded bytes to the native decoder; false when it is busy or its queue is full.
  bool (*queue_decode)(size_t bytes, uint8_t sf, uint32_t bandwidth_hz, uint32_t frequency_hz) = nullptr;
};

void get_begin(const Hooks& hooks);
void get_chunk(const Hooks& hooks);
void get_abort(const Hooks& hooks, const char* reason);
void get_reset();   // silently drop a download
bool get_active();

void replay_begin(const Hooks& hooks, const char* arguments);   // "<bytes> <sha256 hex> <rate> <hz> <sf> <bw>"
void replay_chunk(const Hooks& hooks, const char* length_text);
void replay_abort(const Hooks& hooks, const char* reason);
bool replay_active();

}  // namespace orcsdr::iq_transfer_engine
