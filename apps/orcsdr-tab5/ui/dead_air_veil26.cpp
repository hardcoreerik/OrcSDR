#include "dead_air_veil26.hpp"

#include <cmath>
#include <cstring>

namespace orcsdr::dead_air::veil26 {

// PLACEHOLDER dictionary (see kDictionaryId). Plain, neutral words so nothing here can be read as the canonical list.
const char* const kDictionary[kWordCount] = {
    "ANCHOR", "BEACON", "CIPHER", "DRIFT",  "ECHO",   "FATHOM", "GLASS",  "HOLLOW",
    "INK",    "JUNCTION", "KEYSTONE", "LANTERN", "MERIDIAN", "NEEDLE", "ORBIT",  "PRISM",
    "QUIET",  "RIVER",  "SIGNAL", "THRESHOLD", "UMBRA", "VESSEL", "WINDOW", "XENON",
    "YIELD",  "ZENITH", "AMBER",  "BRIDGE", "COMPASS", "DELTA",  "EMBER",  "FLINT"};

namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

struct Sha256 {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[64]{};
  size_t used = 0;
  uint64_t total = 0;
  void compress() {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t{block[i * 4]} << 24) | (uint32_t{block[i * 4 + 1]} << 16) | (uint32_t{block[i * 4 + 2]} << 8) | block[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  void update(const uint8_t* data, size_t length) {
    total += length;
    for (size_t i = 0; i < length; ++i) {
      block[used++] = data[i];
      if (used == 64) { compress(); used = 0; }
    }
  }
  void finish(uint8_t out[32]) {
    const uint64_t bits = total * 8u;
    block[used++] = 0x80;
    if (used > 56) {
      while (used < 64) block[used++] = 0;
      compress();
      used = 0;
    }
    while (used < 56) block[used++] = 0;
    for (int i = 7; i >= 0; --i) block[used++] = static_cast<uint8_t>(bits >> (i * 8));
    compress();
    for (int i = 0; i < 8; ++i) {
      out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
      out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
      out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
      out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
  }
};

int letter_index(char c) {  // 1..26, or 0 for a non-letter
  if (c >= 'a' && c <= 'z') return c - 'a' + 1;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
  return 0;
}

}  // namespace

void sha256(const uint8_t* data, size_t length, uint8_t out[32]) {
  Sha256 s;
  if (data && length) s.update(data, length);
  s.finish(out);
}

uint32_t seed_state(const char* intention) {
  Sha256 s;
  const char* prefix = "VEIL26-v1:";
  s.update(reinterpret_cast<const uint8_t*>(prefix), std::strlen(prefix));
  if (intention) s.update(reinterpret_cast<const uint8_t*>(intention), std::strlen(intention));
  uint8_t digest[32];
  s.finish(digest);
  return (uint32_t{digest[0]} << 24) | (uint32_t{digest[1]} << 16) | (uint32_t{digest[2]} << 8) | digest[3];
}

int64_t py_round(double value) {
  if (!std::isfinite(value)) return 0;
  if (value >= 9.2e18) return INT64_MAX;
  if (value <= -9.2e18) return INT64_MIN;
  return static_cast<int64_t>(std::nearbyint(value));  // the default rounding mode is round-half-to-even, like Python
}

Codes quantize_spectral(double frequency_hz, double rms_amplitude) {
  return {py_round(frequency_hz * 100.0), py_round(rms_amplitude * 10000.0)};
}

Codes quantize_log(double frequency_hz, double level_db) {
  Codes c;
  if (frequency_hz > 1.0e6) c.frequency = py_round(frequency_hz / 1000.0);       // kilohertz
  else if (frequency_hz >= 1.0e3) c.frequency = py_round(frequency_hz);          // whole hertz
  else c.frequency = py_round(frequency_hz * 100.0);                             // centihertz
  c.level = py_round((level_db + 160.0) * 10.0);
  return c;
}

uint32_t step_state(uint32_t state, const Codes& codes, uint32_t frame_index) {
  // Python integers are unbounded and the result is reduced mod 2^32 at the end; the low 32 bits of a product, XOR or
  // two's-complement negative are independent of the higher bits, so wrapping 64-bit arithmetic gives the same answer.
  const uint64_t a = static_cast<uint64_t>(state) * 33u;
  const uint64_t b = static_cast<uint64_t>(codes.frequency) * 131u;
  const uint64_t c = static_cast<uint64_t>(codes.level) * 17u;
  return static_cast<uint32_t>((a ^ b ^ c ^ static_cast<uint64_t>(frame_index)) & 0xffffffffu);
}

uint32_t gematria_a1z26(const char* text) {
  uint32_t sum = 0;
  for (; text && *text; ++text) sum += static_cast<uint32_t>(letter_index(*text));
  return sum;
}

uint32_t gematria_pythagorean(const char* text) {
  uint32_t sum = 0;
  for (; text && *text; ++text) {
    const int n = letter_index(*text);
    if (n) sum += static_cast<uint32_t>((n - 1) % 9 + 1);
  }
  return sum;
}

uint32_t digital_root(uint32_t value) {
  if (value == 0) return 0;
  return 1 + (value - 1) % 9;
}

void Engine::reset(const char* intention) {
  char copy[kMaxIntention + 1] = {};  // the caller may pass this engine's own intention()
  if (intention) std::strncpy(copy, intention, kMaxIntention);
  std::memcpy(intention_, copy, sizeof(intention_));
  seed_ = seed_state(intention_);
  state_ = seed_;
  frames_ = 0;
  gaps_ = 0;
  std::memset(letters_, 0, sizeof(letters_));
  letter_count_ = 0;
}

Frame Engine::feed(const Codes& codes) {
  Frame f;
  f.index = frames_;
  f.state_in = state_;
  f.codes = codes;
  state_ = step_state(state_, codes, frames_);
  ++frames_;
  f.state = state_;
  f.letter_number = static_cast<uint8_t>(1 + state_ % 26u);
  f.letter = static_cast<char>('A' + f.letter_number - 1);
  f.word_index = static_cast<uint8_t>((state_ / 26u) % kWordCount);
  f.word = kDictionary[f.word_index];
  f.word_a1z26 = gematria_a1z26(f.word);
  f.word_pythagorean = gematria_pythagorean(f.word);
  if (letter_count_ == kLetterTail) {
    std::memmove(letters_, letters_ + 1, kLetterTail - 1);
    --letter_count_;
  }
  letters_[letter_count_++] = f.letter;
  letters_[letter_count_] = '\0';
  return f;
}

Frame Engine::feed_rf(uint32_t frequency_hz, double level_dbfs) {
  return feed(quantize_log(static_cast<double>(frequency_hz), level_dbfs));
}

void Engine::note_gap() {
  ++gaps_;
  if (letter_count_ == 0 || letters_[letter_count_ - 1] == '|') return;
  if (letter_count_ == kLetterTail) {
    std::memmove(letters_, letters_ + 1, kLetterTail - 1);
    --letter_count_;
  }
  letters_[letter_count_++] = '|';
  letters_[letter_count_] = '\0';
}

void Stream::restart(const char* intention) {
  reset(intention);
  in_gap_ = true;
  cfg_rev_ = 0;
  next_frame_ms_ = 0;
  next_log_ms_ = 0;
  skipped_ = 0;
}

Stream::Result Stream::observe(const Observation& o, Frame* frame, bool* log_due) {
  if (log_due) *log_due = false;
  const bool usable = o.valid && o.settled && o.frequency_hz != 0 && std::isfinite(o.level_dbfs);
  if (!usable) {
    ++skipped_;
    if (!in_gap_) { in_gap_ = true; note_gap(); }  // a retune, a lost receiver, a settling window
    return Result::skipped;
  }
  if (!in_gap_ && o.config_revision != cfg_rev_) note_gap();  // reconfigured with no unusable observation in between
  in_gap_ = false;
  cfg_rev_ = o.config_revision;
  if (static_cast<int32_t>(o.now_ms - next_frame_ms_) < 0) return Result::waiting;
  next_frame_ms_ = o.now_ms + kFrameMs;
  const Frame f = feed_rf(o.frequency_hz, o.level_dbfs);
  if (frame) *frame = f;
  if (static_cast<int32_t>(o.now_ms - next_log_ms_) >= 0) {
    next_log_ms_ = o.now_ms + kSampleLogMs;
    if (log_due) *log_due = true;
  }
  return Result::frame;
}

bool Stream::observe_event(uint32_t frequency_hz, double level_dbfs, Frame* frame) {
  if (frequency_hz == 0 || !std::isfinite(level_dbfs)) { ++skipped_; return false; }
  const Frame f = feed_rf(frequency_hz, level_dbfs);
  if (frame) *frame = f;
  return true;
}

uint32_t Engine::sequence_a1z26() const { return gematria_a1z26(letters_); }
uint32_t Engine::sequence_pythagorean() const { return gematria_pythagorean(letters_); }

}  // namespace orcsdr::dead_air::veil26
