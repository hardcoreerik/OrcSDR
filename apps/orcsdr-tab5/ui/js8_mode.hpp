#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::js8 {

enum class Submode : uint8_t {
  normal,
  fast,
  turbo,
  slow,
  ultra_experimental,
  count,
};

enum class Readiness : uint8_t {
  physical_layer_verified,
  sync_pattern_pending,
  experimental,
};

struct SyncBlock {
  uint8_t first_symbol = 0;
  std::array<uint8_t, 7> tones{};
};

struct DataBlock {
  uint8_t first_symbol = 0;
  uint8_t length = 0;
};

struct Profile {
  Submode submode = Submode::normal;
  const char* name = "Normal";
  uint32_t sample_rate_hz = 12000;
  uint32_t slot_ms = 15000;
  uint32_t tx_duration_ms = 12640;
  uint16_t symbol_samples = 1920;
  uint16_t channel_symbols = 79;
  uint8_t data_symbols = 58;
  uint8_t tone_count = 8;
  uint32_t tone_spacing_millihz = 6250;
  std::array<SyncBlock, 3> sync{};
  std::array<DataBlock, 2> data{{DataBlock{7, 29}, DataBlock{43, 29}}};
  bool sync_pattern_verified = false;
  Readiness readiness = Readiness::sync_pattern_pending;
};

const Profile& profile(Submode submode);
bool valid_submode(uint8_t value);
bool physical_layer_ready(Submode submode);
bool self_check();

}  // namespace orcsdr::js8
