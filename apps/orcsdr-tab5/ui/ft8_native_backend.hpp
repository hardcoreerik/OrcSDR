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
//   FFT spectral grid (2 rows per symbol) -> Costas sync search -> per-candidate time/frequency refinement scored on the sync
//   symbols -> best candidates through soft demod, LDPC, CRC-14, unpack and plausibility (ft8_pipeline::try_candidate).
// Clean-room: written from this repository's own spectral/sync/pipeline modules and the measurements in
// docs/ft8/REAL_WAV_BENCHMARK.md. No external decoder code. No SNR is reported (no calibrated estimator exists), and no
// transmit path exists.

// Optional allocator for the large buffers (the firmware passes PSRAM). Defaults to malloc/free.
struct Memory {
  void* (*alloc)(size_t bytes) = nullptr;
  void (*release)(void* pointer) = nullptr;
};

struct Config {
  uint16_t candidate_k = 32;      // coarse candidates refined
  uint16_t gate = 16;             // refined candidates sent through the FEC gates
  float min_score = 0.10f;        // coarse sync threshold
  bool joint_search = false;      // joint time x frequency refinement (about 3.7x the cost, recovers local-optimum misses)
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
  size_t hop_ = 0;
  size_t first_bin_ = 0;
  size_t bin_count_ = 0;
  double bin_hz_ = 0.0;
  float* grid_ = nullptr;
  size_t grid_rows_capacity_ = 0;
  float* local_ = nullptr;          // channel_symbols x tone_count candidate-local energies
  sync::Candidate* candidates_ = nullptr;
  pipeline::Workspace* workspace_ = nullptr;

  Stats stats_{};
};

// Binds a Backend to the UI seam. `backend` must outlive the returned struct.
orcsdr::ft8::DecoderBackend make_seam(Backend* backend);

bool self_check();

}  // namespace orcsdr::ftx::native
