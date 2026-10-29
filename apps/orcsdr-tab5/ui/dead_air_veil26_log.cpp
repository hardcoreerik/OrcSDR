#include "dead_air_veil26_log.hpp"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::dead_air::veil26 {

namespace {

// Writes `text` as a JSON string body (no quotes) into out[pos..], escaping quotes, backslashes and anything non-printable.
// Returns false if it does not fit.
bool put_escaped(char* out, size_t capacity, size_t* pos, const char* text) {
  for (; text && *text; ++text) {
    const unsigned char c = static_cast<unsigned char>(*text);
    const char* esc = nullptr;
    char single[2] = {static_cast<char>(c), 0};
    if (c == '"') esc = "\\\"";
    else if (c == '\\') esc = "\\\\";
    else if (c < 0x20 || c > 0x7e) esc = "?";
    else esc = single;
    const size_t n = std::strlen(esc);
    if (*pos + n >= capacity) return false;
    std::memcpy(out + *pos, esc, n);
    *pos += n;
  }
  return true;
}

}  // namespace

size_t format_record(const RecordInput& in, char* out, size_t capacity) {
  if (!out || capacity < 8) return 0;
  char intention[kMaxIntention * 2 + 4];
  char kind[40];
  size_t p = 0;
  if (!put_escaped(intention, sizeof(intention), &p, in.intention)) return 0;
  intention[p] = '\0';
  p = 0;
  if (!put_escaped(kind, sizeof(kind), &p, in.event_kind)) return 0;
  kind[p] = '\0';
  char utc[16];
  if (in.unix_time) std::snprintf(utc, sizeof(utc), "%" PRIu32, in.unix_time);
  else std::snprintf(utc, sizeof(utc), "null");
  const Frame& f = in.frame;
  const int ri = runes::index(in.alphabet, f.state);
  const auto* rune = runes::glyph(in.alphabet, static_cast<unsigned>(ri));
  char rune_fields[96];
  if (rune) std::snprintf(rune_fields, sizeof(rune_fields), "\"rune_index\":%d,\"rune_name\":\"%s\"", ri, rune->name);
  else std::snprintf(rune_fields, sizeof(rune_fields), "\"rune_index\":null,\"rune_name\":null");
  const int n = std::snprintf(
      out, capacity,
      "{\"v\":1,\"type\":\"veil26\",\"interpreted\":true,\"alg\":\"%s\",\"dict\":\"%s\",\"quant\":\"%s\","
      "\"trigger\":\"%s\",\"event_seq\":%" PRIu32 ",\"event_kind\":\"%s\",\"intention\":\"%s\",\"seed\":\"%08" PRIx32 "\","
      "\"frame\":%" PRIu32 ",\"t_ms\":%" PRIu32 ",\"utc\":%s,\"freq_hz\":%" PRIu32 ",\"level_dbfs\":%.3f,\"level_src\":\"%s\","
      "\"cfg_rev\":%" PRIu32 ",\"freq_code\":%" PRId64 ",\"level_code\":%" PRId64 ",\"state_in\":\"%08" PRIx32 "\","
      "\"state\":\"%08" PRIx32 "\",\"letter_n\":%u,\"letter\":\"%c\",\"word_i\":%u,\"word\":\"%s\","
      "\"a1z26\":%" PRIu32 ",\"pyth\":%" PRIu32 ",\"rune_alphabet\":\"%s\",%s}\n",
      kAlgorithm, kDictionaryId, kQuantLog, in.trigger == Trigger::event ? "event" : "sample", in.event_sequence, kind, intention,
      in.seed, f.index, in.monotonic_ms, utc, in.frequency_hz, std::isfinite(in.level_dbfs) ? in.level_dbfs : 0.0,
      in.level_source ? in.level_source : "rf", in.config_revision, f.codes.frequency, f.codes.level, f.state_in, f.state,
      static_cast<unsigned>(f.letter_number), f.letter, static_cast<unsigned>(f.word_index), f.word, f.word_a1z26,
      f.word_pythagorean, runes::alphabet(in.alphabet).id, rune_fields);
  if (n <= 0 || static_cast<size_t>(n) >= capacity) return 0;
  return static_cast<size_t>(n);
}

size_t format_config(const char* intention, uint32_t seed, uint32_t unix_time, uint32_t monotonic_ms, char* out, size_t capacity, runes::Alphabet alphabet) {
  if (!out || capacity < 8) return 0;
  char text[kMaxIntention * 2 + 4];
  size_t p = 0;
  if (!put_escaped(text, sizeof(text), &p, intention)) return 0;
  text[p] = '\0';
  char utc[16];
  if (unix_time) std::snprintf(utc, sizeof(utc), "%" PRIu32, unix_time);
  else std::snprintf(utc, sizeof(utc), "null");
  const int n = std::snprintf(
      out, capacity,
      "{\"v\":1,\"type\":\"veil26_config\",\"interpreted\":true,\"alg\":\"%s\",\"dict\":\"%s\",\"dict_canonical\":false,"
      "\"quant\":\"%s\",\"rune_alphabet\":\"%s\",\"frame_base\":0,\"rule\":\"docs/dead-air/VEIL26.md\","
      "\"intention\":\"%s\",\"seed\":\"%08" PRIx32 "\",\"utc\":%s,\"t_ms\":%" PRIu32 ","
      "\"note\":\"symbols come from a fixed recipe applied to measured numbers; not measurements, messages or detections\"}\n",
      kAlgorithm, kDictionaryId, kQuantLog, runes::alphabet(alphabet).id, text, seed, utc, monotonic_ms);
  if (n <= 0 || static_cast<size_t>(n) >= capacity) return 0;
  return static_cast<size_t>(n);
}

size_t format_summary(const Counters& c, char* out, size_t capacity) {
  if (!out || capacity < 8) return 0;
  const int n = std::snprintf(
      out, capacity,
      "{\"v\":1,\"type\":\"veil26_summary\",\"interpreted\":true,\"frames\":%" PRIu32 ",\"gaps\":%" PRIu32 ",\"skipped_invalid\":%" PRIu32
      ",\"records_posted\":%" PRIu32 ",\"records_dropped\":%" PRIu32 ",\"records_written\":%" PRIu32 ",\"write_failures\":%" PRIu32 "}\n",
      c.frames, c.gaps, c.skipped_invalid, c.records_posted, c.records_dropped, c.records_written, c.write_failures);
  if (n <= 0 || static_cast<size_t>(n) >= capacity) return 0;
  return static_cast<size_t>(n);
}

bool RecordQueue::post(const char* line, size_t length) {
  if (!line || length == 0 || length > kLineBytes) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const uint32_t head = head_.load(std::memory_order_relaxed);
  if (head - tail_.load(std::memory_order_acquire) >= kSlots) {  // the worker is behind: drop, never wait
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const uint32_t slot = head % kSlots;
  std::memcpy(line_[slot], line, length);
  length_[slot] = static_cast<uint16_t>(length);
  head_.store(head + 1, std::memory_order_release);
  posted_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool RecordQueue::take(char* out, size_t capacity, size_t* length) {
  const uint32_t tail = tail_.load(std::memory_order_relaxed);
  if (tail == head_.load(std::memory_order_acquire)) return false;
  const uint32_t slot = tail % kSlots;
  const size_t n = length_[slot];
  if (!out || n > capacity) {
    tail_.store(tail + 1, std::memory_order_release);  // cannot be delivered: free the slot rather than stall
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  std::memcpy(out, line_[slot], n);
  if (length) *length = n;
  tail_.store(tail + 1, std::memory_order_release);
  return true;
}

void RecordQueue::reset() {
  head_.store(0, std::memory_order_relaxed);
  tail_.store(0, std::memory_order_relaxed);
  dropped_.store(0, std::memory_order_relaxed);
  posted_.store(0, std::memory_order_relaxed);
}

size_t drain(RecordQueue& queue, LineWriter& writer, size_t max_lines, bool* write_failed) {
  char line[kLineBytes];
  size_t written = 0, length = 0;
  for (size_t i = 0; i < max_lines; ++i) {
    if (!queue.take(line, sizeof(line), &length)) break;
    if (!writer.append(line, length)) {
      if (write_failed) *write_failed = true;
      break;
    }
    ++written;
  }
  return written;
}

}  // namespace orcsdr::dead_air::veil26
