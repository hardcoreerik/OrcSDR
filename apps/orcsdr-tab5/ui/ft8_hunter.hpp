#pragma once

#include "ft8_model.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8 {

enum class HunterMode : uint8_t { fast, decode };
enum class HunterPhase : uint8_t {
  idle,
  tuning,
  waiting_slot,
  observing,
  decoding,
  complete,
  stopped,
  error
};
enum class HunterEvidence : uint8_t { quiet, energy, signature, decoded };

struct HunterObservation {
  bool slot_complete = false;
  bool energy_detected = false;
  uint16_t sync_candidates = 0;
  uint16_t valid_decodes = 0;
  int16_t best_snr_db = -99;
  float peak_dbfs = -120.0f;
};

struct HunterBandResult {
  bool enabled = false;
  bool visited = false;
  HunterEvidence evidence = HunterEvidence::quiet;
  uint8_t slots_observed = 0;
  uint16_t sync_candidates = 0;
  uint16_t valid_decodes = 0;
  int16_t best_snr_db = -99;
  float peak_dbfs = -120.0f;
};

struct HunterSnapshot {
  HunterMode mode = HunterMode::fast;
  HunterPhase phase = HunterPhase::idle;
  size_t current_band = SIZE_MAX;
  size_t best_band = SIZE_MAX;
  uint8_t slots_per_band = 1;
  HunterBandResult results[16]{};
};

uint32_t common_band_mask();
const char* hunter_mode_name(HunterMode mode);
const char* hunter_phase_name(HunterPhase phase);
const char* hunter_evidence_name(HunterEvidence evidence);

class Hunter {
 public:
  bool start(HunterMode mode, uint32_t band_mask = 0, uint8_t slots_per_band = 0);
  void stop();
  void reset();
  bool mark_tuned();
  bool begin_slot();
  bool mark_decoding();
  bool finish_slot(const HunterObservation& observation);

  bool active() const;
  uint32_t requested_frequency_hz() const;
  size_t current_band() const { return snapshot_.current_band; }
  size_t best_band() const { return snapshot_.best_band; }
  const HunterSnapshot& snapshot() const { return snapshot_; }

 private:
  bool select_first_band();
  bool advance_band();
  void update_best();
  HunterSnapshot snapshot_{};
};

bool hunter_self_check();

}  // namespace orcsdr::ft8
