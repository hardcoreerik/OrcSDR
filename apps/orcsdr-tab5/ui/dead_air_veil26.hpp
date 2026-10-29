#pragma once
// VEIL-26, the experimental "Esoteric Frequency Demodulator", as an optional analysis engine inside Dead Air.
//
// What this is: a deterministic, reproducible transformation of REAL measured numbers (a tuned frequency and a measured
// level) into SYMBOLS (a letter, a dictionary word, two gematria sums). Same inputs and same intention give the same
// symbols on any machine; every record names the inputs so a result can be recomputed.
// What this is not: a detector, a classifier, or a message. The symbols are the output of a fixed arithmetic recipe. They
// carry no information about the source of the signal, and the engine never raises a Dead Air event.
//
// Pure logic (no clock, no I/O, no allocation, no display): the Dead Air app feeds it observations and decides what to show
// and what to log. Host-tested against golden vectors; the arithmetic is the 32-bit state recurrence of the VEIL-26 v0.1.0
// Python prototype, with Python's round-half-to-even and modulo-2^32 behaviour.
//
// Provenance gap: the prototype's canonical 32-word dictionary was not available when this was written. kDictionary is a
// clearly-labelled placeholder (kDictionaryId); every record names the dictionary it used, so results made with the
// placeholder can never be mistaken for canonical ones. Replace the table and the id together.
#include <cstddef>
#include <cstdint>

namespace orcsdr::dead_air::veil26 {

constexpr const char* kAlgorithm = "VEIL26-v1";        // seed prefix and record tag
constexpr const char* kDictionaryId = "placeholder-0";  // NOT the canonical dictionary; see above
constexpr const char* kQuantLog = "freq_log_v1";        // kHz > 1 MHz, Hz 1 kHz..1 MHz, centihertz < 1 kHz; (dB + 160) * 10
constexpr const char* kQuantSpectral = "spectral_v1";   // Hz * 100 and RMS amplitude * 10000
constexpr size_t kWordCount = 32;
constexpr size_t kMaxIntention = 24;                    // characters kept of the user's intention
constexpr size_t kLetterTail = 32;                      // letters kept for the live sequence
extern const char* const kDictionary[kWordCount];

// ---- SHA-256 (FIPS 180-4), used only to turn the intention into the initial state --------------------------------------
void sha256(const uint8_t* data, size_t length, uint8_t out[32]);
// First four bytes, big-endian, of SHA-256("VEIL26-v1:" + intention).
uint32_t seed_state(const char* intention);

// ---- Quantization ------------------------------------------------------------------------------------------------------
// Python round(): halves go to the even neighbour. Values outside int64 are clamped.
int64_t py_round(double value);

struct Codes { int64_t frequency = 0; int64_t level = 0; };
// Spectral convention: frequency in Hz and an RMS amplitude (0..1 of full scale or any linear amplitude).
Codes quantize_spectral(double frequency_hz, double rms_amplitude);
// Frequency-log convention, used for Dead Air's tuned frequency and measured level in dBFS. A level in dBFS is NOT an RMS
// amplitude, which is why it has its own convention: `(level_db + 160) * 10`.
Codes quantize_log(double frequency_hz, double level_db);

// ---- The Sigil Engine ---------------------------------------------------------------------------------------------------
// state = ((state * 33) ^ (frequency_code * 131) ^ (level_code * 17) ^ frame_index) mod 2^32
uint32_t step_state(uint32_t state, const Codes& codes, uint32_t frame_index);

// ---- Gematria -----------------------------------------------------------------------------------------------------------
// Non-letters are ignored; case is ignored. A1Z26: A=1 .. Z=26. Pythagorean: A=1 .. I=9, J=1 .. R=9, S=1 .. Z=8.
uint32_t gematria_a1z26(const char* text);
uint32_t gematria_pythagorean(const char* text);
uint32_t digital_root(uint32_t value);  // repeated digit sum down to 1..9; 0 stays 0

// One analysed frame.
struct Frame {
  uint32_t index = 0;          // frame_index used in the recurrence: 0 for the first frame (Python enumerate order; see the docs)
  uint32_t state_in = 0;       // state before this frame (the seed for the first one)
  uint32_t state = 0;          // state after
  Codes codes{};
  uint8_t letter_number = 0;   // 1..26
  char letter = 'A';
  uint8_t word_index = 0;      // 0..31
  const char* word = "";
  uint32_t word_a1z26 = 0, word_pythagorean = 0;
};

class Engine {
 public:
  Engine() { reset(""); }
  // Starts a new stream from the intention: state = seed, frame counter 0, empty sequence.
  void reset(const char* intention);
  const char* intention() const { return intention_; }
  uint32_t seed() const { return seed_; }
  uint32_t state() const { return state_; }
  uint32_t frames() const { return frames_; }

  // Advances the stream by one frame with already-quantized inputs.
  Frame feed(const Codes& codes);
  // Convenience: a measured tuned frequency (Hz) and level (dBFS) through the frequency-log convention.
  Frame feed_rf(uint32_t frequency_hz, double level_dbfs);
  // Marks a break in the measurements (a retune, a lost receiver, a stale reading): the state is NOT changed, so the stream
  // stays reproducible, but the letter sequence shows a break and the gap is counted.
  void note_gap();
  uint32_t gaps() const { return gaps_; }

  // The newest letters, oldest first, with '|' where a gap fell. NUL-terminated.
  const char* letters() const { return letters_; }
  size_t letter_count() const { return letter_count_; }
  uint32_t sequence_a1z26() const;         // gematria of the letters shown (breaks ignored)
  uint32_t sequence_pythagorean() const;

 private:
  char intention_[kMaxIntention + 1]{};
  uint32_t seed_ = 0, state_ = 0, frames_ = 0, gaps_ = 0;
  char letters_[kLetterTail + 1]{};
  size_t letter_count_ = 0;
};

// ---- Policy: when does an observation become a frame ---------------------------------------------------------------------
// Dead Air observes the receiver about 20 times a second. VEIL-26 makes at most one frame a second from a USABLE
// observation (valid, settled, a tuned frequency, a finite level), so the letter stream is readable and the work per
// second is bounded. An unusable observation, a retune or a receiver reconfiguration is a break, never a fabricated frame.
struct Observation {
  uint32_t now_ms = 0;
  bool valid = false;            // the receiver produced a fresh level
  bool settled = false;          // past the settling window of a retune / gain / rate change
  uint32_t frequency_hz = 0;     // tuned frequency (0 = none)
  uint32_t config_revision = 0;  // changes on every retune or receiver (re)start
  double level_dbfs = 0.0;       // relative dBFS
};

class Stream : public Engine {
 public:
  static constexpr uint32_t kFrameMs = 1000;       // one frame per second at most
  static constexpr uint32_t kSampleLogMs = 30000;  // a periodic frame is worth logging this often
  enum class Result : uint8_t { skipped, waiting, frame };

  // Starts again from an intention: frame counter, letters, timers and gap state all restart (the skipped count too).
  void restart(const char* intention);
  // `frame` is set on Result::frame; `log_due` says whether that periodic frame should also be logged.
  Result observe(const Observation& o, Frame* frame, bool* log_due);
  // A Dead Air RF or audio event is its own frame, whenever it happens. False (and counted as skipped) without a usable pair.
  bool observe_event(uint32_t frequency_hz, double level_dbfs, Frame* frame);
  uint32_t skipped() const { return skipped_; }

 private:
  bool in_gap_ = true;
  uint32_t cfg_rev_ = 0, next_frame_ms_ = 0, next_log_ms_ = 0, skipped_ = 0;
};

}  // namespace orcsdr::dead_air::veil26
