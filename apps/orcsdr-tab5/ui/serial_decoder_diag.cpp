#include "serial_decoder_diag.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_decoder_diag {

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
  if (strcmp(command, "RTL_ACARS STATUS") == 0) {
    char line[900];
    hooks.acars_describe(line, sizeof(line));
    hooks.write_line(line);
    hooks.acars_format_status(line, sizeof(line));
    hooks.write_line(line);
    return true;
  }
  if (strcmp(command, "RTL_AIS STATUS") == 0) {
    constexpr size_t kLine = 1400;   // PSRAM, not the UI task's stack
    char* line = hooks.ais_line_alloc(kLine);
    if (!line) { hooks.write_line("RTL_AIS_STATUS error=no_memory"); return true; }
    hooks.ais_describe(line, kLine);
    hooks.write_line(line);
    hooks.ais_format_status(line, kLine);
    hooks.write_line(line);
    hooks.ais_line_free(line);
    say(hooks, "RTL_AIS_TASK dsp_stack_hwm=%u\n", hooks.ais_dsp_stack_hwm());
    return true;
  }
  if (strcmp(command, "RTL_DROPLOG") == 0) {
    const uint32_t count = hooks.droplog_count();
    say(hooks, "RTL_DROPLOG now_ms=%u stream_started_ms=%u total_drops=%u first_%u:",
        static_cast<unsigned>(hooks.droplog_now_ms()), static_cast<unsigned>(hooks.droplog_stream_started_ms()),
        static_cast<unsigned>(count),
        static_cast<unsigned>(count < hooks.droplog_capacity ? count : hooks.droplog_capacity));
    for (uint32_t i = 0; i < count && i < hooks.droplog_capacity; ++i)
      say(hooks, " %u(f%u)", static_cast<unsigned>(hooks.droplog_entry_ms(i)),
          static_cast<unsigned>(hooks.droplog_entry_filled(i)));
    hooks.write("\n");
    if (strcmp(command, "RTL_DROPLOG") == 0 && count) {}   // read only; RTL_DROPLOG CLEAR resets it
    return true;
  }
  if (strcmp(command, "RTL_DROPLOG CLEAR") == 0) { hooks.droplog_clear(); hooks.write_line("RTL_DROPLOG cleared"); return true; }
  if (strcmp(command, "RTL_AIS BENCH") == 0) {
    if (!authenticated) { hooks.write_line("RTL_AIS_BENCH error=auth_required"); return true; }
    char line[400];
    hooks.ais_bench(line, sizeof(line));
    hooks.write_line(line);
    return true;
  }
  // Bench replay marker: every AIS page then says the data is replayed, not live RF. Arm it BEFORE opening AIS and
  // pointing the receiver at the replay source; it is cleared when the screen is left and by TEST OFF.
  if (strcmp(command, "RTL_AIS TEST ON") == 0 || strcmp(command, "RTL_AIS TEST OFF") == 0) {
    if (!authenticated) { hooks.write_line("RTL_AIS_TEST_ERR auth_required"); return true; }
    const bool on = command[14] == 'N';   // may be sent before AIS opens: the label is armed and applies from the first block
    hooks.ais_set_test_replay(on);
    say(hooks, "RTL_AIS_TEST_OK test=%d\n", on ? 1 : 0);
    return true;
  }
  // Known-signal check of this build's ACARS decoder, on a private decoder instance (live reception is not touched).
  if (strcmp(command, "RTL_ACARS SELFTEST") == 0 && !authenticated) {
    hooks.write_line("RTL_ACARS_SELFTEST_ERROR auth_required");
    return true;
  }
  if (strcmp(command, "RTL_ACARS SELFTEST") == 0) {
    const AcarsSelfTest r = hooks.acars_selftest();
    say(hooks, "RTL_ACARS_SELFTEST allocated=%d cases=%u passed=%u missed=%u duplicates=%u samples=%u dsp_us=%u "
               "us_per_ksample=%u result=%s\n",
        r.allocated ? 1 : 0, r.cases, r.passed, r.missed, r.duplicates, static_cast<unsigned>(r.samples),
        static_cast<unsigned>(r.total_us), r.samples ? static_cast<unsigned>(r.total_us / (r.samples / 1000u)) : 0u,
        r.allocated && r.passed == r.cases ? "PASS" : "FAIL");
    return true;
  }
  // Archive check against the real SD card, in a scratch folder (the real log is not touched).
  if (strcmp(command, "RTL_ACARS ARCHIVETEST") == 0 && !authenticated) {
    hooks.write_line("RTL_ACARS_ARCHIVETEST_ERROR auth_required");
    return true;
  }
  if (strcmp(command, "RTL_ACARS ARCHIVETEST") == 0) {
    char line[200];
    hooks.acars_archive_test(line, sizeof(line));
    hooks.write_line(line);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_decoder_diag
