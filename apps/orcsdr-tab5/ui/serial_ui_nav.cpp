#include "serial_ui_nav.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_ui_nav {

namespace {

// One formatted line, one write: what Serial.printf did for these replies.
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[512];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

struct NamedTarget {
  const char* name;
  Target target;
};

constexpr NamedTarget kTargets[] = {
    {"HOME", Target::home},
    {"CLOCK", Target::clock},
    {"FM", Target::fm},
    {"AM", Target::am},
    {"SHORTWAVE", Target::shortwave},
    {"AIRBAND", Target::airband},
    {"CB", Target::cb},
    {"P25", Target::p25},
    {"ADSB", Target::adsb},
    {"LORA", Target::lora},
    {"RF_LAB", Target::rf_lab},
    {"WIFI_ANALYSIS", Target::wifi_analysis},
    {"FT8", Target::ft8},
    {"SETTINGS", Target::settings},
    {"WEATHER", Target::weather},
    {"POCSAG", Target::pocsag},
    {"ACARS", Target::acars},
    {"DEAD_AIR", Target::dead_air},
    {"AIS", Target::ais},
    {"VISUALIZER", Target::visualizer},
    {"MARINE", Target::marine},
    {"SATELLITE", Target::satellite},
    {"UTILITIES", Target::utilities},
};

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  // Serial input mimics the screen: RTL_UI TAP x y goes through the same routing as a finger on the panel (full-screen
  // coordinates, header included).
  if (strncmp(command, "RTL_UI TAP ", 11) == 0 && !authenticated) {
    hooks.write_line("RTL_UI_TAP_ERROR auth_required");
    return true;
  }
  if (strncmp(command, "RTL_UI TAP ", 11) == 0) {
    int x = 0, y = 0;
    if (sscanf(command + 11, "%d %d", &x, &y) != 2 || x < 0 || y < 0 || x >= 1280 || y >= 720) {
      hooks.write_line("RTL_UI_TAP_INVALID use RTL_UI TAP <x 0-1279> <y 0-719>");
      return true;
    }
    hooks.touch(x, y);
    say(hooks, "RTL_UI_TAP_OK x=%d y=%d\n", x, y);
    return true;
  }
  if (strncmp(command, "RTL_UI OPEN ", 12) == 0 && !authenticated) {
    hooks.write_line("RTL_UI_OPEN_ERROR auth_required");
    return true;
  }
  if (strncmp(command, "RTL_UI OPEN ", 12) == 0) {
    const char* name = command + 12;
    bool known = false;
    for (const NamedTarget& entry : kTargets) {
      if (strcmp(name, entry.name) == 0) {
        hooks.open(entry.target);
        known = true;
        break;
      }
    }
    if (!known) {
      hooks.write_line("RTL_UI_OPEN_INVALID use HOME|FM|AM|SHORTWAVE|P25|ADSB|LORA|RF_LAB|WIFI_ANALYSIS|FT8|SETTINGS|WEATHER|CB|POCSAG|AIRBAND|ACARS|DEAD_AIR|AIS|VISUALIZER|MARINE|SATELLITE|UTILITIES");
      return true;
    }
    say(hooks, "RTL_UI_OPEN_OK target=%s\n", name);
    return true;
  }
  if (strncmp(command, "RTL_RF24_PAGE ", 14) == 0 && !authenticated) {
    hooks.write_line("RTL_RF24_PAGE_ERROR auth_required");
    return true;
  }
  if (strncmp(command, "RTL_RF24_PAGE ", 14) == 0) {
    unsigned page = 0;
    if (sscanf(command + 14, "%u", &page) != 1 || page >= hooks.rf24_page_count()) {
      hooks.write_line("RTL_RF24_PAGE_INVALID use 0-4");
      return true;
    }
    if (!hooks.rf24_select_page(static_cast<uint8_t>(page))) {
      hooks.write_line("RTL_RF24_PAGE_ERROR dashboard_inactive");
      return true;
    }
    say(hooks, "RTL_RF24_PAGE_OK page=%u\n", page);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_ui_nav
