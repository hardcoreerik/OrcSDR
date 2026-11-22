#include "serial_pocsag.hpp"

#include <strings.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_pocsag {

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
  if (strcmp(command, "RTL_POCSAG STATUS") == 0) {
    hooks.print_status();
    return true;
  }
  if (strcmp(command, "RTL_POCSAG_SCAN") == 0 && authenticated) {
    if (!hooks.on_pocsag_band()) {
      hooks.write_line("RTL_POCSAG_SCAN_INVALID POCSAG band only");
      return true;
    }
    hooks.request_scan();
    hooks.write_line("RTL_POCSAG_SCAN_QUEUED");
    return true;
  }
  if (strcmp(command, "RTL_POCSAG_SCAN_STOP") == 0 && authenticated) {
    hooks.request_scan_stop();
    hooks.write_line("RTL_POCSAG_SCAN_STOP_QUEUED");
    return true;
  }
  if (strncmp(command, "RTL_POCSAG_SET_BAUD ", 20) == 0 && authenticated) {
    const char* arg = command + 20;
    uint16_t baud_bps;
    if (strcasecmp(arg, "AUTO") == 0) baud_bps = 0;
    else if (strcmp(arg, "512") == 0) baud_bps = 512;
    else if (strcmp(arg, "1200") == 0) baud_bps = 1200;
    else if (strcmp(arg, "2400") == 0) baud_bps = 2400;
    else {
      hooks.write_line("RTL_POCSAG_SET_BAUD_INVALID use AUTO|512|1200|2400");
      return true;
    }
    hooks.set_baud(baud_bps);
    say(hooks, "RTL_POCSAG_SET_BAUD_OK baud=%s\n", baud_bps == 0 ? "AUTO" : arg);
    return true;
  }
  if (strncmp(command, "RTL_POCSAG_SET_POLARITY ", 24) == 0 && authenticated) {
    const char* arg = command + 24;
    uint8_t mode;
    if (strcasecmp(arg, "AUTO") == 0) mode = 0;
    else if (strcasecmp(arg, "NORMAL") == 0) mode = 1;
    else if (strcasecmp(arg, "INVERTED") == 0) mode = 2;
    else {
      hooks.write_line("RTL_POCSAG_SET_POLARITY_INVALID use AUTO|NORMAL|INVERTED");
      return true;
    }
    hooks.set_polarity(mode);
    say(hooks, "RTL_POCSAG_SET_POLARITY_OK polarity=%s\n",
        mode == 0 ? "AUTO" : mode == 1 ? "NORMAL" : "INVERTED");
    return true;
  }
  if (strncmp(command, "RTL_POCSAG_TUNE ", 16) == 0 && authenticated) {
    char* end = nullptr;
    const unsigned long parsed = strtoul(command + 16, &end, 10);
    if (end == command + 16 || parsed == 0) {
      hooks.write_line("RTL_POCSAG_TUNE_INVALID usage: RTL_POCSAG_TUNE <HZ>");
      return true;
    }
    // The hook clamps to the band, records the result as POCSAG's own frequency (the dashboard header and the
    // discovery scan's restore target read it) and then queues the same band switch RTL_TUNE uses.
    const uint32_t clamped = hooks.tune(static_cast<uint32_t>(parsed));
    say(hooks, "RTL_POCSAG_TUNE_OK frequency_hz=%lu\n", static_cast<unsigned long>(clamped));
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_pocsag
