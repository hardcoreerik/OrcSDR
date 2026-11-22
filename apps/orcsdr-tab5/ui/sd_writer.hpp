#pragma once
// Writes one file to the SD card from a background task, in pieces, so the caller (the main loop: screen, touch,
// serial) never waits on the card.
//
// Why: an RDS multiplex capture (3.84 MB) written inline from the main loop stalled it for 10 to 12 seconds, and the
// receiver's input fell to about a tenth of its rate while the card was busy, which the owner heard as stuttering
// audio. Here the data is copied through a small cache-aligned staging buffer into the card in `chunk_bytes` pieces with
// a pause after each, and the caller polls for the result.
//
// One job at a time. The caller owns the data and must keep it alive and unchanged until result() reports done or
// failed, then call acknowledge(). Portable: the platform supplies file access, staging memory, the pause and the
// wake-up of the worker, so the logic builds and is tested on a host.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace orcsdr::sd_writer {

constexpr size_t kPathMax = 96;
constexpr size_t kHeaderMax = 256;

struct Platform {
  void* (*open)(const char* path) = nullptr;                                // created for writing; nullptr = failed
  size_t (*write)(void* file, const uint8_t* data, size_t size) = nullptr;  // bytes accepted
  bool (*close)(void* file) = nullptr;
  void* (*alloc_staging)(size_t size) = nullptr;  // may return nullptr: then pieces are written straight from the data
  void (*free_staging)(void* staging) = nullptr;
  void (*pause)() = nullptr;                      // after each piece; lets the radio and the screen have the card back
  void (*wake)() = nullptr;                       // a job is queued: run run_once() on the worker
  size_t chunk_bytes = 32 * 1024;
};

enum class State : uint8_t { idle, queued, writing, done, failed };
enum class Failure : uint8_t { none, open, write };

struct Result {
  State state = State::idle;
  Failure failure = Failure::none;
  size_t written = 0;  // header and data bytes the card accepted
  size_t wanted = 0;   // header and data bytes asked for
};

class Writer {
 public:
  explicit Writer(const Platform& platform) : platform_(platform) {}

  // Caller side. Copies `path` and `header`; `data` is only referenced. False when a job is already in hand, the path
  // or header is too long, or there is nothing to write.
  bool submit(const char* path, const void* header, size_t header_len, const void* data, size_t data_len);
  Result result() const;
  bool busy() const;    // queued or writing
  void acknowledge();   // after done or failed: ready for the next job

  // Worker side. Performs a queued job, if any. Returns true when it ran one.
  bool run_once();

 private:
  const Platform& platform_;
  std::atomic<State> state_{State::idle};
  char path_[kPathMax] = {};
  uint8_t header_[kHeaderMax] = {};
  size_t header_len_ = 0;
  const uint8_t* data_ = nullptr;
  size_t data_len_ = 0;
  Failure failure_ = Failure::none;
  size_t written_ = 0;
};

// The firmware's one shared writer and its task (sd_writer_device.cpp). Not built on the host.
Writer& shared();

}  // namespace orcsdr::sd_writer
