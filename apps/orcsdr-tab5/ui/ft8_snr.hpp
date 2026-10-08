#pragma once

#include "ft8_codec.hpp"
#include "ft8_mode.hpp"
#include "ft8_sync.hpp"

namespace orcsdr::ftx::snr {

// Signal-to-noise estimate for a decoded message, in the usual weak-signal convention: signal power over noise power in a
// 2500 Hz bandwidth. It is only computed for a CRC-valid message, so the transmitted tones are known: the decoded bits are
// re-encoded, and for every data symbol the energy at the transmitted tone is compared with the energy at the other tones
// (empty apart from noise). It is not a detector and never runs on a candidate that failed the gates.
struct Calibration {
  // Added to the result in dB. The default is the value fitted against signals of known strength (tools/ft8-snr-sweep.cpp);
  // an advanced Settings control may change it.
  float offset_db = 0.5f;   // fitted: the raw estimate reads about 0.5 dB low across -20..+12 dB on FT8 test signals
};

// A user-set trim in dB added on top of the fitted calibration (advanced Settings). Zero by default; thread-safe.
void set_user_offset_db(float offset_db);
float user_offset_db();

constexpr float kFloorDb = -30.0f;   // below this the estimate is reported as the floor, never as a smaller number

// message_bits is the 91-bit LDPC message exactly as decoded (for FT4, before the payload XOR is undone).
// Returns false when the grid does not cover the frame or the energies are unusable.
bool estimate(const ModeProfile& profile, const sync::EnergyGrid& grid, const sync::Geometry& geometry,
              const sync::Candidate& candidate, const orcsdr::ft8::codec::MessageBits& message_bits,
              const Calibration& calibration, float* snr_db);

bool self_check();

}  // namespace orcsdr::ftx::snr
