#include "serial_gain.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_gain {

namespace {

// One formatted line, one write: what Serial.printf did for these replies (its buffer is 2048 bytes).
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[2048];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_GAIN STATUS") == 0) {
    hooks.print_status();
    return true;
  }
  if (strncmp(command, "RTL_GAIN ", 9) == 0) {
    // Same paths as the Home gain popup, for the current band.
    if (!authenticated) {
      hooks.write_line("RTL_GAIN_ERROR auth_required");
      return true;
    }
    const char* action = command + 9;
    if (strcmp(action, "AUTO") == 0 || strcmp(action, "SMART") == 0) {
      hooks.set_auto();
    } else if (strcmp(action, "MANUAL") == 0 || strncmp(action, "MANUAL ", 7) == 0) {
      int gain = 0;
      if (action[6] == ' ') {
        char* end = nullptr;
        const long value = strtol(action + 7, &end, 10);
        if (end == action + 7 || *end != '\0' || value < 0 || value > 496) {
          hooks.write_line("RTL_GAIN_INVALID use MANUAL [0..496]");
          return true;
        }
        gain = static_cast<int>(value);
      } else {
        gain = hooks.current_gain();  // hold the current gain
      }
      hooks.set_manual(gain);
    } else if (strcmp(action, "RTLAGC ON") == 0 || strcmp(action, "RTLAGC OFF") == 0) {
      hooks.set_rtl_agc(strcmp(action + 7, "ON") == 0);
    } else {
      hooks.write_line("RTL_GAIN_INVALID use STATUS|AUTO|SMART|MANUAL [0..496]|RTLAGC ON|OFF");
      return true;
    }
    hooks.refresh_ui();
    return true;
  }
  if (strcmp(command, "RTL_AM_GAIN STATUS") == 0) {
    const AutoStatus status = hooks.am_status();
    say(hooks,
        "RTL_AM_GAIN_STATUS mode=%s selecting=%d gain_tenth_db=%d "
        "target_dbfs=%.1f clipping_percent=%.3f\n",
        status.automatic ? "AUTO" : "MANUAL", status.selecting ? 1 : 0, status.gain_tenth_db, status.target_dbfs,
        status.clipping_percent);
    return true;
  }
  if (strcmp(command, "RTL_FM_GAIN STATUS") == 0) {
    const AutoStatus status = hooks.fm_status();
    say(hooks,
        "RTL_FM_GAIN_STATUS mode=%s selecting=%d gain_tenth_db=%d "
        "target_dbfs=%.1f clipping_percent=%.3f\n",
        status.automatic ? "AUTO" : "MANUAL", status.selecting ? 1 : 0, status.gain_tenth_db, status.target_dbfs,
        status.clipping_percent);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_gain
