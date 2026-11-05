#pragma once
// Candidate scan centers, not a regulatory allocation table or complete band coverage.
// An adapter must filter these through detected tuner limits and antenna configuration.
// 2.4 GHz devices are deliberately excluded from RTL-SDR tuner profiles.
#include <cstddef>
#include <cstdint>

namespace orcsdr::iot {

enum class Region : uint8_t { north_america, europe };
enum ModulationHint : uint8_t { ook_ask = 1, fsk = 2, lora = 4 };

struct ScanTarget {
  uint32_t center_hz;
  uint16_t dwell_seconds;
  uint8_t modulation_hints;
};

struct ScanProfile {
  const ScanTarget* targets;
  size_t count;
};

// Long enough to encounter some frequent burst devices, but cannot guarantee discovery
// of sensors sending every few minutes. Keep cycling in a real runtime.
constexpr ScanTarget kNorthAmerica[] = {
    {315000000u, 12, ook_ask | fsk},
    {345000000u, 12, ook_ask | fsk},
    {433920000u, 30, ook_ask | fsk},
    {902500000u, 12, fsk | lora},
    {906000000u, 12, fsk | lora},
    {909500000u, 12, fsk | lora},
    {913000000u, 12, fsk | lora},
    {916500000u, 12, fsk | lora},
    {920000000u, 12, fsk | lora},
    {923500000u, 12, fsk | lora},
    {927000000u, 12, fsk | lora},
};

constexpr ScanTarget kEurope[] = {
    {433920000u, 30, ook_ask | fsk},
    {868100000u, 20, fsk | lora},
    {868300000u, 20, fsk | lora},
    {868500000u, 20, fsk | lora},
    {868950000u, 20, fsk | lora},
    {869525000u, 20, fsk | lora},
};

constexpr ScanProfile profile(Region region) {
  return region == Region::europe
      ? ScanProfile{kEurope, sizeof(kEurope) / sizeof(kEurope[0])}
      : ScanProfile{kNorthAmerica, sizeof(kNorthAmerica) / sizeof(kNorthAmerica[0])};
}

} // namespace orcsdr::iot
