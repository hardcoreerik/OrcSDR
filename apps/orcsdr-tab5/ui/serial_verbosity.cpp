#include "serial_verbosity.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_verbosity {

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
  if (strcmp(command, "RTL_SERIAL VERBOSITY") == 0) {
    const uint8_t level = hooks.current();
    say(hooks, "RTL_SERIAL_VERBOSITY mode=%s\n", hooks.name(level));
    return true;
  }
  if (strncmp(command, "RTL_SERIAL VERBOSITY ", 21) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_SERIAL_VERBOSITY_ERROR auth_required");
      return true;
    }
    uint8_t level = 0;
    if (!hooks.parse(command + 21, &level)) {
      hooks.write_line("RTL_SERIAL_VERBOSITY_INVALID use QUIET|NORMAL|DEBUG|TRACE");
      return true;
    }
    hooks.save_and_apply(level);
    say(hooks, "RTL_SERIAL_VERBOSITY_OK mode=%s\n", hooks.name(level));
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_verbosity
