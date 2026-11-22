#include "serial_scope.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_scope {

namespace {

// One formatted line, one write: what Serial.printf did for these replies.
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[192];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  (void)authenticated;
  if (strncmp(command, "RTL_CB", 6) == 0 && (command[6] == '\0' || command[6] == ' ')) {
    hooks.citizens_band(command);
    return true;
  }
  if (strcmp(command, "RTL_FM_SCOPE") == 0) {   // frame rate of the FM spectrum view (0 when not showing)
    const FmScope scope = hooks.fm_scope();
    say(hooks, "RTL_FM_SCOPE_STATUS active=%d fps=%lu draw_ms=%lu\n", scope.active ? 1 : 0,
        static_cast<unsigned long>(scope.fps), static_cast<unsigned long>(scope.draw_ms));
    return true;
  }
  if (strcmp(command, "RTL_AIRBAND STATUS") == 0) {
    char line[640];
    if (hooks.airband_status(line, sizeof(line)) > 0) hooks.write_line(line);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_scope
