// Host tests for the capture and IQ-recording serial commands (apps/orcsdr-tab5/ui/serial_iq.cpp). Expected replies are
// typed from the code this replaced in main.cpp.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_iq_tests.cpp apps/orcsdr-tab5/ui/serial_iq.cpp
#include "serial_iq.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_iq;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_writes;
std::vector<std::string> g_calls;
CaptureStatus g_capture;
IqStatus g_iq;

const Hooks kHooks = [] {
  Hooks h;
  h.write = [](const char* t) { g_writes.emplace_back(t); };
  h.write_line = [](const char* t) { g_writes.emplace_back(t); g_writes.emplace_back("\n"); };
  h.capture_status = [] { return g_capture; };
  h.iq_start = [] { g_calls.emplace_back("start"); };
  h.iq_diag_start = [](const char* a) { g_calls.push_back(std::string("diag ") + a); };
  h.diag_status = [] {
    DiagStatus s;
    s.transition = "retune";
    s.sequence = 4000000000u;
    s.active = true;
    s.ready = false;
    s.bytes = 12345;
    s.max_bytes = 7680000;
    s.rate = 2400000;
    s.frequency_hz = 915000000;
    s.started_ms = 3900000000u;
    return s;
  };
  h.iq_stop = [] { g_calls.emplace_back("stop"); };
  h.iq_status = [] { return g_iq; };
  return h;
}();

void reset() {
  g_writes.clear();
  g_calls.clear();
}

}  // namespace

int main() {
  // RTL_CAPTURE_STATUS, running and finished
  reset();
  g_capture = CaptureStatus{};
  g_capture.state = "running";
  CHECK(handle_command("RTL_CAPTURE_STATUS", false, kHooks));
  CHECK(g_writes.size() == 1 && g_writes[0] == "RTL_CAPTURE_STATUS state=running\n");
  reset();
  g_capture.state = "complete";
  g_capture.finished = true;
  g_capture.bytes = 5000000000ull;
  g_capture.min = 3;
  g_capture.max = 252;
  g_capture.mean = 127.4567;
  g_capture.sha256 = "";
  g_capture.error = "not run";
  CHECK(handle_command("RTL_CAPTURE_STATUS", false, kHooks));
  CHECK(g_writes.size() == 1 &&
        g_writes[0] == "RTL_CAPTURE_STATUS state=complete bytes=5000000000 min=3 max=252 mean=127.457 sha256=none "
                       "error=\"not run\"\n");
  reset();
  g_capture.sha256 = "abc123";
  g_capture.error = "";
  CHECK(handle_command("RTL_CAPTURE_STATUS", false, kHooks));
  CHECK(g_writes.size() == 1 && g_writes[0].find("sha256=abc123 error=\"\"\n") != std::string::npos);

  // RTL_IQ_START needs no sign-in
  reset();
  CHECK(handle_command("RTL_IQ_START", false, kHooks) && g_calls.size() == 1 && g_calls[0] == "start" && g_writes.empty());

  // RTL_IQ_DIAG_START needs it
  reset();
  CHECK(handle_command("RTL_IQ_DIAG_START retune 2", false, kHooks));
  CHECK(g_calls.empty() && g_writes.size() == 2 && g_writes[0] == "RTL_IQ_DIAG_ERROR auth_required");
  reset();
  CHECK(handle_command("RTL_IQ_DIAG_START retune 2", true, kHooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "diag retune 2" && g_writes.empty());
  reset();
  CHECK(handle_command("RTL_IQ_DIAG_START ", true, kHooks) && g_calls.size() == 1 && g_calls[0] == "diag ");

  reset();
  CHECK(handle_command("RTL_IQ_DIAG_STATUS", false, kHooks));
  CHECK(g_writes.size() == 1 &&
        g_writes[0] == "RTL_IQ_DIAG_STATUS transition=\"retune\" sequence=4000000000 active=1 ready=0 bytes=12345 "
                       "max_bytes=7680000 rate=2400000 frequency_hz=915000000 started_ms=3900000000\n");

  for (const char* c : {"RTL_IQ_STOP", "RTL_IQ_SAVE"}) {
    reset();
    CHECK(handle_command(c, false, kHooks) && g_calls.size() == 1 && g_calls[0] == "stop" && g_writes.empty());
  }

  reset();
  g_iq = IqStatus{};
  g_iq.source = "lora";
  g_iq.active = false;
  g_iq.ready = true;
  g_iq.energy_triggered = true;
  g_iq.bytes = 7680000;
  g_iq.max_bytes = 7680000;
  g_iq.frequency_hz = 869525000;
  g_iq.sf = 11;
  g_iq.bandwidth_hz = 250000;
  g_iq.detector_on = true;
  g_iq.events = 7;
  g_iq.messages = 2;
  g_iq.noise_dbfs = -91.24;
  g_iq.trigger_dbfs = -78.06;
  g_iq.last_path = "";
  CHECK(handle_command("RTL_IQ_STATUS", false, kHooks));
  CHECK(g_writes.size() == 1 &&
        g_writes[0] == "RTL_IQ_STATUS source=lora active=false ready=true storage=psram mode=energy bytes=7680000 "
                       "max_bytes=7680000 frequency_hz=869525000 sf=11 bw=250000 auto=on events=7 messages=2 "
                       "noise_dbfs=-91.2 trigger_dbfs=-78.1 last_path=\"none\"\n");
  reset();
  g_iq.energy_triggered = false;
  g_iq.detector_on = false;
  g_iq.last_path = "/orcsdr/iq/001.cu8";
  CHECK(handle_command("RTL_IQ_STATUS", false, kHooks));
  CHECK(g_writes.size() == 1 && g_writes[0].find("mode=manual") != std::string::npos &&
        g_writes[0].find("auto=off") != std::string::npos &&
        g_writes[0].find("last_path=\"/orcsdr/iq/001.cu8\"\n") != std::string::npos);

  for (const char* other : {"RTL_IQ_DIAG_START", "RTL_IQ_STARTX", "RTL_CAPTURE", "RTL_IQ_STATUS ", "RTL_IQ_GET_BEGIN",
                            "RTL_CAPTURE_STATUS "}) {
    reset();
    CHECK(!handle_command(other, true, kHooks));
    CHECK(g_calls.empty() && g_writes.empty());
  }
  if (failures) {
    std::printf("serial_iq_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("serial_iq_tests: ok\n");
  return 0;
}
