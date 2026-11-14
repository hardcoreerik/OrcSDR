#include "serial_bench.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::serial_bench {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_SD_SELF_CHECK") == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_SD_SELF_CHECK_ERROR auth_required");
      return true;
    }
    hooks.sd_self_check();
    return true;
  }
  if (strncmp(command, "RTL_SD_BENCH", 12) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_SD_BENCH_ERROR auth_required");
      return true;
    }
    unsigned file_mib = 32;
    if (command[12] != '\0' &&
        (command[12] != ' ' || sscanf(command + 13, "%u", &file_mib) != 1 ||
         file_mib < 4 || file_mib > 64)) {
      hooks.write_line("RTL_SD_BENCH_INVALID use RTL_SD_BENCH [4..64 MiB]");
      return true;
    }
    hooks.sd_bench(file_mib);
    return true;
  }
  if (strcmp(command, "RTL_WIFI_C6_POWER OFF") == 0) {
    // Test hook (#106): drop the C6 rail to simulate a dead SDIO link.
    if (!authenticated) { hooks.write_line("RTL_WIFI_C6_POWER_ERROR auth_required"); return true; }
    hooks.c6_power_off();
    hooks.write_line("RTL_WIFI_C6_POWER off");
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_bench
