#include "serial_ui_status.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_ui_status {

namespace {

// One formatted line, one write: what Serial.printf did for this reply.
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[512];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  (void)authenticated;
  if (strcmp(command, "RTL_UI STATUS") == 0) {
    const UiStatus s = hooks.ui_status();
    say(hooks, "RTL_UI_STATUS screen=%s band=%s frequency_hz=%u settings=%d fm=%d am=%d p25=%d "
               "adsb=%d lora=%d rf24=%d home_font=%d graphics=%d radio_ui=%d route=%s volume=%u audio_enabled=%d tray=%d\n",
        s.screen, s.band, static_cast<unsigned>(s.frequency_hz), s.settings, s.fm, s.am, s.p25, s.adsb, s.lora,
        s.rf24, s.home_font, s.graphics, s.radio_ui, s.route, static_cast<unsigned>(s.volume), s.audio_enabled,
        s.tray);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_ui_status
