#include "serial_dead_air.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_dead_air {

namespace {

// One formatted line, one write: what Serial.printf did for these replies.
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[96];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strncmp(command, "RTL_DEAD_AIR FLOOD ", 19) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_DEAD_AIR_FLOOD_ERROR auth_required");
      return true;
    }
    const uint32_t logged = hooks.flood_manual_marks(static_cast<uint32_t>(atoi(command + 19)));
    say(hooks, "RTL_DEAD_AIR_FLOOD logged=%lu\n", static_cast<unsigned long>(logged));
    return true;
  }
  if (strcmp(command, "RTL_DEAD_AIR STATUS") == 0) {
    char line[768];
    hooks.describe(line, sizeof(line));
    hooks.write_line(line);
    return true;
  }
  if (strcmp(command, "RTL_DEAD_AIR VEIL") == 0) {
    char line[560];
    hooks.describe_veil(line, sizeof(line));
    hooks.write_line(line);
    return true;
  }
  if (strncmp(command, "RTL_DEAD_AIR VEIL ", 18) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_DEAD_AIR_VEIL_ERROR auth_required");
      return true;
    }
    char reply[160];
    hooks.veil_command(command + 18, reply, sizeof(reply));
    hooks.write_line(reply);
    return true;
  }
  if (strcmp(command, "RTL_DEAD_AIR ENV") == 0) {
    char line[420];
    hooks.describe_env(line, sizeof(line));
    hooks.write_line(line);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_dead_air
