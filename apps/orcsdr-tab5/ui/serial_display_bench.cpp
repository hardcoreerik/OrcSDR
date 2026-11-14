#include "serial_display_bench.hpp"

#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_display_bench {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_DISPLAY_BENCH") == 0 || strncmp(command, "RTL_DISPLAY_BENCH ", 18) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_DISPLAY_BENCH_ERROR auth_required");
      return true;
    }
    unsigned seconds = 2;
    if (command[17] == ' ') {
      char* end = nullptr;
      const unsigned long value = strtoul(command + 18, &end, 10);
      if (end == command + 18 || *end != '\0' || value < 1 || value > 10) {
        hooks.write_line("RTL_DISPLAY_BENCH_ERROR usage=RTL_DISPLAY_BENCH [1-10]");
        return true;
      }
      seconds = static_cast<unsigned>(value);
    }
    hooks.run(seconds);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_display_bench
