#include "serial_reset.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::serial_reset {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_RESET") == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_RESET_ERROR auth_required");
      return true;
    }
    hooks.write_line("RTL_RESETTING");
    hooks.restart_clean();
    return true;
  }
  if (strcmp(command, "RTL_RESET CPU") == 0) {
    // Diagnostic: the plain CPU-level restart, to exercise the boot-time normalisation (issue #127).
    if (!authenticated) {
      hooks.write_line("RTL_RESET_ERROR auth_required");
      return true;
    }
    hooks.write_line("RTL_RESETTING cpu");
    hooks.restart_cpu();
    return true;
  }
  if (strcmp(command, "RTL_USB_SAFE_MODE_STATUS") == 0) {
    char line[48];
    std::snprintf(line, sizeof(line), "RTL_USB_SAFE_MODE_STATUS active=%d\n", hooks.usb_safe_mode_active() ? 1 : 0);
    hooks.write(line);
    return true;
  }
  if (strcmp(command, "RTL_USB_SAFE_MODE_RESET CONFIRM") == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_USB_SAFE_MODE_RESET_ERROR auth_required");
      return true;
    }
    hooks.usb_safe_mode_reset_and_restart();
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_reset
