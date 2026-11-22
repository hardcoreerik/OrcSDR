#include "serial_driver.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_driver {

namespace {

// One formatted line, one write: what Serial.printf did for these replies (its buffer is 2048 bytes).
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[2048];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_DRIVER SELF_CHECK") == 0) {
    const SelfCheck check = hooks.self_check();
    say(hooks,
        "RTL_DRIVER_SELF_CHECK pass=%d version=%s profile=%u "
        "device_caps=0x%08x required=0x%08x\n",
        check.pass ? 1 : 0, check.version, check.profile, check.device_caps, check.required);
    return true;
  }
  if (strncmp(command, "RTL_DRIVER ", 11) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_DRIVER_ERROR auth_required");
      return true;
    }
    if (!hooks.installed()) {
      hooks.write_line("RTL_DRIVER_ERROR not_installed");
      return true;
    }
    Outcome result;
    const char* action = command + 11;
    if (strcmp(action, "OFFSET ON") == 0 || strcmp(action, "OFFSET OFF") == 0) {
      const bool enabled = strcmp(action, "OFFSET ON") == 0;
      result = hooks.offset_tuning(enabled);
    } else if (strcmp(action, "GAINMODE AUTO") == 0)
      result = hooks.gain_mode(true);
    else if (strcmp(action, "GAINMODE MANUAL") == 0)
      result = hooks.gain_mode(false);
    else if (strncmp(action, "GAIN ", 5) == 0) {
      char* end = nullptr;
      const long gain = strtol(action + 5, &end, 10);
      if (end == action + 5 || *end != '\0' || gain < -10 || gain > 496) {
        hooks.write_line("RTL_DRIVER_INVALID use GAIN <-10..496>; driver selects nearest supported step");
        return true;
      }
      result = hooks.gain(static_cast<int>(gain));
    } else if (strcmp(action, "RTLAGC ON") == 0 || strcmp(action, "RTLAGC OFF") == 0)
      result = hooks.rtl_agc(strcmp(action + 7, "ON") == 0);
    else if (strcmp(action, "BIAS ON") == 0 || strcmp(action, "BIAS OFF") == 0)
      result = hooks.bias_tee(strcmp(action + 5, "ON") == 0);
    else if (strncmp(action, "HFDIRECT ", 9) == 0) {
      // V4L route override for the next start: AUTO (CB direct), OFF, or a min Hz.
      const char* value = action + 9;
      uint32_t override_hz = 0;
      if (strcmp(value, "AUTO") == 0) override_hz = UINT32_MAX;
      else if (strcmp(value, "OFF") == 0) override_hz = 0;
      else {
        char* end = nullptr;
        const unsigned long hz = strtoul(value, &end, 10);
        if (end == value || *end != '\0' || hz < hooks.hf_direct_min_hz || hz >= hooks.hf_upconv_lo_hz) {
          hooks.write_line("RTL_DRIVER_INVALID use HFDIRECT AUTO|OFF|<24000000..28799999>");
          return true;
        }
        override_hz = static_cast<uint32_t>(hz);
      }
      result = hooks.hf_direct(override_hz);
    }
    else if (strncmp(action, "BW ", 3) == 0) {
      // Queues the request only; read back with RTL_DRIVER STATUS
      // (bw_requested_hz / bw_applied_hz). 0 = automatic.
      char* end = nullptr;
      const unsigned long bandwidth_hz = strtoul(action + 3, &end, 10);
      if (end == action + 3 || *end != '\0') {
        hooks.write_line("RTL_DRIVER_INVALID use BW <hz|0>");
        return true;
      }
      result = hooks.bandwidth(static_cast<uint32_t>(bandwidth_hz));
    }
    else if (strncmp(action, "TUNE ", 5) == 0) {
      char* end = nullptr;
      const unsigned long frequency_hz = strtoul(action + 5, &end, 10);
      if (end == action + 5 || *end != '\0' || frequency_hz < hooks.freq_min_hz || frequency_hz > hooks.freq_max_hz) {
        hooks.write_line("RTL_DRIVER_INVALID use TUNE <24000..1766000000>");
        return true;
      }
      result = hooks.retune(static_cast<uint32_t>(frequency_hz));
    }
    else {
      hooks.write_line("RTL_DRIVER_INVALID use STATUS|SELF_CHECK|TUNE <HZ>|OFFSET ON|OFF|HFDIRECT AUTO|OFF|<24000000..28799999>|GAINMODE AUTO|MANUAL|GAIN <-10..496>|RTLAGC ON|OFF|BIAS ON|OFF|BW <hz|0>");
      return true;
    }
    say(hooks, "RTL_DRIVER_RESULT action=\"%s\" accepted=%d result=%s\n", action, result.accepted ? 1 : 0, result.name);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_driver
