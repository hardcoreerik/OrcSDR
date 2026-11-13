#include "serial_rds.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_rds {

namespace {

// One formatted line, one write: what Serial.printf did for this reply, with the same 2048-byte limit.
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
  (void)authenticated;
  if (strcmp(command, "RTL_RDS_CAPTURE_START") == 0) {
    hooks.capture_start();
    return true;
  }
  if (strcmp(command, "RTL_RDS_CAPTURE_STOP") == 0 ||
      strcmp(command, "RTL_RDS_CAPTURE_SAVE") == 0) {
    hooks.capture_stop_and_export();
    return true;
  }
  if (strcmp(command, "RTL_RDS_CAPTURE_STATUS") == 0) {
    hooks.capture_status_print();
    return true;
  }
  if (strncmp(command, "RTL_RDS_REPLAY ", 15) == 0) {
    hooks.replay(command + 15);
    return true;
  }
#if ORCSDR_DSP_AB
  // Same replay through the old per-sample feed, for A/B against the block feed.
  if (strncmp(command, "RTL_RDS_REPLAY_SAMPLE ", 22) == 0) {
    hooks.replay_per_sample(command + 22);
    return true;
  }
#endif
  if (strcmp(command, "RTL_RDS_STATUS") == 0) {
    const Status status = hooks.status();
    const float bler = status.total_blocks > 0
                           ? 100.0f * (1.0f - static_cast<float>(status.good_blocks) /
                                                   static_cast<float>(status.total_blocks))
                           : 100.0f;
    say(hooks,
        "RDS_STATUS carrier=%d carrier_signal=%.1f block_locked=%d bler=%.1f%% "
        "good=%lu total=%lu hyp0_streak=%d hyp1_streak=%d "
        "pilot_pll=%d ps=\"%s\" rt=\"%s\" pi=%04X "
        "timing_chip_rate=%.2f timing_correction_ppm=%.1f "
        "nco_freq_off=%.6f i_lpf=%.2f q_lpf=%.2f mu=%.3f "
        "A=%04x B=%04x C=%04x D=%04x driver_overruns=%u driver_drops=%u "
        "effective_sps=%u audio_chunks=%u audio_drops=%u\n",
        status.carrier_present ? 1 : 0,
        static_cast<double>(status.carrier_signal_dbfs),
        status.block_locked, static_cast<double>(bler),
        static_cast<unsigned long>(status.good_blocks),
        static_cast<unsigned long>(status.total_blocks), status.parity_streak[0],
        status.parity_streak[1],
        status.pilot_pll_locked ? 1 : 0, status.ps, status.rt,
        status.pi,
        static_cast<double>(status.chip_rate),
        static_cast<double>(status.chip_rate_correction_ppm),
        0.0,
        static_cast<double>(status.i_lpf),
        static_cast<double>(status.q_lpf),
        static_cast<double>(status.chip_phase),
        status.group_info[0], status.group_info[1], status.group_info[2], status.group_info[3],
        static_cast<unsigned>(status.driver_overruns), static_cast<unsigned>(status.driver_drops),
        static_cast<unsigned>(status.effective_sps),
        static_cast<unsigned>(status.audio_chunks), static_cast<unsigned>(status.audio_drops));
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_rds
