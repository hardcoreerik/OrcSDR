#pragma once
// Built-in scan ranges for the Digital Voice scanner. Pure data: the scanner turns one into a computed channel list (start + i * step).
// The raster matters: a digital voice channel must be within a few kHz of the tuned frequency, so each preset starts on the grid its band
// actually uses (verified against the Terra channels 152.1575 / 152.345 MHz and the 451.9125 MHz control channel seen in the field).
// Nothing here says a channel is occupied or what system it carries; it only says where to look.
#include <cstddef>
#include <cstdint>

namespace orcsdr::dv {

struct BandPreset {
  const char* name;        // shown on the SCAN screen (at most 29 characters)
  uint32_t start_hz;
  uint32_t end_hz;         // last channel, inclusive
  uint32_t step_hz;
};

inline constexpr BandPreset kBandPresets[] = {
    {"2M AMATEUR 144-148", 144000000u, 147987500u, 12500u},
    {"VHF BUSINESS 150-158", 150007500u, 157995000u, 12500u},
    {"VHF PUBLIC 158-174", 158007500u, 173995000u, 12500u},
    {"70CM AMATEUR 430-450", 430000000u, 449987500u, 12500u},
    {"UHF BUSINESS 450-470", 450000000u, 469987500u, 12500u},
    {"800 MHZ 806-824", 806012500u, 823987500u, 12500u},
};
inline constexpr size_t kBandPresetCount = sizeof(kBandPresets) / sizeof(kBandPresets[0]);

constexpr size_t channel_count(const BandPreset& p) { return static_cast<size_t>((p.end_hz - p.start_hz) / p.step_hz) + 1; }

static_assert(channel_count(kBandPresets[1]) == 640, "VHF business range is 640 channels at 12.5 kHz");
static_assert(channel_count(kBandPresets[4]) == 1600, "UHF business range is 1600 channels at 12.5 kHz");

}  // namespace orcsdr::dv
