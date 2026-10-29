#pragma once
// VEIL-26 interpretation records: formatting and a bounded hand-off from the UI task to the SD worker.
//
// These records live in their own file (veil26_<session>.jsonl), never in the measured-event journal, and they are always
// tagged "interpreted": true. A record names every input and convention it used, so it can be recomputed from the record
// alone: state_in, freq_code and level_code, the frame index and the dictionary id fully determine the symbols.
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "dead_air_veil26.hpp"
#include "dead_air_runes.hpp"

namespace orcsdr::dead_air::veil26 {

enum class Trigger : uint8_t { sample, event };

// What the app knows when it analyses a frame. All numbers are measured or logged values; nothing here is invented.
struct RecordInput {
  Frame frame{};
  runes::Alphabet alphabet = runes::Alphabet::latin;
  const char* intention = "";
  uint32_t seed = 0;
  Trigger trigger = Trigger::sample;
  uint32_t event_sequence = 0;   // Dead Air event this frame interprets (0 for a periodic sample)
  const char* event_kind = "";   // kind_name() of that event, for the reader
  uint32_t monotonic_ms = 0;
  uint32_t unix_time = 0;        // 0 = wall clock not verified
  uint32_t frequency_hz = 0;     // tuned frequency at the observation
  double level_dbfs = 0.0;       // measured level (relative dBFS, never dBm)
  const char* level_source = "rf";  // where the level came from: "rf" (receiver) or "audio"
  uint32_t config_revision = 0;  // Dead Air's receiver configuration revision at the observation
};

constexpr size_t kLineBytes = 640;
// One JSON line, newline included. Returns the length, or 0 if it does not fit. Strings are escaped.
size_t format_record(const RecordInput& in, char* out, size_t capacity);
// The session header written when the file is opened and whenever the intention changes.
size_t format_config(const char* intention, uint32_t seed, uint32_t unix_time, uint32_t monotonic_ms, char* out, size_t capacity,
                     runes::Alphabet alphabet = runes::Alphabet::latin);
// The closing line: what was analysed, what was logged, what was dropped.
struct Counters {
  uint32_t frames = 0, gaps = 0, skipped_invalid = 0, records_posted = 0, records_dropped = 0, records_written = 0, write_failures = 0;
};
size_t format_summary(const Counters& c, char* out, size_t capacity);

// Bounded single-producer (UI task) single-consumer (SD worker) line queue. The producer never blocks and never waits for
// the worker: a full queue drops the line and counts it.
class RecordQueue {
 public:
  static constexpr uint32_t kSlots = 16;
  bool post(const char* line, size_t length);               // producer
  bool take(char* out, size_t capacity, size_t* length);    // consumer: copies one line out, frees the slot
  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
  uint32_t posted() const { return posted_.load(std::memory_order_relaxed); }
  uint32_t pending() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
  void reset();  // only with no consumer running

 private:
  char line_[kSlots][kLineBytes];
  uint16_t length_[kSlots]{};
  std::atomic<uint32_t> head_{0}, tail_{0}, dropped_{0}, posted_{0};
};

// Where the worker puts lines. A failed append makes the writer permanently failed, like the event journal's sink.
class LineWriter {
 public:
  virtual ~LineWriter() = default;
  virtual bool append(const char* line, size_t length) = 0;
};
// Moves up to `max_lines` queued lines to the writer. Returns how many were written; a failure counts the rest of that
// batch as dropped by the caller via the returned failure flag.
size_t drain(RecordQueue& queue, LineWriter& writer, size_t max_lines, bool* write_failed);

}  // namespace orcsdr::dead_air::veil26
