#pragma once

#include "ft8_decoder_backend.hpp"
#include "ft8_mode.hpp"
#include "ft8_model.hpp"
#include "ft8_pipeline.hpp"
#include "ft8_spectral_fft.hpp"
#include "ft8_sync.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::ftx::native {

// OrcSDR's native receive-only decoder behind the DecoderBackend seam: buffer one slot of 12 kS/s USB audio, then
//   one FFT pass builds a fine time-frequency grid (4 or 8 rows per symbol, half-bin frequency spacing) -> a decimated copy
//   (2 rows per symbol, whole bins) feeds the Costas sync search -> each coarse candidate is refined by scoring the fine grid
//   around it (no per-candidate correlation) -> the best go through soft demod, LDPC, CRC-14, unpack and plausibility.
// Clean-room: written from this repository's own spectral/sync/pipeline modules and the measurements in
// docs/ft8/REAL_WAV_BENCHMARK.md. No external decoder code. No SNR is reported (no calibrated estimator exists), and no
// transmit path exists.

// Optional allocator for the large buffers (the firmware passes PSRAM). Defaults to malloc/free.
struct Memory {
  void* (*alloc)(size_t bytes) = nullptr;
  void (*release)(void* pointer) = nullptr;
};

struct Config {
  uint16_t candidate_k = 64;      // coarse candidates refined
  uint16_t gate = 32;             // refined candidates sent through the FEC gates
  float min_score = 0.10f;        // coarse sync threshold
  uint8_t fine_rows = 4;          // fine-grid rows per symbol: 4 (40 ms) or 8 (20 ms); the grid is always half-bin in frequency
  uint32_t deadline_ms = 0;       // stop refining/gating after this long (0 = no limit); needs now_us
  uint64_t (*now_us)() = nullptr;
};

struct Stats {
  uint32_t spectral_ms = 0;
  uint32_t search_ms = 0;
  uint32_t refine_ms = 0;
  uint32_t gate_ms = 0;
  uint32_t total_ms = 0;
  uint16_t coarse_candidates = 0;
  uint16_t attempted = 0;
  uint16_t accepted = 0;
  bool deadline_hit = false;
  bool slot_too_short = false;
};

class Backend {
 public:
  Backend() = default;
  ~Backend();
  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;

  bool begin(uint32_t sample_rate_hz, Mode mode, const Config& config, const Memory& memory = {});
  void end();
  bool set_mode(Mode mode);
  void reset();
  bool begin_slot(uint64_t slot_epoch_ms);
  bool offer_audio(const int16_t* samples, size_t count);
  // Decodes the buffered slot. `incomplete` marks a slot whose audio had a discontinuity: it is skipped, not "decoded".
  size_t finish_slot(orcsdr::ft8::Decode* output, size_t capacity, bool incomplete = false);

  // Tunables that may change between slots. candidate_k and gate are clamped to kMaxCandidateK.
  static constexpr uint16_t kMaxCandidateK = 64;
  void set_config(const Config& config);
  const Config& config() const { return config_; }
  // The slot most recently offered (valid until the next begin_slot); for diagnostics such as saving it to storage.
  const int16_t* slot_audio() const { return samples_; }
  uint64_t slot_epoch_ms() const { return slot_epoch_ms_; }

  const Stats& stats() const { return stats_; }
  Mode mode() const { return mode_; }
  size_t buffered() const { return filled_; }
  bool ready() const { return samples_ != nullptr; }

 private:
  void* grab(size_t bytes);
  void drop(void* pointer);
  bool configure_mode(Mode mode);

  Config config_{};
  Memory memory_{};
  Mode mode_ = Mode::ft8;
  uint32_t sample_rate_ = 0;
  uint64_t slot_epoch_ms_ = 0;
  bool slot_open_ = false;

  int16_t* samples_ = nullptr;
  size_t capacity_ = 0;
  size_t filled_ = 0;

  // spectral geometry for the active mode
  spectral_fft::Plan plan_{};
  spectral_fft::Scratch scratch_{};
  size_t symbol_samples_ = 0;
  size_t fine_rows_ = 4;            // fine rows per symbol in use
  size_t fine_hop_ = 0;
  size_t first_bin_ = 0;            // coarse (whole-bin) grid
  size_t bin_count_ = 0;
  size_t fine_bin_count_ = 0;
  double bin_hz_ = 0.0;
  float* fine_ = nullptr;           // fine grid: rows x fine_bin_count_
  size_t fine_rows_capacity_ = 0;
  float* coarse_ = nullptr;         // decimated copy for the coarse search: rows x bin_count_
  size_t coarse_rows_capacity_ = 0;
  sync::Candidate* candidates_ = nullptr;
  pipeline::Workspace* workspace_ = nullptr;

  Stats stats_{};
};

// Binds a Backend to the UI seam. `backend` must outlive the returned struct.
orcsdr::ft8::DecoderBackend make_seam(Backend* backend);

bool self_check();

}  // namespace orcsdr::ftx::native
