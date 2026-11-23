#include "serial_digimodes.hpp"

#include <cstring>

namespace orcsdr::serial_digimodes {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  (void)authenticated;   // the JS8 and FT8 parsers apply their own rules
  if (strncmp(command, "JS8", 3) == 0 && (command[3] == '\0' || command[3] == ' ')) {
    hooks.js8(command + 3);
    return true;
  }
  if (strncmp(command, "FT8", 3) == 0 && (command[3] == '\0' || command[3] == ' ')) {
    hooks.ft8(command + 3);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_digimodes
