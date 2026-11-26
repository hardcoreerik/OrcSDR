#pragma once
// Digital Voice DEVELOPMENT capture: records the live IQ stream the Tab5 itself receives (raw CU8, exactly what the receiver and
// decoders see) to the SD card for offline analysis. Started by a serial command for a chosen centre, rate and time; never on by
// default, never part of normal operation. Receive only.
//
// The card sustains about 4.7 MiB/s (RTL_SD_BENCH, 2026-11-25), so the capture is a pipeline of 2 MB segments: the DSP task copies
// blocks into the segment being filled, the UI task hands each full segment to the shared SD writer. When every segment is busy
// a block is DROPPED and recorded (stream offset and size) in the sidecar, so the file stays usable with known gaps and the
// analyst never has to guess. Rates whose byte rate is above what the card holds will show up as drops, not as silent damage.
//
// Pure logic: no radio, no card, no allocation (the caller supplies the segment memory, PSRAM on the Tab5). Host-testable.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dv_capture {

constexpr size_t kSegBytes = 2u * 1024u * 1024u;
constexpr size_t kSegs = 5;                       // 10 MB of PSRAM (the largest free block on the Tab5 with Digital Voice open is about 13 MB)
constexpr size_t kMemBytes = kSegBytes * kSegs;
constexpr size_t kMaxDrops = 64;                  // gaps listed in the sidecar (the total is always counted)
constexpr uint32_t kMaxSeconds = 120;

struct Config {
  uint32_t center_hz = 0, rate = 0, seconds = 10, utc = 0;
};
struct Drop { uint64_t offset = 0; uint32_t bytes = 0; };

enum class State : uint8_t { idle, capturing, draining, done, failed };

class Capture {
 public:
  void attach(uint8_t* memory) noexcept { mem_ = memory; }
  bool start(const Config& c, uint32_t now_ms) noexcept;     // false without memory, centre, rate or time
  void stop() noexcept;                                       // ends capture; full and partial segments are still written
  void abort() noexcept;                                      // forget everything
  State state() const noexcept { return state_.load(std::memory_order_acquire); }
  bool capturing() const noexcept { return state() == State::capturing; }

  // DSP task: one CU8 block at `rate`. Copies into the current segment; never blocks. A block at another rate is skipped (counted).
  void offer(const uint8_t* cu8, size_t bytes, uint32_t rate) noexcept;

  // UI task. now_ms ends the capture when the time is up.
  void service(uint32_t now_ms) noexcept;
  // The next segment to write (oldest first). False when none is ready. The data stays valid until write_done().
  bool next(const uint8_t** data, size_t* bytes, char* path, size_t path_cap) noexcept;
  void write_started() noexcept;                              // the segment returned by next() was accepted by the writer
  void write_done(bool ok) noexcept;
  bool writing() const noexcept { return writing_; }
  // The sidecar JSON (valid once state is done or failed). Returns the length, 0 if it does not fit.
  size_t sidecar(char* out, size_t cap) const noexcept;
  bool sidecar_path(char* out, size_t cap) const noexcept;

  uint64_t bytes_captured() const noexcept { return captured_; }
  uint64_t bytes_dropped() const noexcept { return dropped_; }
  uint32_t drop_events() const noexcept { return drop_events_; }
  uint32_t wrong_rate_blocks() const noexcept { return wrong_rate_.load(std::memory_order_relaxed); }
  uint32_t segments_written() const noexcept { return written_; }
  uint32_t segments_failed() const noexcept { return failed_; }
  uint32_t center_hz() const noexcept { return cfg_.center_hz; }
  uint32_t rate() const noexcept { return cfg_.rate; }
  uint32_t elapsed_ms(uint32_t now_ms) const noexcept { return now_ms - start_ms_; }

 private:
  enum Seg : uint8_t { kFree, kFilling, kFull, kWriting };
  void name(char* out, size_t cap, uint32_t index) const noexcept;
  Config cfg_{};
  uint8_t* mem_ = nullptr;
  std::atomic<State> state_{State::idle};
  std::atomic<uint8_t> seg_state_[kSegs]{};
  uint32_t seg_len_[kSegs]{}, seg_index_[kSegs]{};
  size_t fill_seg_ = 0;            // DSP task: segment being filled
  size_t write_seg_ = 0;           // UI task: next segment to hand out (segments are used in a ring, so oldest-first holds)
  uint32_t next_index_ = 0, written_ = 0, failed_ = 0, drop_events_ = 0;
  uint64_t captured_ = 0, dropped_ = 0, stream_off_ = 0;
  std::atomic<uint32_t> wrong_rate_{0};
  Drop drops_[kMaxDrops]{};
  uint32_t start_ms_ = 0;
  bool writing_ = false, stop_requested_ = false;
};

// The one capture the DSP tap feeds (installed by the app while it owns one).
void install(Capture* c) noexcept;
bool active() noexcept;                 // capturing: the DSP task skips its normal work for these blocks
void offer_iq(const uint8_t* cu8, size_t bytes, uint32_t rate) noexcept;

}  // namespace orcsdr::dv_capture
