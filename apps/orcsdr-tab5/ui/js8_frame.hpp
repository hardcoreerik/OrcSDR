#pragma once

#include "js8_mode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::js8 {

constexpr size_t kChannelSymbols = 79;
constexpr size_t kDataSymbols = 58;

struct RawFrame {
  Submode submode = Submode::normal;
  std::array<uint8_t, kChannelSymbols> tones{};
};

struct DataTones {
  std::array<uint8_t, kDataSymbols> tones{};
};

bool tones_valid(const RawFrame& frame);
bool sync_matches(const RawFrame& frame);
bool extract_data_tones(const RawFrame& frame, DataTones* out);
bool self_check_frame();

}  // namespace orcsdr::js8
