#pragma once
#include <cstdint>

namespace orc {
// Display names for Packet::band (the Tab5's band profile id + 1). The Dial holds no band data beyond these
// labels: the Tab5 resolves the band, mode and step, and the Dial only shows what it is told. The order must
// match orcsdr::band_profile::Id; the OrcSDR host tests compare each name with the profile's.
constexpr const char* band_names[] = {
    "HF", "CB RADIO", "10 M HAM", "6 M HAM", "FM RADIO", "AIR NAV", "AIR BAND", "WEATHER SAT", "2 M HAM",
    "MARINE", "WEATHER RADIO", "1.25 M HAM", "DISTRESS BEACON", "70 CM HAM", "FRS / GMRS", "PAGERS", "UHF",
    "VHF", "LORA / ISM", "ADS-B UAT", "ADS-B", "SATCOM", "GPS", "SATCOM", "L-BAND", "AM RADIO", "160 M HAM",
    "80 M HAM", "60 M HAM", "40 M HAM", "30 M HAM", "20 M HAM", "17 M HAM", "15 M HAM", "12 M HAM",
    "SHORTWAVE 120 M", "SHORTWAVE 90 M", "SHORTWAVE 60 M", "SHORTWAVE 49 M", "SHORTWAVE 41 M",
    "SHORTWAVE 31 M", "SHORTWAVE 25 M", "SHORTWAVE 22 M", "SHORTWAVE 19 M", "SHORTWAVE 16 M",
    "SHORTWAVE 13 M", "SHORTWAVE 11 M", "LOW BAND VHF", "VHF TV", "VHF TV", "VHF TV", "GOVT VHF", "GOVT VHF",
    "LAND MOBILE", "LAND MOBILE", "LAND MOBILE", "MIL AIR", "RADIOSONDE", "GOVT UHF", "LAND MOBILE",
    "LAND MOBILE", "UHF TV", "UHF TV", "700 MHZ", "800 MHZ", "CELLULAR", "800 MHZ", "CELLULAR", "900 MHZ",
    "900 MHZ", "AERO NAV", "AERO NAV", "AERO NAV", "GPS", "23 CM HAM", "HYDROGEN LINE", "GLONASS",
    "WEATHER SAT",
};
constexpr int band_name_count = sizeof band_names / sizeof band_names[0];
inline const char* band_name(uint8_t band) {
  return band >= 1 && band <= band_name_count ? band_names[band - 1] : "RADIO";
}
} // namespace orc
