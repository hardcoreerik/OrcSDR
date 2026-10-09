#pragma once

#include "js8_decoder.hpp"
#include "js8_demod.hpp"
#include "js8_frame.hpp"
#include "js8_mode.hpp"
#include "js8_sync.hpp"
#include "ft8_model.hpp"
#include "ft8_spectral_fft.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::native {

// Receive-only JS8 slot front end for the Tab5 firmware, with the same lifecycle as the FT8/FT4 native backend:
//   begin(12000, submode, config, memory) -> begin_slot(epoch_ms) -> offer_audio(...) -> finish_slot(...) -> stats()
//
// What it does today: buffer one slot of 12 kS/s USB audio, build the time/frequency energy grid with the proven mixed-radix
// FFT (one 1920-point transform per half symbol, bins on the 6.25 Hz tone spacing), run the bounded JS8 sync search, and
// demodulate each candidate to a raw 79-tone frame. That is where it stops.
//
// TRUTH BOUNDARY: a sync candidate or a raw tone frame is evidence of a JS8-like transmission, not a message. The tone-to-bit
// mapping, the FEC graph, the CRC and the frame parser have not yet been accepted from the real capture corpus, so finish_slot()
// returns ZERO Decode records. The acceptance layer (FEC -> CRC -> supported frame parser) attaches after the raw-frame stage
// in finish_slot(); nothing user-visible may be produced before it exists. Receive only: no transmit path of any kind.
//
// Only Normal has an independently established sync pattern. Other submodes are refused by begin() and set_submode(); the
// backend never falls back from one submode to another.
struct Memory {
  void* (*alloc)(size_t bytes) = nullptr;
  void (*release)(void* pointer) = nullptr;
};

struct Config {
  uint16_t candidate_limit = 16;   // raw frames kept per slot (at most kMaxCandidates)
  float min_score = 0.10f;         // sync threshold
  uint8_t min_decode_sync_hits = 14;   // raw frames with fewer sync hits (of 21) are not given to the soft decoder
  decoder::Config decoder{};
  uint32_t deadline_ms = 0;        // stop demodulating candidates after this long (0 = no limit); needs now_us
  uint64_t (*now_us)() = nullptr;
};

struct Stats {
  uint32_t spectral_ms = 0;
  uint32_t search_ms = 0;
  uint32_t demod_ms = 0;
  uint32_t total_ms = 0;
  uint16_t grid_rows = 0;
  uint16_t candidates = 0;          // sync candidates above the threshold
  uint16_t strong_candidates = 0;   // sync score of 0.45 or more
  uint16_t raw_frames = 0;          // candidates whose demodulated frame passed the sync-hit checks
  uint16_t refined = 0;             // candidates whose time/frequency was refined
  uint16_t aliases_removed = 0;     // weaker alignments one or two sync periods away from a stronger frame
  float best_sync_score = 0.0f;
  bool deadline_hit = false;
  bool slot_too_short = false;
  bool incomplete = false;          // the slot had a discontinuity and was skipped
  uint32_t decodes = 0;             // messages published (parity and CRC verified, renderable)
  uint16_t decode_attempts = 0;     // raw frames that reached the soft decoder (enough sync hits)
  uint16_t valid_unrendered = 0;    // parity + CRC held but the frame type is not rendered (counted, never shown)
  uint16_t by_hard = 0;             // published by hard decision / belief propagation / OSD
  uint16_t by_bp = 0;
  uint16_t by_osd = 0;
  uint32_t decode_ms = 0;
};

// One raw frame, for diagnostics. Never shown to a user as text.
struct RawResult {
  float audio_hz = 0.0f;       // tone-0 frequency
  uint32_t start_sample = 0;   // first sample of the frame in the slot buffer
  int32_t dt_ms = 0;           // frame start relative to the nominal 0.5 s into the slot
  float sync_score = 0.0f;
  uint8_t sync_hits = 0;
  float mean_margin = 0.0f;
  RawFrame frame{};
  // Soft-decoder log for this frame (valid when attempted).
  bool attempted = false;
  uint16_t initial_syndrome = 0;
  uint16_t final_syndrome = 0;
  uint8_t bp_iterations = 0;
  uint8_t osd_order = 0;
  uint16_t hard_corrections = 0;
  bool crc_valid = false;
  bool rendered = false;
  decoder::Method method = decoder::Method::none;
  char payload[13]{};
  uint8_t frame_kind = 0;
  char text[48]{};
};

class Backend {
 public:
  static constexpr uint16_t kMaxCandidates = 32;

  Backend() = default;
  ~Backend();
  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;

  bool begin(uint32_t sample_rate_hz, Submode submode, const Config& config, const Memory& memory = {});
  void end();
  bool set_submode(Submode submode);   // false (and unchanged) for a submode whose sync pattern is not established
  void reset();

  bool begin_slot(uint64_t slot_epoch_ms);
  bool offer_audio(const int16_t* samples, size_t count);
  // Runs the front end on the buffered slot. Returns the number of Decode records written (verified messages only).
  size_t finish_slot(orcsdr::ft8::Decode* output, size_t capacity, bool incomplete = false);

  void set_config(const Config& config);
  const Config& config() const { return config_; }
  const Stats& stats() const { return stats_; }
  Submode submode() const { return submode_; }
  size_t buffered() const { return filled_; }
  bool ready() const { return samples_ != nullptr; }

  // Raw frames from the last finish_slot(), strongest sync first.
  size_t raw_count() const { return raw_count_; }
  const RawResult* raw(size_t index) const { return index < raw_count_ ? &raw_[index] : nullptr; }

  // Every sync candidate of the last finish_slot() (before demodulation), for diagnostics.
  size_t candidate_count() const { return stats_.candidates; }
  const sync::Candidate* candidate(size_t index) const { return index < stats_.candidates ? &candidates_[index] : nullptr; }

  // The energy grid of the last finish_slot(), so a host test can compare it with the exact-correlation oracle.
  const float* grid() const { return grid_; }
  size_t grid_rows() const { return stats_.grid_rows; }
  size_t grid_bins() const { return bin_count_; }
  float first_hz() const { return first_hz_; }
  const int16_t* slot_audio() const { return samples_; }

 private:
  void* grab(size_t bytes);
  void drop(void* pointer);

  Config config_{};
  Memory memory_{};
  Submode submode_ = Submode::normal;
  uint32_t sample_rate_ = 0;
  uint64_t slot_epoch_ms_ = 0;
  bool slot_open_ = false;
  int16_t* samples_ = nullptr;
  size_t capacity_ = 0;
  size_t filled_ = 0;

  orcsdr::ftx::spectral_fft::Plan plan_{};
  orcsdr::ftx::spectral_fft::Scratch scratch_{};
  size_t symbol_samples_ = 0;
  size_t hop_ = 0;
  size_t first_bin_ = 0;
  size_t bin_count_ = 0;
  float first_hz_ = 0.0f;
  float* grid_ = nullptr;
  size_t grid_row_capacity_ = 0;

  sync::Candidate* candidates_ = nullptr;
  decoder::Workspace* decoder_ws_ = nullptr;
  float (*energy_)[8] = nullptr;
  RawResult raw_[kMaxCandidates]{};
  size_t raw_count_ = 0;
  Stats stats_{};
};

bool self_check();

}  // namespace orcsdr::js8::native
