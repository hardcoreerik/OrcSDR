#pragma once
// Background microSD archive for SSTV receptions.
//
// The receiver (UI task) publishes finished images to a small in-RAM gallery. This
// module copies them to the SD card on its own low-priority task so that a slow,
// full or missing card can never stall the audio tap, the USB receiver or the UI:
//
//   * images are pinned (reference counted), so eviction cannot free pixels under the writer;
//   * the job queue is tiny and bounded; a full queue drops the job, never blocks;
//   * every BMP is written to <name>.part and renamed only when complete;
//   * the index is append-only CSV (schema v2); an older v1 index is kept, not edited;
//   * results (pending/saved/failed + reason) are visible to the UI;
//   * all file access goes through Io, so host tests can inject slow/full/removed cards.
//
// One reception event always produces one file and one index row, even if two images
// contain identical pixels; a content hash is recorded so duplicates can be grouped later.
#include "sstv_receiver.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace orcsdr::sstv_archive {

struct Io {
  bool (*mounted)() = nullptr;
  bool (*make_dir)(const char* path) = nullptr;           // true if it exists afterwards
  std::FILE* (*open)(const char* path, const char* mode) = nullptr;
  size_t (*write)(const void* data, size_t size, std::FILE* file) = nullptr;
  size_t (*read)(void* data, size_t size, std::FILE* file) = nullptr;
  int (*close)(std::FILE* file) = nullptr;
  int (*rename)(const char* from, const char* to) = nullptr;
  int (*remove)(const char* path) = nullptr;
  bool (*exists)(const char* path) = nullptr;
  uint64_t (*free_bytes)() = nullptr;
  // Calls visit(name, ctx) for every entry in dir; false if the directory is unreadable.
  bool (*list_dir)(const char* dir, void (*visit)(const char* name, void* ctx), void* ctx) = nullptr;
  uint32_t (*now_ms)() = nullptr;
  void (*yield)() = nullptr;                              // called between 16 KiB blocks
  void (*delay_ms)(uint32_t ms) = nullptr;                // optional pause before a rewrite attempt
};
const Io& default_io();  // libc/VFS (device: FatFS under /sd) with the Tab5 mount and free-space checks

enum class JobState : uint8_t { none, pending, saving, saved, failed, skipped };
enum class Error : uint8_t {
  none, no_card, no_space, write_error, rename_error, index_error, too_few_rows, queue_full, shutdown
};
const char* error_name(Error error);
const char* state_name(JobState state);

struct Config {
  bool enabled = true;           // auto-save finished and interrupted images
  uint16_t min_rows = 8;         // below this a reception is noise, not an image
  uint8_t retries = 2;           // extra attempts while the card is missing
  uint32_t retry_delay_ms = 10000;
  char root[64] = "/sd/orcsdr/sstv";
};

struct Result {
  uint32_t session_id = 0;
  JobState state = JobState::none;
  Error error = Error::none;
  uint32_t duration_ms = 0;      // time spent writing (SD latency), 0 until finished
  uint32_t bytes = 0;
  int os_error = 0;              // errno at the first failed file operation, 0 if none
  char filename[64] = {};
};

struct Stats {
  uint32_t queued = 0;           // jobs accepted
  uint32_t saved = 0;
  uint32_t failed = 0;
  uint32_t skipped = 0;          // too few rows
  uint32_t dropped_queue_full = 0;
  uint32_t pending = 0;          // queued or being written right now
  uint32_t last_write_ms = 0;
  uint32_t max_write_ms = 0;
  uint32_t write_retries = 0;     // whole-file rewrites after a transient card error (card health indicator)
  Error last_error = Error::none;
};

// Reset all state and select file access. Does not start the worker.
void init(const Io* io, const Config& config);
const Config& config();
void set_enabled(bool enabled);
bool enabled();

// UI task, cheap: queues images archived since the last call (when enabled).
// utc_epoch == 0 records an unverified clock. Returns the number queued.
size_t poll(uint32_t utc_epoch);
// Queue one pinned image (ownership of the pin passes to the archive on true; on false
// the caller still owns it and must release_image()). Never blocks.
bool enqueue(const orcsdr::sstv::Image& pinned, uint32_t utc_epoch);

// Worker side. step() handles at most one job and returns true if more work is waiting.
bool step();
// Starts/stops the background worker (FreeRTOS task on the device, a thread on the host).
bool start_worker();
void stop_worker();
bool worker_running();
// Host/tests: run jobs on the calling thread until the queue is empty or time runs out.
void drain(uint32_t max_ms);

Stats stats();
size_t recent_results(Result* out, size_t capacity);   // newest first
bool result_for(uint32_t session_id, Result* out);

// Power-loss recovery: delete stale .part files and add an index row for every BMP the
// index does not mention. Returns how many BMPs were re-indexed. Call after mount.
size_t recover();
// Ask the worker to run recover() when it is idle (never blocks the caller).
void request_recover();
// True after init(); init() abandons queued jobs, so call it once and use set_enabled() afterwards.
bool initialised();

extern const char kIndexHeaderV2[];

}  // namespace orcsdr::sstv_archive
