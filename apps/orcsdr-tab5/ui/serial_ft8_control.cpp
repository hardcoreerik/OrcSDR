#include "serial_ft8_control.hpp"

#include <strings.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_ft8_control {

using orcsdr::serial_ft8::refuse_signed_out;
using orcsdr::serial_ft8::say;

namespace {

constexpr const char* kVerbs[] = {"OPEN", "BAND", "MODE", "CLEAR", "RUN", "TAB", "HUNT", "DIAL", "CLOCKTEST", "NTP",
                                  "SNR", "MEM", "LOG", "CONFIG"};

bool is_control_verb(const char* verb) {
  for (const char* name : kVerbs)
    if (strcmp(verb, name) == 0) return true;
  return false;
}

}  // namespace

bool handle(const char* verb, const char* rest, bool authenticated, const Hooks& hooks) {
  if (!is_control_verb(verb)) return false;
  // Everything here changes state (or sat below the same gate in the original parser).
  if (refuse_signed_out(hooks.io, verb, authenticated)) return true;

  if (strcmp(verb, "OPEN") == 0) {
    hooks.open();
    hooks.io.write_line("ORC_FT8_OPEN_OK");
    return true;
  }

  if (strcmp(verb, "BAND") == 0) {
    size_t index = 0;
    if (!hooks.parse_band(rest, &index)) {
      hooks.io.write_line("ORC_FT8_ERROR BAND invalid use BAND <index|label|dial_hz> (see FT8 BANDS)");
      return true;
    }
    const bool ok = hooks.select_band(index);
    const char* label = "-";
    unsigned long dial_hz = 0;
    hooks.band_info(index, &label, &dial_hz);
    say(hooks.io, "ORC_FT8_BAND_%s index=%u label=%s dial_hz=%lu\n", ok ? "OK" : "FAILED", static_cast<unsigned>(index),
        label, dial_hz);
    return true;
  }

  if (strcmp(verb, "MODE") == 0) {
    Mode mode = Mode::ft8;
    if (strcasecmp(rest, "FT8") == 0) mode = Mode::ft8;
    else if (strcasecmp(rest, "FT4") == 0) mode = Mode::ft4;
    else {
      hooks.io.write_line("ORC_FT8_ERROR MODE invalid use MODE <FT8|FT4> (JS8 is disabled)");
      return true;
    }
    const char* name = "";
    unsigned long dial_hz = 0;
    hooks.set_mode(mode, &name, &dial_hz);
    say(hooks.io, "ORC_FT8_MODE_OK mode=%s dial_hz=%lu\n", name, dial_hz);
    return true;
  }

  if (strcmp(verb, "CLEAR") == 0) {
    hooks.clear();
    hooks.io.write_line("ORC_FT8_CLEAR_OK");
    return true;
  }

  if (strcmp(verb, "RUN") == 0) {
    const bool on = *rest == '1';
    if (!on && *rest != '0') {
      hooks.io.write_line("ORC_FT8_ERROR RUN invalid use RUN <0|1>");
      return true;
    }
    if (on) {
      if (!hooks.run_start()) {
        hooks.io.write_line("ORC_FT8_ERROR RUN start_failed");
        return true;
      }
    } else {
      hooks.run_stop();
    }
    say(hooks.io, "ORC_FT8_RUN_OK headless=%d\n", on ? 1 : 0);
    return true;
  }

  if (strcmp(verb, "TAB") == 0) {
    for (size_t i = 0; i < kTabCount; ++i) {
      if (strcasecmp(rest, kTabNames[i]) == 0) {
        hooks.select_tab(i);
        say(hooks.io, "ORC_FT8_TAB_OK tab=%s\n", kTabNames[i]);
        return true;
      }
    }
    hooks.io.write_line("ORC_FT8_ERROR TAB invalid use TAB <LIVE|DECODES|MAP|HUNTER|HEARD|SETUP>");
    return true;
  }

  if (strcmp(verb, "HUNT") == 0) {
    if (strcasecmp(rest, "FAST") == 0 || strcasecmp(rest, "DECODE") == 0) {
      const bool ok = hooks.hunt_start(strcasecmp(rest, "FAST") == 0);
      say(hooks.io, "ORC_FT8_HUNT_%s\n", ok ? "OK" : "FAILED");
      return true;
    }
    if (strcasecmp(rest, "STOP") == 0) {
      hooks.hunt_stop();
      hooks.io.write_line("ORC_FT8_HUNT_OK stopped");
      return true;
    }
    hooks.io.write_line("ORC_FT8_ERROR HUNT invalid use HUNT <FAST|DECODE|STOP>");
    return true;
  }

  if (strcmp(verb, "DIAL") == 0) {
    // Bench: tune an exact dial frequency in Hz (what the Live DIAL chip does with expert tuning), e.g. a time-signal station.
    // FT8 BAND <label> returns to the band table.
    char* dial_end = nullptr;
    const unsigned long hz = strtoul(rest, &dial_end, 10);
    while (dial_end != nullptr && *dial_end == ' ') ++dial_end;
    // The whole argument must be the number ("14074000oops" is rejected, not tuned as 14074000).
    if (hz == 0 || dial_end == rest || *dial_end != '\0' || !hooks.tune_dial(static_cast<uint32_t>(hz)))
      hooks.io.write_line("ORC_FT8_ERROR DIAL invalid use DIAL <hz> (0.5 MHz to the tuner limit)");
    return true;
  }

  if (strcmp(verb, "CLOCKTEST") == 0) {
    // Bench only: FT8 CLOCKTEST <ms> steps the clock by ms (positive = later), forgets the last sync and holds the automatic network sync
    // off, so the DT-based correction can be exercised. FT8 CLOCKTEST 0 releases the hold (then FT8 NTP, or the automatic sync, restores the clock).
    const long delta = strtol(rest, nullptr, 10);
    if (delta < -5000 || delta > 5000) {
      hooks.io.write_line("ORC_FT8_ERROR CLOCKTEST invalid use CLOCKTEST <-5000..5000 ms>");
      return true;
    }
    if (delta == 0) {
      hooks.clock_test_release();
      hooks.io.write_line("ORC_FT8_CLOCKTEST released");
      return true;
    }
    const bool ok = hooks.clock_test_step(delta);
    say(hooks.io, "ORC_FT8_CLOCKTEST %s step_ms=%ld\n", ok ? "OK" : "FAILED", delta);
    return true;
  }

  if (strcmp(verb, "NTP") == 0) {
    const bool started = hooks.ntp_start();
    say(hooks.io, "ORC_FT8_NTP_%s\n", started ? "OK started (see ORC_NTP_OK; then FT8 TIME)" : "FAILED (no Wi-Fi or already running)");
    return true;
  }

  if (strcmp(verb, "SNR") == 0) {
    // FT8 SNR [OFFSET <dB> | RESET]: the advanced trim added to the fitted SNR calibration (stored in NVS). Default 0.
    if (strncasecmp(rest, "OFFSET", 6) == 0) {
      const char* number = rest + 6;
      char* end = nullptr;
      const float value = strtof(number, &end);
      if (end == number) {
        hooks.io.write_line("ORC_FT8_ERROR SNR usage: SNR OFFSET <dB>");
        return true;
      }
      hooks.set_snr_offset(value);
    } else if (strncasecmp(rest, "RESET", 5) == 0) {
      hooks.reset_snr_offset();
    }
    say(hooks.io, "ORC_FT8_SNR user_offset_db=%.1f\n", hooks.snr_offset());
    return true;
  }

  if (strcmp(verb, "MEM") == 0) {
    // FT8 MEM [FULL]: internal/DMA/PSRAM heap budget (the SD card and Wi-Fi need internal DMA blocks); FULL adds the heap block map.
    hooks.memory_report(strncasecmp(rest, "FULL", 4) == 0);
    return true;
  }

  if (strcmp(verb, "LOG") == 0) {
    // FT8 LOG [ON|OFF]: the rolling decode log on the SD card (stations heard, one CSV per UTC day under /ft8/).
    hooks.log_load();
    if (strncasecmp(rest, "ON", 2) == 0 || strncasecmp(rest, "OFF", 3) == 0) {
      hooks.log_set_enabled(strncasecmp(rest, "ON", 2) == 0);
    } else if (strncasecmp(rest, "FLUSH", 5) == 0) {
      const bool done = hooks.log_flush();
      say(hooks.io, "ORC_FT8_LOG_FLUSH %s\n", done ? "ok" : "pending (check SD errors and DMA memory)");
    }
    LogStatus log;
    hooks.log_status(&log);
    say(hooks.io, "ORC_FT8_LOG enabled=%d written=%u staged=%u dropped=%u errors=%u dir=/orcsdr/ft8 dma_largest=%u need=%u\n",
        log.enabled ? 1 : 0, log.written, log.staged, log.dropped, log.errors, log.dma_largest, log.need);
    return true;
  }

  // CONFIG
  unsigned k = 0, gate = 0, fine = 0;
  unsigned long deadline = 0;
  if (sscanf(rest, "%u %u %u %lu", &k, &gate, &fine, &deadline) != 4 || !hooks.set_config(k, gate, fine, deadline)) {
    hooks.io.write_line("ORC_FT8_ERROR CONFIG invalid use CONFIG <k 1-64> <gate 1-64> <fine_rows 4|8> <deadline_ms>");
    return true;
  }
  say(hooks.io, "ORC_FT8_CONFIG_OK k=%u gate=%u fine_rows=%u deadline_ms=%lu\n", k, gate, fine, deadline);
  return true;
}

}  // namespace orcsdr::serial_ft8_control
