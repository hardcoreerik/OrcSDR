#include "serial_rtc.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_rtc {

namespace {

// One formatted line, one write: what Serial.printf did for these replies.
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[512];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "ORC_RTC_STATUS") == 0) {
    const Clock clock = hooks.clock();
    say(hooks, "ORC_RTC_STATUS valid=%d utc=%lu source=%s offset_min=%ld\n", clock.valid ? 1 : 0,
        static_cast<unsigned long>(clock.utc), clock.hardware ? "hardware" : "unavailable",
        static_cast<long>(clock.offset_min));
    return true;
  }
  if (strncmp(command, "ORC_TZ_SET ", 11) == 0) {
    if (!authenticated) {
      hooks.write_line("ORC_TZ_SET_ERROR auth_required");
      return true;
    }
    char* end = nullptr;
    const long minutes = strtol(command + 11, &end, 10);
    if (end == command + 11 || *end != '\0' || !hooks.valid_offset(static_cast<int32_t>(minutes))) {
      hooks.write_line("ORC_TZ_SET_ERROR out_of_range");
      return true;
    }
    if (!hooks.store_offset(static_cast<int32_t>(minutes))) {
      hooks.write_line("ORC_TZ_SET_ERROR persistence_failed");
      return true;
    }
    say(hooks, "ORC_TZ_SET_OK offset_min=%ld\n", minutes);
    hooks.settings_changed();
    return true;
  }
  if (strncmp(command, "ORC_RTC_SET ", 12) == 0) {
    if (!authenticated) {
      hooks.write_line("ORC_RTC_SET_ERROR auth_required");
      return true;
    }
    char* end = nullptr;
    const unsigned long epoch = strtoul(command + 12, &end, 10);
    if (end == command + 12 || *end != '\0' || !hooks.set_utc(static_cast<uint32_t>(epoch))) {
      hooks.write_line("ORC_RTC_SET_ERROR invalid_or_unavailable");
      return true;
    }
    hooks.forget_clock_trust();   // set by hand: nothing vouches for its accuracy, so the sync status starts over (even if persisting below fails)
    if (!hooks.mark_estimated()) {
      hooks.write_line("ORC_RTC_SET_ERROR persistence_failed");
      return true;
    }
    say(hooks, "ORC_RTC_SET_OK utc=%lu\n", epoch);
    hooks.settings_changed();
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_rtc
