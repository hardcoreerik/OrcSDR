#include "serial_splash.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::serial_splash {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strncmp(command, "RTL_SPLASH_GATE", 15) == 0) {
    const char* argument = command[15] == ' ' ? command + 16 : command + 15;
    if (strcmp(argument, "ON") == 0 || strcmp(argument, "OFF") == 0) {
      if (!authenticated) {
        hooks.write_line("RTL_SPLASH_GATE_ERROR auth_required");
        return true;
      }
      hooks.store_gate(strcmp(argument, "ON") == 0);
    } else if (argument[0] != '\0' && strcmp(argument, "STATUS") != 0) {
      hooks.write_line("RTL_SPLASH_GATE_INVALID use RTL_SPLASH_GATE [ON|OFF|STATUS]");
      return true;
    }
    char line[48];
    std::snprintf(line, sizeof(line), "RTL_SPLASH_GATE enabled=%d\n", hooks.gate() ? 1 : 0);
    hooks.write(line);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_splash
