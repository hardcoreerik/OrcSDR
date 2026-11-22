#pragma once
// RTL_DISPLAY_BENCH [1-10]: screen-copy benchmark (display_bench.hpp). Signed-in only: it holds the main loop for
// several seconds. Portable; the device action comes in through Hooks.
namespace orcsdr::serial_display_bench {

struct Hooks {
  void (*write_line)(const char* text) = nullptr;
  void (*run)(unsigned seconds) = nullptr;
};

bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_display_bench
