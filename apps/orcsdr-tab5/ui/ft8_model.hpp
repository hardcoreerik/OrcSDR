#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8 {

constexpr uint32_t kSlotMs = 15000;
constexpr uint16_t kAudioLowHz = 200;
constexpr uint16_t kAudioHighHz = 3000;
constexpr size_t kDecodeCapacity = 64;
constexpr size_t kVisibleDecodeRows = 8;

struct BandPreset {
  const char* label;
  uint32_t dial_hz;
  bool common;
};

struct SlotClock {
  uint32_t slot_index = 0;
  uint32_t elapsed_ms = 0;
  uint32_t remaining_ms = kSlotMs;
  bool first_half_minute = true;
};

enum class DecodeKind : uint8_t { cq, qso, free_text, unknown };

struct Decode {
  uint32_t utc_epoch = 0;
  int16_t snr_db = 0;
  int16_t dt_ms = 0;
  uint16_t audio_hz = 0;
  int16_t sync_score = 0;
  char message[48]{};
  char callsign[16]{};
  char grid[9]{};
  DecodeKind kind = DecodeKind::unknown;
};

struct GeoPoint {
  float latitude = 0.0f;
  float longitude = 0.0f;
  uint8_t precision = 0;
};

size_t band_count();
const BandPreset* band(size_t index);
size_t nearest_band(uint32_t dial_hz);
SlotClock slot_clock(uint64_t utc_ms);
DecodeKind classify_message(const char* message);
bool maidenhead_valid(const char* locator);
bool maidenhead_center(const char* locator, GeoPoint* out);
bool parse_cq_fields(const char* message, char* callsign, size_t callsign_size,
                     char* grid, size_t grid_size);
const char* kind_name(DecodeKind kind);
bool self_check();

class DecodeStore {
 public:
  void clear();
  void append(const Decode& decode);
  size_t size() const { return size_; }
  const Decode* newest(size_t offset = 0) const;
  size_t unique_calls() const;
  size_t cq_count() const;
  size_t grid_count() const;

 private:
  Decode records_[kDecodeCapacity]{};
  size_t size_ = 0;
};

}  // namespace orcsdr::ft8
