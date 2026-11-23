#include "serial_radio.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_radio {
namespace {

void say(const Hooks& hooks, const char* format, ...) {
  char line[256];
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  // The LoRa test and DSP lab builds accept RTL_STOP signed out (compile-time constants in main.cpp).
  if (strcmp(command, "RTL_STOP") == 0 &&
      (authenticated || hooks.stop_without_sign_in)) {
    hooks.write_line(hooks.stop() ? "RTL_STOPPING" : "RTL_STOP_RESULT ESP_OK");
    return true;
  }
  const bool continuous_command = strncmp(command, "RTL_LISTEN", 10) == 0;
  const bool kzel_capture = strcmp(command, "RTL_CAPTURE") == 0 ||
                            strcmp(command, "RTL_CAPTURE KZEL") == 0 ||
                            strcmp(command, "RTL_LISTEN KZEL") == 0 ||
                            strcmp(command, "RTL_LISTEN FM") == 0;
  const bool noaa_capture = strcmp(command, "RTL_CAPTURE NOAA") == 0 ||
                            strcmp(command, "RTL_LISTEN NOAA") == 0 ||
                            strcmp(command, "RTL_LISTEN WX") == 0;
  const bool am_capture = strcmp(command, "RTL_CAPTURE AM") == 0 ||
                          strcmp(command, "RTL_LISTEN AM") == 0;
  const bool lora_capture = strcmp(command, "RTL_CAPTURE LORA") == 0 ||
                            strcmp(command, "RTL_LISTEN LORA") == 0;
  if ((kzel_capture || noaa_capture || am_capture || lora_capture) && authenticated) {
    const Band band =
        noaa_capture ? Band::wx
        : am_capture ? Band::am
        : lora_capture ? Band::lora
                       : Band::fm;
    Queued queued;
    if (!hooks.queue_capture(band, continuous_command, &queued)) {
      hooks.write_line("RTL_CAPTURE_BUSY_OR_UNAVAILABLE");
      return true;
    }
    say(hooks,
        "RTL_CAPTURE_QUEUED band=%s frequency_hz=%u sample_rate_sps=%u "
        "bytes=%u continuous=%s volume=%u\n",
        queued.band_name, static_cast<unsigned>(queued.frequency_hz), static_cast<unsigned>(queued.sample_rate_sps),
        static_cast<unsigned>(queued.bytes), continuous_command ? "true" : "false", queued.volume);
    return true;
  }
  if (strncmp(command, "RTL_VOLUME ", 11) == 0 && authenticated) {
    const int value = atoi(command + 11);
    if (value < hooks.volume_min || value > hooks.volume_max) {
      hooks.write_line("RTL_VOLUME_INVALID");
      return true;
    }
    hooks.set_volume(static_cast<uint8_t>(value));
    say(hooks, "RTL_VOLUME_OK volume=%u\n", static_cast<unsigned>(value));
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_radio
