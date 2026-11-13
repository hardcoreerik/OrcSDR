#include "serial_tune.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_tune {

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
  if (strncmp(command, "RTL_TUNE ", 9) == 0 && authenticated) {
    char band_name[16] = {0};
    unsigned long freq_hz = 0;
    if (sscanf(command + 9, "%15s %lu", band_name, &freq_hz) != 2) {
      hooks.write_line("RTL_TUNE_INVALID usage: RTL_TUNE <BAND> <HZ>");
      return true;
    }
    const char* band_label = "";
    switch (hooks.tune(band_name, static_cast<uint32_t>(freq_hz), &band_label)) {
      case TuneResult::unknown_band:
        hooks.write_line("RTL_TUNE_INVALID unknown band (FM|AM|WX|CB|LORA|BROWSE|ADSB|P25)");
        return true;
      case TuneResult::not_ready:
        hooks.write_line("RTL_TUNE_UNAVAILABLE device not ready");
        return true;
      case TuneResult::not_queued:
        return true;
      case TuneResult::ok:
        break;
    }
    say(hooks, "RTL_TUNE_OK band=%s frequency_hz=%lu\n", band_label, freq_hz);
    return true;
  }
  if (strcmp(command, "RTL_FREQ") == 0) {
    const FreqStatus status = hooks.freq_status();
    say(hooks, "RTL_FREQ_STATUS band=%s frequency_hz=%u mode=%s\n", status.band, static_cast<unsigned>(status.frequency_hz), status.mode);
    return true;
  }
  if (strncmp(command, "RTL_FREQ ", 9) == 0 && authenticated) {
    const unsigned long freq_hz = strtoul(command + 9, nullptr, 10);
    if (freq_hz == 0) {
      hooks.write_line("RTL_FREQ_INVALID usage: RTL_FREQ <HZ>");
      return true;
    }
    if (!hooks.hot_retune(static_cast<uint32_t>(freq_hz))) {
      say(hooks, "RTL_FREQ_REJECTED band=%s frequency_hz=%lu\n", hooks.ui_band_name(), freq_hz);
      return true;
    }
    say(hooks, "RTL_FREQ_OK band=%s frequency_hz=%lu\n", hooks.ui_band_name(), freq_hz);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_tune
