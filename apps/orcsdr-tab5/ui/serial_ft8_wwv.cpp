#include "serial_ft8_wwv.hpp"

#include <strings.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_ft8_wwv {

using orcsdr::serial_ft8::refuse_signed_out;
using orcsdr::serial_ft8::say;

bool handle(const char* verb, const char* rest, bool authenticated, const Hooks& hooks) {
  if (strcmp(verb, "WWV") != 0) return false;

  State status;
  hooks.state(&status);
  if (*rest == '\0') {
    say(hooks.io, "ORC_FT8_WWV_STATUS phase=%u seconds=%u action=%u mhz=%.1f\n", status.phase, status.seconds,
        static_cast<unsigned>(status.action), status.mhz);
    return true;
  }
  if (refuse_signed_out(hooks.io, verb, authenticated)) return true;
  if (strcasecmp(rest, "STOP") == 0) {
    hooks.stop();
    hooks.io.write_line("ORC_FT8_WWV_STOP requested");
    return true;
  }
  char* end = nullptr;
  const float mhz = strtof(rest, &end);
  unsigned long seconds = 90;
  Action action = Action::report;
  const char* p = end;
  while (*p == ' ') ++p;
  if (*p >= '0' && *p <= '9') {
    seconds = strtoul(p, &end, 10);
    p = end;
    while (*p == ' ') ++p;
  }
  if (strcasecmp(p, "CAL") == 0) action = Action::calibrate;
  else if (strcasecmp(p, "SYNC") == 0) action = Action::sync;
  // The key rounds to the nearest supported frequency, so require the typed value to be that frequency (10.04 must not tune 40 kHz off).
  if (end == rest || !hooks.frequency_supported(mhz) || std::fabs(mhz * 10.0f - std::round(mhz * 10.0f)) > 0.01f || seconds < 20 || seconds > 240 ||
      (*p != '\0' && action == Action::report)) {
    hooks.io.write_line("ORC_FT8_ERROR WWV invalid use WWV <2.5|5|10|15|20> [20-240 seconds] [CAL|SYNC] | WWV STOP | WWV");
    return true;
  }
  if (!hooks.decoder_active()) {
    hooks.io.write_line("ORC_FT8_ERROR WWV the decoder is not running (FT8 RUN 1 or open the FT8 screen)");
    return true;
  }
  if (status.phase == 1 || status.action != Action::none) {
    hooks.io.write_line("ORC_FT8_ERROR WWV a measurement is already running (FT8 WWV STOP)");
    return true;
  }
  const uint32_t dial = static_cast<uint32_t>(mhz * 1000000.0f + 0.5f) - 1000u;   // 1 kHz below the carrier: the carrier is then a line at 1000 Hz
  if (!hooks.start(mhz, dial, static_cast<uint32_t>(seconds), action)) {
    hooks.io.write_line("ORC_FT8_ERROR WWV could not start (tuning refused or no memory)");
    return true;
  }
  say(hooks.io, "ORC_FT8_WWV_START mhz=%.1f dial_hz=%lu seconds=%lu action=%u (about %lu s with the retune; the band comes back by itself)\n",
      static_cast<double>(mhz), static_cast<unsigned long>(dial), seconds, static_cast<unsigned>(action), seconds + 12);
  return true;
}

}  // namespace orcsdr::serial_ft8_wwv
