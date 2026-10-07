#pragma once
#include <cstdint>

namespace orc {
// Wire IDs are explicit and match the current Tab5 registry. Utilities is a menu, not a screen.
enum class Dashboard : uint8_t {
  home=0, fm=1, p25=2, adsb=3, shortwave=4, weather=5, cb=6,
  lora=7, airband=8, marine=9, satellite=10, utilities=11,
  settings=12, rf_lab=13, wifi_analysis=14, pocsag=15, am=16
};
// Local menu token, never a receiver dashboard ID or valid wire command.
constexpr Dashboard devices_entry=static_cast<Dashboard>(255);
constexpr Dashboard carousel[] = {
  Dashboard::home, Dashboard::fm, Dashboard::am, Dashboard::weather,
  Dashboard::airband, Dashboard::marine, Dashboard::cb, Dashboard::adsb,
  Dashboard::satellite, Dashboard::lora, Dashboard::rf_lab, Dashboard::p25,
  Dashboard::shortwave, Dashboard::pocsag, Dashboard::wifi_analysis, Dashboard::settings, devices_entry
};
constexpr int carousel_count = sizeof(carousel) / sizeof(carousel[0]);
inline bool valid_dashboard(uint8_t id) { return id <= 16 && id != 11; }
inline const char* dashboard_name(Dashboard id) {
  if (id == devices_entry) return "DIAL SETTINGS"; // not an enumerator, so it cannot be a case label (-Werror=switch)
  switch (id) {
    case Dashboard::home: return "HOME";
    case Dashboard::fm: return "FM RADIO";
    case Dashboard::am: return "AM RADIO";
    case Dashboard::weather: return "WEATHER";
    case Dashboard::airband: return "AIRBAND";
    case Dashboard::marine: return "MARINE";
    case Dashboard::cb: return "CB RADIO";
    case Dashboard::adsb: return "ADS-B";
    case Dashboard::satellite: return "SATELLITE";
    case Dashboard::lora: return "LORA / MESH";
    case Dashboard::rf_lab: return "RF LAB";
    case Dashboard::p25: return "P25 RADIO";
    case Dashboard::shortwave: return "SHORTWAVE";
    case Dashboard::pocsag: return "POCSAG";
    case Dashboard::wifi_analysis: return "2.4G WIFI";
    case Dashboard::settings: return "SETTINGS";
    default: return "UNKNOWN";
  }
}
inline bool tunable(Dashboard id) {
  return id == Dashboard::fm || id == Dashboard::am || id == Dashboard::shortwave ||
         id == Dashboard::airband || id == Dashboard::satellite || id == Dashboard::rf_lab;
}
inline int carousel_index(Dashboard id) {
  for (int i=0; i<carousel_count; ++i) if (carousel[i] == id) return i;
  return 0;
}
} // namespace orc
