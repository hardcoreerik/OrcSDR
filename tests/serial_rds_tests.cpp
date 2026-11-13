// Host test for the RTL_RDS command family (serial_rds.cpp). The expected RDS_STATUS reply is the format string the
// dispatcher in main.cpp used before the family moved, filled in with the scripted device facts below.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_rds_tests.cpp and apps/orcsdr-tab5/ui/serial_rds.cpp
// Build it a second time with -DORCSDR_DSP_AB=1 for the RTL_RDS_REPLAY_SAMPLE route.
#include "serial_rds.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_rds;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write (it carries its own '\n'), "C:" is a hook call; one list, in the order they happened.
std::vector<std::string> g_log;
Status g_status;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_log.push_back(std::string("W:") + text); };
  hooks.capture_start = [] { g_log.push_back("C:capture_start"); };
  hooks.capture_stop_and_export = [] { g_log.push_back("C:capture_stop_and_export"); };
  hooks.capture_status_print = [] { g_log.push_back("C:capture_status"); };
  hooks.replay = [](const char* path) { g_log.push_back(std::string("C:replay [") + path + "]"); };
  hooks.status = []() -> Status {
    g_log.push_back("C:status");
    return g_status;
  };
#if ORCSDR_DSP_AB
  hooks.replay_per_sample = [](const char* path) { g_log.push_back(std::string("C:replay_per_sample [") + path + "]"); };
#endif
  return hooks;
}

void expect(int line, const std::vector<std::string>& expected) {
  if (g_log != expected) {
    ++failures;
    std::printf("FAIL line %d output:\n", line);
    for (size_t i = 0; i < g_log.size() || i < expected.size(); ++i) {
      std::printf("  got:  [%s]\n  want: [%s]\n", i < g_log.size() ? g_log[i].c_str() : "<nothing>",
                  i < expected.size() ? expected[i].c_str() : "<nothing>");
    }
  }
  g_log.clear();
}
#define EXPECT(...) expect(__LINE__, __VA_ARGS__)

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // None of these routes checks authentication: the same thing happens either way.
  for (bool authenticated : {false, true}) {
    CHECK(handle_command("RTL_RDS_CAPTURE_START", authenticated, hooks));
    EXPECT({"C:capture_start"});
    CHECK(handle_command("RTL_RDS_CAPTURE_STOP", authenticated, hooks));
    EXPECT({"C:capture_stop_and_export"});
    CHECK(handle_command("RTL_RDS_CAPTURE_SAVE", authenticated, hooks));
    EXPECT({"C:capture_stop_and_export"});
    CHECK(handle_command("RTL_RDS_CAPTURE_STATUS", authenticated, hooks));
    EXPECT({"C:capture_status"});

    // REPLAY passes on everything after the one space, untouched (an empty path too).
    CHECK(handle_command("RTL_RDS_REPLAY /sdcard/rds/kzel.mpx", authenticated, hooks));
    EXPECT({"C:replay [/sdcard/rds/kzel.mpx]"});
    CHECK(handle_command("RTL_RDS_REPLAY  two words ", authenticated, hooks));
    EXPECT({"C:replay [ two words ]"});
    CHECK(handle_command("RTL_RDS_REPLAY ", authenticated, hooks));
    EXPECT({"C:replay []"});

#if ORCSDR_DSP_AB
    // Only in the A/B build. It is not taken by the RTL_RDS_REPLAY route before it: that one needs a space at the
    // 15th character, where this command has '_'.
    CHECK(handle_command("RTL_RDS_REPLAY_SAMPLE /sdcard/rds/kzel.mpx", authenticated, hooks));
    EXPECT({"C:replay_per_sample [/sdcard/rds/kzel.mpx]"});
    CHECK(handle_command("RTL_RDS_REPLAY_SAMPLE ", authenticated, hooks));
    EXPECT({"C:replay_per_sample []"});
#else
    // Without the A/B harness the line is nobody's here; the dispatcher goes on to its later routes.
    CHECK(!handle_command("RTL_RDS_REPLAY_SAMPLE /sdcard/rds/kzel.mpx", authenticated, hooks));
    CHECK(!handle_command("RTL_RDS_REPLAY_SAMPLE ", authenticated, hooks));
    EXPECT({});
#endif
  }

  // STATUS: one read of the device facts, one write.
  g_status.carrier_present = true;
  g_status.carrier_signal_dbfs = -42.5f;
  g_status.block_locked = true;
  g_status.good_blocks = 75;
  g_status.total_blocks = 100;
  g_status.parity_streak[0] = 3;
  g_status.parity_streak[1] = -1;
  g_status.pilot_pll_locked = true;
  g_status.ps = "KZEL-FM ";
  g_status.rt = "Now playing";
  g_status.pi = 0x4b2a;
  g_status.chip_rate = 2374.98f;
  g_status.chip_rate_correction_ppm = -10.0f;
  g_status.i_lpf = 0.25f;
  g_status.q_lpf = -1.5f;
  g_status.chip_phase = 0.125f;
  g_status.group_info[0] = 0x4b2a;
  g_status.group_info[1] = 0x0408;
  g_status.group_info[2] = 0x00ab;
  g_status.group_info[3] = 0x2020;
  g_status.driver_overruns = 7;
  g_status.driver_drops = 2;
  g_status.effective_sps = 1920000;
  g_status.audio_chunks = 5;
  g_status.audio_drops = 1;
  CHECK(handle_command("RTL_RDS_STATUS", false, hooks));
  EXPECT({"C:status",
          "W:RDS_STATUS carrier=1 carrier_signal=-42.5 block_locked=1 bler=25.0% "
          "good=75 total=100 hyp0_streak=3 hyp1_streak=-1 "
          "pilot_pll=1 ps=\"KZEL-FM \" rt=\"Now playing\" pi=4B2A "
          "timing_chip_rate=2374.98 timing_correction_ppm=-10.0 "
          "nco_freq_off=0.000000 i_lpf=0.25 q_lpf=-1.50 mu=0.125 "
          "A=4b2a B=0408 C=00ab D=2020 driver_overruns=7 driver_drops=2 "
          "effective_sps=1920000 audio_chunks=5 audio_drops=1\n"});

  // Nothing received yet: no blocks means a block error rate of 100 %, not a division by zero.
  g_status = Status{};
  g_status.carrier_signal_dbfs = -90.0f;
  CHECK(handle_command("RTL_RDS_STATUS", true, hooks));
  EXPECT({"C:status",
          "W:RDS_STATUS carrier=0 carrier_signal=-90.0 block_locked=0 bler=100.0% "
          "good=0 total=0 hyp0_streak=0 hyp1_streak=0 "
          "pilot_pll=0 ps=\"\" rt=\"\" pi=0000 "
          "timing_chip_rate=0.00 timing_correction_ppm=0.0 "
          "nco_freq_off=0.000000 i_lpf=0.00 q_lpf=0.00 mu=0.000 "
          "A=0000 B=0000 C=0000 D=0000 driver_overruns=0 driver_drops=0 "
          "effective_sps=0 audio_chunks=0 audio_drops=0\n"});

  // The longest texts RDS can carry (8 and 64 characters) come through whole.
  const std::string rt64(64, 'R');
  g_status.ps = "ABCDEFGH";
  g_status.rt = rt64.c_str();
  g_status.good_blocks = 100;
  g_status.total_blocks = 100;
  CHECK(handle_command("RTL_RDS_STATUS", true, hooks));
  CHECK(g_log.size() == 2 && g_log[1].find("bler=0.0% ") != std::string::npos &&
        g_log[1].find("ps=\"ABCDEFGH\" rt=\"" + rt64 + "\" pi=0000 ") != std::string::npos && g_log[1].back() == '\n');
  g_log.clear();

  // Anything else is left to the dispatcher: exact matches stay exact, REPLAY needs its trailing space.
  for (const char* other : {"RTL_RDS", "RTL_RDS_STATUS ", "RTL_RDS_STATUSX", "RTL_RDS STATUS", "rtl_rds_status",
                            "RTL_RDS_CAPTURE_START ", "RTL_RDS_CAPTURE_STOP ", "RTL_RDS_CAPTURE_SAVE NOW",
                            "RTL_RDS_CAPTURE_STATUS ", "RTL_RDS_CAPTURE", "RTL_RDS_REPLAY", "RTL_RDS_REPLAY_SAMPLE",
                            "RTL_RDS_REPLAYX file", "RTL_PRESET_LIST", "PAIR 00", ""}) {
    CHECK(!handle_command(other, true, hooks));
    CHECK(!handle_command(other, false, hooks));
  }
  CHECK(g_log.empty());

  if (failures == 0) {
    std::printf("PASS: RTL_RDS capture, replay%s and status routes, the RDS_STATUS reply and lines left alone\n",
#if ORCSDR_DSP_AB
                ", per-sample replay (A/B build)"
#else
                " (per-sample replay absent without ORCSDR_DSP_AB)"
#endif
                );
  }
  return failures == 0 ? 0 : 1;
}
