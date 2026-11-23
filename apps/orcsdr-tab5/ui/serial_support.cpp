#include "serial_support.hpp"

#include <cstring>

namespace orcsdr::serial_support {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  (void)authenticated;
  if (strcmp(command, "RTL_SUPPORT REPORT") == 0) {
    if (hooks.snapshot != nullptr && hooks.write_line != nullptr) {
      const orcsdr::support_report::Snapshot s = hooks.snapshot();
      orcsdr::support_report::emit(s, hooks.write_line);
    }
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_support
