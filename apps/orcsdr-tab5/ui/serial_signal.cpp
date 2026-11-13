#include "serial_signal.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_signal {

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

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_SIGNAL") == 0) {
    const Signal s = hooks.signal();
    say(hooks,
        "RTL_SIGNAL_STATUS band=%s frequency_hz=%u signal_dbfs_tenths=%d "
        "stereo_locked=%d left_dbfs_tenths=%d right_dbfs_tenths=%d "
        "rds_carrier=%d rds_signal_tenths=%d pilot_env_thou=%d clipping_percent=%.3f "
        "filter_hz=%u lo_nudge=%d\n",
        s.band, s.frequency_hz, s.signal_tenths, s.stereo_locked ? 1 : 0, s.left_tenths, s.right_tenths,
        s.rds_carrier ? 1 : 0, s.rds_tenths, s.pilot_env_thou, static_cast<double>(s.clipping_percent), s.filter_hz,
        0);
    return true;
  }
  if (strcmp(command, "RTL_PRESET_SCAN") == 0 && authenticated) {
    if (!hooks.band_is_fm()) {
      hooks.write_line("RTL_PRESET_SCAN_INVALID FM band only");
      return true;
    }
    hooks.request_preset_scan();
    hooks.write_line("RTL_PRESET_SCAN_QUEUED");
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_signal
