// Checks the band-plan lookup: the right label at known frequencies, the most specific entry wins, unknown frequencies
// say so, and every name fits beside the event detail buttons.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "dead_air_bandplan.hpp"

using namespace orcsdr::dead_air;

int main() {
  assert(std::string(band_name(98700000u)) == "FM broadcast");
  assert(std::string(band_name(121500000u)) == "Airband voice");
  assert(std::string(band_name(145500000u)) == "2 m amateur");
  assert(std::string(band_name(162475000u)) == "NOAA weather radio (US)");
  assert(std::string(band_name(161975000u)) == "Marine VHF");
  assert(std::string(band_name(433920000u)) == "433 MHz remotes and sensors");  // inside 70 cm, the narrow entry wins
  assert(std::string(band_name(435000000u)) == "70 cm amateur");
  assert(std::string(band_name(1090000000u)) == "Aircraft transponders, ADS-B");
  assert(std::string(band_name(7100000u)) == "40 m amateur");
  assert(std::string(band_name(7350000u)) == "Shortwave broadcast");
  assert(std::string(band_name(27185000u)) == "Citizens band (CB)");
  // Edges are inclusive; just outside is not the band.
  assert(band_for(87500000u) && band_for(108000000u));
  assert(band_for(87499999u) == nullptr);
  // Nothing is invented for a frequency with no listed allocation.
  assert(band_for(300000000u) == nullptr);
  assert(std::string(band_name(300000000u)) == "no common allocation listed");
  assert(band_for(0u) == nullptr);
  // 108 MHz is the shared edge of FM broadcast and navigation: the first listed (FM) wins, deterministically.
  assert(std::string(band_name(108000000u)) == "FM broadcast");
  // Every label fits the space beside the detail-box buttons (30 characters at 12 px each).
  for (uint32_t hz = 500000u; hz < 1800000000u; hz += 97000u) {
    const Band* b = band_for(hz);
    if (b) assert(std::strlen(b->name) <= 30 && b->low_hz < b->high_hz);
  }
  std::puts("Dead Air band plan tests PASS");
  return 0;
}
