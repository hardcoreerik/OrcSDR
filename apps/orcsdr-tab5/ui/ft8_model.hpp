#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8 {

constexpr uint32_t kSlotMs = 15000;
constexpr uint16_t kAudioLowHz = 200;
constexpr uint16_t kAudioHighHz = 3000;
constexpr size_t kDecodeCapacity = 64;
constexpr size_t kVisibleDecodeRows = 8;

// The weak-signal modes the dashboard can present. FT8 is the default and the only mode the existing backend contract
// implies; the others are selectable only when the bound decoder reports support for them. JS8 60 is experimental: its
// specification is unpublished, so it is carried only as a clearly marked profile.
enum class DigitalMode : uint8_t {
  ft8,
  ft4,
  js8_normal,
  js8_fast,
  js8_40,
  js8_slow,
  js8_60_experimental,
  count
};
constexpr size_t kDigitalModeCount = static_cast<size_t>(DigitalMode::count);

// Provenance a decode can carry. None of these changes what DecodeKind means (CQ / QSO / free text ...).
enum DecodeFlags : uint16_t {
  decode_flag_none = 0,
  decode_flag_assisted = 1u << 0,        // a-priori (AP) assisted: not a plain over-the-air decode
  decode_flag_hash_resolved = 1u << 1,   // a callsign was resolved from a receiver-side hash table
  decode_flag_multi_frame = 1u << 2,     // assembled from several frames
  decode_flag_snr_unavailable = 1u << 3, // the decoder has no calibrated SNR estimate: show a dash, never a number
  decode_flag_new_station = 1u << 4,     // first time this callsign appears in the session store (set by DecodeStore::append)
};

uint32_t slot_ms(DigitalMode mode);          // FT8 15000, FT4 7500, JS8 Normal 15000, Fast 10000, 40 6000, Slow 30000, 60 4000
const char* mode_name(DigitalMode mode);     // at most 10 characters
bool mode_experimental(DigitalMode mode);
bool mode_is_js8(DigitalMode mode);
bool valid_mode(uint8_t value);

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
  uint32_t period_ms = kSlotMs;   // the selected mode's slot length
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
  // Appended after the original fields so existing positional initialisation keeps working.
  DigitalMode mode = DigitalMode::ft8;
  uint16_t flags = decode_flag_none;
};

struct GeoPoint {
  float latitude = 0.0f;
  float longitude = 0.0f;
  uint8_t precision = 0;
};

size_t band_count();
// The dial (USB) frequency to tune for a band in a given mode: FT4 sits on its own frequencies, FT8 on the band table's. JS8 is
// disabled, so it falls back to the FT8 dial. Returns 0 for an invalid band.
uint32_t mode_dial_hz(size_t band_index, DigitalMode mode);
const BandPreset* band(size_t index);
size_t nearest_band(uint32_t dial_hz);
SlotClock slot_clock(uint64_t utc_ms, DigitalMode mode = DigitalMode::ft8);
DecodeKind classify_message(const char* message);
bool maidenhead_valid(const char* locator);
bool maidenhead_center(const char* locator, GeoPoint* out);
// Great-circle distance (km, spherical Earth, R = 6371 km) and initial compass bearing (degrees clockwise from true
// north, 0-360) from one point to another. Fails on non-finite or out-of-range coordinates. The result is as good as
// the grid-square centre it is computed from: a 4-character locator is a 2 x 1 degree cell, so roughly +-100 km.
bool distance_bearing(const GeoPoint& from, const GeoPoint& to, float* distance_km, float* bearing_deg);
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
