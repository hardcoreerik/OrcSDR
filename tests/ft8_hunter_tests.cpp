#include "../apps/orcsdr-tab5/ui/ft8_hunter.hpp"

#include <cassert>

int main() {
  using namespace orcsdr::ft8;
  assert(hunter_self_check());

  Hunter fast;
  const uint32_t mask =
      (uint32_t{1} << 3) | (uint32_t{1} << 5) | (uint32_t{1} << 7);
  assert(fast.start(HunterMode::fast, mask));
  assert(fast.snapshot().slots_per_band == 1);
  assert(fast.current_band() == 3);
  assert(fast.requested_frequency_hz() == 7074000);

  assert(fast.mark_tuned());
  assert(fast.begin_slot());
  HunterObservation quiet{};
  quiet.slot_complete = true;
  quiet.peak_dbfs = -100.0f;
  assert(fast.finish_slot(quiet));
  assert(fast.current_band() == 5);

  assert(fast.mark_tuned());
  assert(fast.begin_slot());
  HunterObservation energy{};
  energy.slot_complete = true;
  energy.energy_detected = true;
  energy.peak_dbfs = -45.0f;
  assert(fast.finish_slot(energy));
  assert(fast.best_band() == 5);

  assert(fast.mark_tuned());
  assert(fast.begin_slot());
  HunterObservation signature{};
  signature.slot_complete = true;
  signature.energy_detected = true;
  signature.sync_candidates = 3;
  signature.peak_dbfs = -70.0f;
  assert(fast.finish_slot(signature));
  assert(fast.snapshot().phase == HunterPhase::complete);
  assert(fast.best_band() == 7);
  assert(fast.snapshot().results[5].evidence == HunterEvidence::energy);
  assert(fast.snapshot().results[7].evidence == HunterEvidence::signature);

  Hunter deep;
  assert(deep.start(HunterMode::decode, uint32_t{1} << 5));
  assert(deep.snapshot().slots_per_band == 2);
  assert(deep.mark_tuned());
  assert(deep.begin_slot());
  assert(deep.mark_decoding());

  HunterObservation first{};
  first.slot_complete = true;
  first.energy_detected = true;
  first.sync_candidates = 8;
  first.valid_decodes = 4;
  first.best_snr_db = -9;
  first.peak_dbfs = -38.0f;
  assert(deep.finish_slot(first));
  assert(deep.snapshot().phase == HunterPhase::waiting_slot);

  assert(deep.begin_slot());
  assert(deep.mark_decoding());
  HunterObservation second{};
  second.slot_complete = true;
  second.energy_detected = true;
  second.sync_candidates = 6;
  second.valid_decodes = 7;
  second.best_snr_db = -4;
  second.peak_dbfs = -41.0f;
  assert(deep.finish_slot(second));
  assert(deep.snapshot().phase == HunterPhase::complete);
  assert(deep.snapshot().results[5].valid_decodes == 11);
  assert(deep.snapshot().results[5].sync_candidates == 14);
  assert(deep.snapshot().results[5].best_snr_db == -4);
  assert(deep.snapshot().results[5].evidence == HunterEvidence::decoded);

  Hunter stopped;
  assert(stopped.start(HunterMode::fast, uint32_t{1} << 5));
  stopped.stop();
  assert(stopped.snapshot().phase == HunterPhase::stopped);
  assert(!stopped.active());
  assert(!stopped.mark_tuned());

  Hunter none;
  assert(!none.start(HunterMode::fast, uint32_t{1} << 31));
  assert(none.snapshot().phase == HunterPhase::error);

  return 0;
}
