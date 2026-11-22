#include "serial_dsp.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_dsp {

namespace {

// One formatted line, one write: what Serial.printf did for these replies. The buffer and the cut at its end are
// the console's own (2048 bytes), so the long RTL_DSP STATS line is limited exactly as it was.
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
  (void)authenticated;   // none of these routes checked it in the dispatcher
#if ORCSDR_DSP_LAB
  if (strncmp(command, "RTL_DSP LAB RATE ", 17) == 0) {
    if (hooks.lab_radio_running()) {
      hooks.write_line("LAB_RATE_ERROR radio_running");
      return true;
    }
    if (strcmp(command + 17, "DEFAULT") == 0) {
      hooks.lab_set_rate_override(0);
      hooks.write_line("LAB_RATE default");
      return true;
    }
    const uint32_t rate = strcmp(command + 17, "2560000") == 0 ? 2560000u
                          : strcmp(command + 17, "3200000") == 0 ? 3200000u : 0u;
    if (!rate || !hooks.lab_rate_usable(rate)) {
      hooks.write_line("LAB_RATE_ERROR use_2560000_3200000_or_DEFAULT");
      return true;
    }
    hooks.lab_set_rate_override(rate);
    if (!hooks.lab_queue_fm_listen()) {
      hooks.lab_set_rate_override(0);
      hooks.write_line("LAB_RATE_ERROR start_failed");
      return true;
    }
    say(hooks, "LAB_RATE queued=1 rate=%u band=FM\n", (unsigned)rate);
    return true;
  }
  if (strncmp(command, "RTL_DSP LAB", 11) == 0) {
    hooks.lab_command(command[11] == ' ' ? command + 12 : "");
    return true;
  }
#endif
#if ORCSDR_DSP_AB
  if (strncmp(command, "RTL_DSP AB CAPTURE ", 19) == 0) {
    const long blocks = strtol(command + 19, nullptr, 10);
    say(hooks, "RTL_DSP_AB_ARM blocks=%ld ok=%d\n", blocks,
        hooks.ab_arm(static_cast<size_t>(blocks)) ? 1 : 0);
    return true;
  }
  // Reset stress: n requests of rotating kinds from this (non-DSP) task with
  // 0-2 ms gaps while the radio streams. All must be served at block
  // boundaries (pending mask drains) and audio/RDS must keep running.
  if (strncmp(command, "RTL_DSP AB STORM ", 17) == 0) {
    const long n = strtol(command + 17, nullptr, 10);
    const Storm storm = hooks.ab_storm(n);
    say(hooks, "RTL_DSP_AB_STORM requests=%ld ms=%lu applied=%lu pending=%lu "
               "audio_chunks_500ms=%lu\n",
        n, static_cast<unsigned long>(storm.storm_ms), static_cast<unsigned long>(storm.applied),
        static_cast<unsigned long>(storm.pending),
        static_cast<unsigned long>(storm.audio_chunks_500ms));
    return true;
  }
  if (strcmp(command, "RTL_DSP AB RUN") == 0) {
    hooks.ab_request_run();
    hooks.write_line("RTL_DSP_AB_RUN queued");
    return true;
  }
#endif
  if (strcmp(command, "RTL_DSP STATS") == 0) {
    // Window since the previous RTL_DSP STATS (read-only, resets on read).
    char line[768];
    const Stats stats = hooks.stats(line, sizeof(line));
    say(hooks, "%s band=%s rate=%lu iq_pipeline_drops=%lu driver_overruns=%u "
               "driver_drops=%u audio_chunks=%lu audio_dropped_chunks=%lu "
               "audio_ring_overruns=%lu audio_submit_failures=%lu\n",
        line, stats.band,
        static_cast<unsigned long>(stats.rate),
        static_cast<unsigned long>(stats.iq_pipeline_drops),
        (unsigned)stats.driver_overruns, (unsigned)stats.driver_drops,
        static_cast<unsigned long>(stats.audio_chunks),
        static_cast<unsigned long>(stats.audio_dropped_chunks),
        static_cast<unsigned long>(stats.audio_ring_overruns),
        static_cast<unsigned long>(stats.audio_submit_failures));
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_dsp
