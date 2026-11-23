// Host test for the RTL_DSP command family (serial_dsp.cpp). The expected replies are the strings the dispatcher in
// main.cpp used before the family moved, filled in with the scripted device facts below.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_dsp_tests.cpp and apps/orcsdr-tab5/ui/serial_dsp.cpp. Build it four ways: with no flag (the release
// build: only RTL_DSP STATS exists), with -DORCSDR_DSP_LAB=1, with -DORCSDR_DSP_AB=1, and with both.
#include "serial_dsp.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_dsp;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// One log for writes and hook calls, so the test pins their order as well as how the reply is split into writes:
// "W:" is one verbatim write (it carries its own '\n'), "L:" is a line written through println, "C:" is a hook call.
std::vector<std::string> g_log;

bool g_radio_running = false;
bool g_rate_usable = true;
bool g_queue_ok = true;
bool g_arm_ok = true;
std::string g_stats_line = "RTL_DSP_STATS window_ms=1000 blocks=48";
size_t g_stats_line_size = 0;

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_log.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_log.push_back(std::string("L:") + text); };
  hooks.stats = [](char* line, size_t line_size) -> Stats {
    g_log.push_back("C:stats");
    g_stats_line_size = line_size;
    std::snprintf(line, line_size, "%s", g_stats_line.c_str());
    return {"FM", 1024000u, 11u, 22u, 33u, 44u, 55u, 66u, 77u};
  };
  hooks.lab_radio_running = []() -> bool { g_log.push_back("C:running"); return g_radio_running; };
  hooks.lab_set_rate_override = [](uint32_t rate) { g_log.push_back("C:override=" + std::to_string(rate)); };
  hooks.lab_rate_usable = [](uint32_t rate) -> bool {
    g_log.push_back("C:usable=" + std::to_string(rate));
    return g_rate_usable;
  };
  hooks.lab_queue_fm_listen = []() -> bool { g_log.push_back("C:queue_fm"); return g_queue_ok; };
  hooks.lab_command = [](const char* args) { g_log.push_back(std::string("C:lab[") + args + "]"); };
  hooks.ab_arm = [](size_t blocks) -> bool {
    g_log.push_back("C:arm=" + std::to_string(static_cast<long long>(static_cast<long>(blocks))));
    return g_arm_ok;
  };
  hooks.ab_storm = [](long requests) -> Storm {
    g_log.push_back("C:storm=" + std::to_string(requests));
    return {123u, 97u, 0u, 12u};
  };
  hooks.ab_request_run = [] { g_log.push_back("C:run"); };
  return hooks;
}

void expect(int line, const std::vector<std::string>& expected) {
  if (g_log != expected) {
    ++failures;
    std::printf("FAIL line %d output:\n", line);
    for (size_t i = 0; i < g_log.size() || i < expected.size(); ++i) {
      std::printf("  got:  %s\n  want: %s\n", i < g_log.size() ? g_log[i].c_str() : "<nothing>",
                  i < expected.size() ? expected[i].c_str() : "<nothing>");
    }
  }
  g_log.clear();
}
#define EXPECT(...) expect(__LINE__, __VA_ARGS__)

// A line this build does not have: left to the dispatcher, nothing called, nothing printed.
void expect_not_mine(int line, const Hooks& hooks, const char* command) {
  for (const bool authenticated : {false, true}) {
    if (handle_command(command, authenticated, hooks) || !g_log.empty()) {
      ++failures;
      std::printf("FAIL line %d: \"%s\" should be left to the dispatcher\n", line, command);
      g_log.clear();
    }
  }
}
#define NOT_MINE(command) expect_not_mine(__LINE__, hooks, command)

}  // namespace

int main() {
  const Hooks hooks = make_hooks();

  // RTL_DSP STATS: in every build, no authentication. One hook call fills the stage text and returns the counters;
  // the reply is one write.
  const std::string stats_tail = " band=FM rate=1024000 iq_pipeline_drops=11 driver_overruns=22 driver_drops=33 "
                                 "audio_chunks=44 audio_dropped_chunks=55 audio_ring_overruns=66 "
                                 "audio_submit_failures=77\n";
  for (const bool authenticated : {false, true}) {
    CHECK(handle_command("RTL_DSP STATS", authenticated, hooks));
    CHECK(g_stats_line_size == 768);
    EXPECT({"C:stats", "W:RTL_DSP_STATS window_ms=1000 blocks=48" + stats_tail});
  }
  // The longest stage text the 768-byte buffer can hold still goes out whole.
  g_stats_line = std::string(767, 's');
  CHECK(handle_command("RTL_DSP STATS", false, hooks));
  EXPECT({"C:stats", "W:" + std::string(767, 's') + stats_tail});
  g_stats_line = "RTL_DSP_STATS window_ms=1000 blocks=48";

#if ORCSDR_DSP_LAB
  // LAB RATE refuses while a stream is queued or running, before anything else.
  g_radio_running = true;
  for (const char* line : {"RTL_DSP LAB RATE DEFAULT", "RTL_DSP LAB RATE 2560000", "RTL_DSP LAB RATE x", "RTL_DSP LAB RATE "}) {
    CHECK(handle_command(line, false, hooks));
    EXPECT({"C:running", "L:LAB_RATE_ERROR radio_running"});
  }
  g_radio_running = false;
  CHECK(handle_command("RTL_DSP LAB RATE DEFAULT", false, hooks));
  EXPECT({"C:running", "C:override=0", "L:LAB_RATE default"});
  // Only the two exact rates are accepted; anything else is refused without asking the receiver.
  for (const char* line : {"RTL_DSP LAB RATE ", "RTL_DSP LAB RATE 0", "RTL_DSP LAB RATE 2400000", "RTL_DSP LAB RATE default",
                           "RTL_DSP LAB RATE 2560000 ", "RTL_DSP LAB RATE  2560000", "RTL_DSP LAB RATE 3200000x"}) {
    CHECK(handle_command(line, true, hooks));
    EXPECT({"C:running", "L:LAB_RATE_ERROR use_2560000_3200000_or_DEFAULT"});
  }
  // No receiver, or one that cannot do the rate: same reply, the override is not touched.
  g_rate_usable = false;
  CHECK(handle_command("RTL_DSP LAB RATE 3200000", false, hooks));
  EXPECT({"C:running", "C:usable=3200000", "L:LAB_RATE_ERROR use_2560000_3200000_or_DEFAULT"});
  g_rate_usable = true;
  // Accepted: the override is set before FM is queued.
  CHECK(handle_command("RTL_DSP LAB RATE 2560000", false, hooks));
  EXPECT({"C:running", "C:usable=2560000", "C:override=2560000", "C:queue_fm", "W:LAB_RATE queued=1 rate=2560000 band=FM\n"});
  CHECK(handle_command("RTL_DSP LAB RATE 3200000", true, hooks));
  EXPECT({"C:running", "C:usable=3200000", "C:override=3200000", "C:queue_fm", "W:LAB_RATE queued=1 rate=3200000 band=FM\n"});
  // FM could not be queued: the override is cleared again.
  g_queue_ok = false;
  CHECK(handle_command("RTL_DSP LAB RATE 3200000", false, hooks));
  EXPECT({"C:running", "C:usable=3200000", "C:override=3200000", "C:queue_fm", "C:override=0", "L:LAB_RATE_ERROR start_failed"});
  g_queue_ok = true;

  // RTL_DSP LAB hands the rest of the line to the lab. It is a prefix match on "RTL_DSP LAB": the arguments are what
  // follows a space, and anything else straight after the prefix gives empty arguments.
  CHECK(handle_command("RTL_DSP LAB", false, hooks));
  EXPECT({"C:lab[]"});
  CHECK(handle_command("RTL_DSP LAB ", false, hooks));
  EXPECT({"C:lab[]"});
  CHECK(handle_command("RTL_DSP LAB BENCH 3", true, hooks));
  EXPECT({"C:lab[BENCH 3]"});
  CHECK(handle_command("RTL_DSP LAB RATE", false, hooks));   // no trailing space: not the RATE route
  EXPECT({"C:lab[RATE]"});
  CHECK(handle_command("RTL_DSP LABX", false, hooks));
  EXPECT({"C:lab[]"});
#else
  NOT_MINE("RTL_DSP LAB RATE DEFAULT");
  NOT_MINE("RTL_DSP LAB RATE 2560000");
  NOT_MINE("RTL_DSP LAB");
  NOT_MINE("RTL_DSP LAB BENCH 3");
#endif

#if ORCSDR_DSP_AB
  CHECK(handle_command("RTL_DSP AB CAPTURE 8", false, hooks));
  EXPECT({"C:arm=8", "W:RTL_DSP_AB_ARM blocks=8 ok=1\n"});
  g_arm_ok = false;
  CHECK(handle_command("RTL_DSP AB CAPTURE 4000", true, hooks));
  EXPECT({"C:arm=4000", "W:RTL_DSP_AB_ARM blocks=4000 ok=0\n"});
  g_arm_ok = true;
  // The number is not range checked here: a non-number arms 0 blocks, a negative one is passed on converted.
  CHECK(handle_command("RTL_DSP AB CAPTURE x", false, hooks));
  EXPECT({"C:arm=0", "W:RTL_DSP_AB_ARM blocks=0 ok=1\n"});
  CHECK(handle_command("RTL_DSP AB CAPTURE ", false, hooks));
  EXPECT({"C:arm=0", "W:RTL_DSP_AB_ARM blocks=0 ok=1\n"});
  CHECK(handle_command("RTL_DSP AB CAPTURE -1", false, hooks));
  EXPECT({"C:arm=-1", "W:RTL_DSP_AB_ARM blocks=-1 ok=1\n"});

  CHECK(handle_command("RTL_DSP AB STORM 100", false, hooks));
  EXPECT({"C:storm=100", "W:RTL_DSP_AB_STORM requests=100 ms=123 applied=97 pending=0 audio_chunks_500ms=12\n"});
  CHECK(handle_command("RTL_DSP AB STORM x", true, hooks));
  EXPECT({"C:storm=0", "W:RTL_DSP_AB_STORM requests=0 ms=123 applied=97 pending=0 audio_chunks_500ms=12\n"});
  CHECK(handle_command("RTL_DSP AB STORM -5", false, hooks));
  EXPECT({"C:storm=-5", "W:RTL_DSP_AB_STORM requests=-5 ms=123 applied=97 pending=0 audio_chunks_500ms=12\n"});

  // RUN is requested before it is acknowledged.
  CHECK(handle_command("RTL_DSP AB RUN", false, hooks));
  EXPECT({"C:run", "L:RTL_DSP_AB_RUN queued"});
#else
  NOT_MINE("RTL_DSP AB CAPTURE 8");
  NOT_MINE("RTL_DSP AB STORM 100");
  NOT_MINE("RTL_DSP AB RUN");
#endif

  // Anything else is left to the dispatcher in every build: exact matches stay exact, prefixes need their space.
  for (const char* other : {"RTL_DSP", "RTL_DSP ", "RTL_DSP STATS ", "RTL_DSP STATSX", "RTL_DSP STAT", "rtl_dsp stats",
                            " RTL_DSP STATS", "RTL_DSP AB", "RTL_DSP AB ", "RTL_DSP AB CAPTURE", "RTL_DSP AB STORM",
                            "RTL_DSP AB RUN ", "RTL_DSP AB RUN NOW", "RTL_DSP AB RUNX", "rtl_dsp ab run", "RTL_DSP LA",
                            "rtl_dsp lab", "RTL_DSPX", "RTL_GAIN STATUS", ""}) {
    NOT_MINE(other);
  }

  if (failures == 0) {
    std::printf("PASS: RTL_DSP STATS%s%s replies, parse errors, call order and lines left to the dispatcher\n",
                ORCSDR_DSP_LAB ? ", LAB RATE, LAB" : " (no LAB routes in this build)",
                ORCSDR_DSP_AB ? ", AB CAPTURE, AB STORM, AB RUN" : " (no AB routes in this build)");
  }
  return failures == 0 ? 0 : 1;
}
