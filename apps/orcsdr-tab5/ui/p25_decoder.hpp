#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::p25decoder {

constexpr size_t kRecentGrantCount = 8;

struct Grant {
  bool valid = false;
  bool encrypted = false;
  bool emergency = false;
  bool tdma = false;
  uint16_t talkgroup = 0;
  uint32_t source_id = 0;
  uint32_t frequency_hz = 0;
  uint32_t seen_ms = 0;
};

struct Snapshot {
  bool frame_sync = false;
  bool identity_valid = false;
  uint16_t nac = 0;
  uint32_t wacn = 0;
  uint16_t system_id = 0;
  uint8_t rfss = 0;
  uint8_t site = 0;
  uint32_t sync_words = 0;
  uint32_t nid_good = 0;
  uint32_t nid_failed = 0;
  uint32_t nid_corrected_bits = 0;
  uint32_t tsbk_good = 0;
  uint32_t tsbk_failed = 0;
  uint16_t last_trellis_metric = 0;
  float estimated_ber_percent = 0.0f;
  float frame_error_percent = 0.0f;
  float afc_offset_hz = 0.0f;
  float symbol_level = 0.0f;
  Grant current_grant{};
  Grant recent_grants[kRecentGrantCount]{};
};

// The decoder is single-writer: call reset/process_cu8 from the RTL delivery
// task. snapshot() is safe from the UI task.
void reset();
void process_cu8(const uint8_t* iq, size_t bytes);
Snapshot snapshot();

// Deterministic protocol/FEC check. It does not touch live decoder state.
bool self_check();

}  // namespace orcsdr::p25decoder
