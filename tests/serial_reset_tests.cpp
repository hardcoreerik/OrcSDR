// Host tests for the reset and USB safe-mode routes moved out of main.cpp (serial_reset.cpp). Expected behaviour is
// typed from the code this replaced. The restarts are fakes that only record the call.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_reset_tests.cpp apps/orcsdr-tab5/ui/serial_reset.cpp
#include <cstdio>
#include <string>

#include "serial_reset.hpp"

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::string g_out;
bool g_safe_mode = false;

const orcsdr::serial_reset::Hooks kHooks = [] {
  orcsdr::serial_reset::Hooks h;
  h.write = [](const char* t) { g_out += t; };
  h.write_line = [](const char* t) { g_out += std::string(t) + "\n"; };
  h.restart_clean = [] { g_out += "<restart_clean>"; };
  h.restart_cpu = [] { g_out += "<restart_cpu>"; };
  h.usb_safe_mode_active = [] { return g_safe_mode; };
  h.usb_safe_mode_reset_and_restart = [] { g_out += "<safe_mode_reset>"; };
  return h;
}();

std::string run(const char* line, bool authenticated, bool* handled = nullptr) {
  g_out.clear();
  const bool h = orcsdr::serial_reset::handle_command(line, authenticated, kHooks);
  if (handled) *handled = h;
  return g_out;
}

}  // namespace

int main() {
  // Signed out: refused, nothing restarts.
  CHECK(run("RTL_RESET", false) == "RTL_RESET_ERROR auth_required\n");
  CHECK(run("RTL_RESET CPU", false) == "RTL_RESET_ERROR auth_required\n");
  CHECK(run("RTL_USB_SAFE_MODE_RESET CONFIRM", false) == "RTL_USB_SAFE_MODE_RESET_ERROR auth_required\n");
  // Signed in.
  CHECK(run("RTL_RESET", true) == "RTL_RESETTING\n<restart_clean>");
  CHECK(run("RTL_RESET CPU", true) == "RTL_RESETTING cpu\n<restart_cpu>");
  CHECK(run("RTL_USB_SAFE_MODE_RESET CONFIRM", true) == "<safe_mode_reset>");
  // Status needs no sign-in.
  g_safe_mode = false;
  CHECK(run("RTL_USB_SAFE_MODE_STATUS", false) == "RTL_USB_SAFE_MODE_STATUS active=0\n");
  g_safe_mode = true;
  CHECK(run("RTL_USB_SAFE_MODE_STATUS", true) == "RTL_USB_SAFE_MODE_STATUS active=1\n");
  // Near misses are not these routes.
  bool handled = true;
  for (const char* line : {"RTL_RESET ", "RTL_RESETX", "RTL_RESET cpu", "RTL_USB_SAFE_MODE_RESET",
                           "RTL_USB_SAFE_MODE_RESET CONFIRM ", "RTL_USB_SAFE_MODE_STATUS ", "rtl_reset"}) {
    CHECK(run(line, true, &handled).empty() && !handled);
  }
  if (failures == 0) std::printf("serial_reset: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
