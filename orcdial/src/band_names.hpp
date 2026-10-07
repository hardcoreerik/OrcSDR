#pragma once
#include <cstdint>

namespace orc {
// Display names for Packet::band (the Tab5's band profile id + 1). The Dial holds no band data beyond these
// labels: the Tab5 resolves the band, mode and step, and the Dial only shows what it is told. The order must
// match orcsdr::band_profile::Id; the OrcSDR host tests compare each name with the profile's.
constexpr const char* band_names[] = {
    "HF", "CB RADIO", "10 M AMATEUR", "6 M AMATEUR", "FM BROADCAST", "AIR NAV", "AIRBAND",
    "NOAA SATELLITE", "2 M AMATEUR", "MARINE RADIO", "NOAA WEATHER", "1.25 M AMATEUR",
    "DISTRESS BEACON", "70 CM AMATEUR", "FRS / GMRS", "POCSAG PAGER", "UHF", "VHF", "LORA / ISM",
    "ADS-B UAT", "ADS-B / MODE S", "SATCOM", "GNSS / GPS", "SATCOM", "L-BAND +",
};
constexpr int band_name_count = sizeof band_names / sizeof band_names[0];
inline const char* band_name(uint8_t band) {
  return band >= 1 && band <= band_name_count ? band_names[band - 1] : "RADIO";
}
} // namespace orc
