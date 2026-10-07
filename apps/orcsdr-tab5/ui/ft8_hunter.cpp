#include "ft8_hunter.hpp"

#include <algorithm>
#include <cmath>

namespace orcsdr::ft8 {
namespace {

constexpr size_t kMaxHunterBands = 16;

HunterEvidence evidence_for(const HunterObservation& observation) {
  if (observation.valid_decodes > 0) return HunterEvidence::decoded;
  if (observation.sync_candidates > 0) return HunterEvidence::signature;
  if (observation.energy_detected) return HunterEvidence::energy;
  return HunterEvidence::quiet;
}

bool better(const HunterBandResult& left, size_t left_index,
            const HunterBandResult& right, size_t right_index) {
  if (!left.visited) return false;
  if (!right.visited) return true;
  if (left.evidence != right.evidence)
    return static_cast<uint8_t>(left.evidence) >
           static_cast<uint8_t>(right.evidence);
  if (left.valid_decodes != right.valid_decodes)
    return left.valid_decodes > right.valid_decodes;
  if (left.sync_candidates != right.sync_candidates)
    return left.sync_candidates > right.sync_candidates;
  if (left.best_snr_db != right.best_snr_db)
    return left.best_snr_db > right.best_snr_db;
  if (left.peak_dbfs != right.peak_dbfs)
    return left.peak_dbfs > right.peak_dbfs;
  return left_index < right_index;
}

}  // namespace

uint32_t common_band_mask() {
  uint32_t mask = 0;
  const size_t count = std::min(band_count(), size_t{32});
  for (size_t i = 0; i < count; ++i) {
    const BandPreset* preset = band(i);
    if (preset && preset->common) mask |= (uint32_t{1} << i);
  }
  return mask;
}

const char* hunter_mode_name(HunterMode mode) {
  return mode == HunterMode::decode ? "DECODE HUNT" : "FAST HUNT";
}

const char* hunter_phase_name(HunterPhase phase) {
  switch (phase) {
    case HunterPhase::idle: return "IDLE";
    case HunterPhase::tuning: return "TUNING";
    case HunterPhase::waiting_slot: return "WAIT SLOT";
    case HunterPhase::observing: return "OBSERVING";
    case HunterPhase::decoding: return "DECODING";
    case HunterPhase::complete: return "COMPLETE";
    case HunterPhase::stopped: return "STOPPED";
    case HunterPhase::error: return "ERROR";
  }
  return "ERROR";
}

const char* hunter_evidence_name(HunterEvidence evidence) {
  switch (evidence) {
    case HunterEvidence::quiet: return "QUIET";
    case HunterEvidence::energy: return "ENERGY";
    case HunterEvidence::signature: return "FT8 SIG";
    case HunterEvidence::decoded: return "DECODED";
  }
  return "QUIET";
}

bool Hunter::start(HunterMode mode, uint32_t band_mask, uint8_t slots_per_band) {
  reset();
  if (band_count() == 0 || band_count() > kMaxHunterBands) {
    snapshot_.phase = HunterPhase::error;
    return false;
  }

  snapshot_.mode = mode;
  snapshot_.slots_per_band =
      slots_per_band ? slots_per_band : (mode == HunterMode::decode ? 2 : 1);
  snapshot_.slots_per_band =
      std::clamp<uint8_t>(snapshot_.slots_per_band, 1, 4);

  const uint32_t mask = band_mask ? band_mask : common_band_mask();
  for (size_t i = 0; i < band_count(); ++i)
    snapshot_.results[i].enabled = (mask & (uint32_t{1} << i)) != 0;

  if (!select_first_band()) {
    snapshot_.phase = HunterPhase::error;
    return false;
  }
  snapshot_.phase = HunterPhase::tuning;
  return true;
}

void Hunter::stop() {
  if (active()) snapshot_.phase = HunterPhase::stopped;
}

void Hunter::reset() {
  snapshot_ = HunterSnapshot{};
}

bool Hunter::mark_tuned() {
  if (snapshot_.phase != HunterPhase::tuning ||
      snapshot_.current_band == SIZE_MAX)
    return false;
  snapshot_.phase = HunterPhase::waiting_slot;
  return true;
}

bool Hunter::begin_slot() {
  if (snapshot_.phase != HunterPhase::waiting_slot ||
      snapshot_.current_band == SIZE_MAX)
    return false;
  snapshot_.phase = HunterPhase::observing;
  return true;
}

bool Hunter::mark_decoding() {
  if (snapshot_.mode != HunterMode::decode ||
      snapshot_.phase != HunterPhase::observing)
    return false;
  snapshot_.phase = HunterPhase::decoding;
  return true;
}

bool Hunter::finish_slot(const HunterObservation& observation) {
  if ((snapshot_.phase != HunterPhase::observing &&
       snapshot_.phase != HunterPhase::decoding) ||
      snapshot_.current_band >= band_count() || !observation.slot_complete)
    return false;

  HunterBandResult& result = snapshot_.results[snapshot_.current_band];
  result.visited = true;
  result.slots_observed =
      static_cast<uint8_t>(std::min<int>(255, result.slots_observed + 1));
  result.evidence = static_cast<HunterEvidence>(
      std::max(static_cast<uint8_t>(result.evidence),
               static_cast<uint8_t>(evidence_for(observation))));
  result.sync_candidates = static_cast<uint16_t>(std::min<unsigned>(
      65535u, static_cast<unsigned>(result.sync_candidates) +
                  observation.sync_candidates));
  result.valid_decodes = static_cast<uint16_t>(std::min<unsigned>(
      65535u, static_cast<unsigned>(result.valid_decodes) +
                  observation.valid_decodes));
  if (observation.valid_decodes > 0)
    result.best_snr_db = std::max(result.best_snr_db, observation.best_snr_db);
  if (std::isfinite(observation.peak_dbfs))
    result.peak_dbfs = std::max(result.peak_dbfs, observation.peak_dbfs);
  update_best();

  if (result.slots_observed < snapshot_.slots_per_band) {
    snapshot_.phase = HunterPhase::waiting_slot;
    return true;
  }
  if (advance_band()) {
    snapshot_.phase = HunterPhase::tuning;
    return true;
  }
  snapshot_.phase = HunterPhase::complete;
  return true;
}

bool Hunter::active() const {
  return snapshot_.phase == HunterPhase::tuning ||
         snapshot_.phase == HunterPhase::waiting_slot ||
         snapshot_.phase == HunterPhase::observing ||
         snapshot_.phase == HunterPhase::decoding;
}

uint32_t Hunter::requested_frequency_hz() const {
  const BandPreset* preset =
      snapshot_.current_band < band_count() ? band(snapshot_.current_band) : nullptr;
  return preset ? preset->dial_hz : 0;
}

bool Hunter::select_first_band() {
  for (size_t i = 0; i < band_count(); ++i) {
    if (!snapshot_.results[i].enabled) continue;
    snapshot_.current_band = i;
    return true;
  }
  snapshot_.current_band = SIZE_MAX;
  return false;
}

bool Hunter::advance_band() {
  if (snapshot_.current_band == SIZE_MAX) return false;
  for (size_t i = snapshot_.current_band + 1; i < band_count(); ++i) {
    if (!snapshot_.results[i].enabled) continue;
    snapshot_.current_band = i;
    return true;
  }
  snapshot_.current_band = SIZE_MAX;
  return false;
}

void Hunter::update_best() {
  size_t best = SIZE_MAX;
  for (size_t i = 0; i < band_count(); ++i) {
    const HunterBandResult& candidate = snapshot_.results[i];
    if (!candidate.visited) continue;
    if (best == SIZE_MAX ||
        better(candidate, i, snapshot_.results[best], best))
      best = i;
  }
  snapshot_.best_band = best;
}

bool hunter_self_check() {
  Hunter hunter;
  const uint32_t mask = (uint32_t{1} << 3) | (uint32_t{1} << 5);
  if (!hunter.start(HunterMode::fast, mask, 1) ||
      hunter.current_band() != 3)
    return false;
  if (!hunter.mark_tuned() || !hunter.begin_slot()) return false;

  HunterObservation energy{};
  energy.slot_complete = true;
  energy.energy_detected = true;
  energy.peak_dbfs = -42.0f;
  if (!hunter.finish_slot(energy) || hunter.current_band() != 5) return false;

  if (!hunter.mark_tuned() || !hunter.begin_slot()) return false;
  HunterObservation signature{};
  signature.slot_complete = true;
  signature.energy_detected = true;
  signature.sync_candidates = 4;
  signature.peak_dbfs = -55.0f;
  if (!hunter.finish_slot(signature)) return false;

  return hunter.snapshot().phase == HunterPhase::complete &&
         hunter.best_band() == 5 &&
         hunter.snapshot().results[3].evidence == HunterEvidence::energy &&
         hunter.snapshot().results[5].evidence == HunterEvidence::signature;
}

}  // namespace orcsdr::ft8
