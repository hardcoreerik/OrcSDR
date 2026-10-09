#pragma once

#include "js8_frame.hpp"
#include "js8_sync.hpp"

namespace orcsdr::js8::snr {

struct Calibration {
  // Synthetic-fixture correction only until real reference pairs validate it.
  float offset_db = 0.0f;
};

constexpr float kFloorDb = -30.0f;

// Estimate signal/noise in the conventional 2500 Hz weak-signal bandwidth.
// `frame` must represent the already-validated transmitted tone sequence;
// production callers must invoke this only after FEC + CRC acceptance.
bool estimate(const Profile& profile, const sync::EnergyGrid& grid,
              const sync::Geometry& geometry,
              const sync::Candidate& candidate,
              const RawFrame& frame,
              const Calibration& calibration,
              float* snr_db);

bool self_check();

}  // namespace orcsdr::js8::snr
