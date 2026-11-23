#include "dead_air_bandplan.hpp"

#include <cstddef>

namespace orcsdr::dead_air {
namespace {
// Narrow, specific entries come before the wide ones that contain them: the first match wins.
constexpr Band kBands[] = {
    {162400000u, 162550000u, "NOAA weather radio (US)"},
    {433050000u, 434790000u, "433 MHz remotes and sensors"},
    {446000000u, 446200000u, "PMR446 walkie-talkies (EU)"},
    {462562500u, 467712500u, "FRS / GMRS radios (US)"},
    {868000000u, 868600000u, "868 MHz devices (EU)"},
    {902000000u, 928000000u, "900 MHz ISM (US)"},
    {1089000000u, 1091000000u, "Aircraft transponders, ADS-B"},
    {1574000000u, 1577000000u, "GPS L1"},
    {1690000000u, 1710000000u, "Weather satellite downlink"},
    {26965000u, 27405000u, "Citizens band (CB)"},
    {137000000u, 138000000u, "Weather satellites (APT)"},
    {156000000u, 162025000u, "Marine VHF"},
    {144000000u, 148000000u, "2 m amateur"},
    {50000000u, 54000000u, "6 m amateur"},
    {87500000u, 108000000u, "FM broadcast"},
    {108000000u, 117975000u, "Aviation navigation (VOR, ILS)"},
    {118000000u, 137000000u, "Airband voice"},
    {174000000u, 240000000u, "DAB radio and TV band III"},
    {430000000u, 440000000u, "70 cm amateur"},
    {470000000u, 790000000u, "UHF television"},
    {960000000u, 1215000000u, "Aviation radio navigation"},
    {530000u, 1710000u, "AM broadcast (medium wave)"},
    {1800000u, 2000000u, "160 m amateur"},
    {3500000u, 4000000u, "80 m amateur"},
    {7000000u, 7300000u, "40 m amateur"},
    {10100000u, 10150000u, "30 m amateur"},
    {14000000u, 14350000u, "20 m amateur"},
    {18068000u, 18168000u, "17 m amateur"},
    {21000000u, 21450000u, "15 m amateur"},
    {24890000u, 24990000u, "12 m amateur"},
    {28000000u, 29700000u, "10 m amateur"},
    {5900000u, 6200000u, "Shortwave broadcast"},
    {7200000u, 7450000u, "Shortwave broadcast"},
    {9400000u, 9900000u, "Shortwave broadcast"},
    {11600000u, 12100000u, "Shortwave broadcast"},
    {13570000u, 13870000u, "Shortwave broadcast"},
    {15100000u, 15800000u, "Shortwave broadcast"},
    {17480000u, 17900000u, "Shortwave broadcast"},
    {21450000u, 21850000u, "Shortwave broadcast"},
    {25670000u, 26100000u, "Shortwave broadcast"},
};
}  // namespace

const Band* band_for(uint32_t hz) {
  for (const Band& band : kBands)
    if (hz >= band.low_hz && hz <= band.high_hz) return &band;
  return nullptr;
}

const char* band_name(uint32_t hz) {
  const Band* band = band_for(hz);
  return band ? band->name : "no common allocation listed";
}

}  // namespace orcsdr::dead_air
