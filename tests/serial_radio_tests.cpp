// Host tests for the receiver start/stop serial commands (apps/orcsdr-tab5/ui/serial_radio.cpp). Expected replies are
// typed from the code this replaced in main.cpp.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_radio_tests.cpp apps/orcsdr-tab5/ui/serial_radio.cpp
#include "serial_radio.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr::serial_radio;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_writes;
std::vector<std::string> g_calls;
bool g_stopping = true;
bool g_queue_ok = true;
Band g_band = Band::fm;
bool g_continuous = false;
int g_volume = -1;

Hooks make(bool stop_signed_out) {
  Hooks h;
  h.write = [](const char* t) { g_writes.emplace_back(t); };
  h.write_line = [](const char* t) { g_writes.emplace_back(t); g_writes.emplace_back("\n"); };
  h.stop_without_sign_in = stop_signed_out;
  h.stop = [] { g_calls.emplace_back("stop"); return g_stopping; };
  h.queue_capture = [](Band b, bool continuous, Queued* q) {
    g_calls.emplace_back("queue");
    g_band = b;
    g_continuous = continuous;
    if (!g_queue_ok) return false;
    const char* names[] = {"FM", "WX", "AM", "LORA"};
    q->band_name = names[static_cast<int>(b)];
    q->frequency_hz = 96100000;
    q->sample_rate_sps = 960000;
    q->bytes = 16384;
    q->volume = 128;
    return true;
  };
  h.volume_min = 0;
  h.volume_max = 255;
  h.set_volume = [](uint8_t v) { g_volume = v; g_calls.emplace_back("volume"); };
  return h;
}
const Hooks kHooks = make(false);
const Hooks kLabHooks = make(true);

void reset() {
  g_writes.clear();
  g_calls.clear();
  g_stopping = true;
  g_queue_ok = true;
  g_volume = -1;
}

std::string joined() {
  std::string s;
  for (const auto& w : g_writes) s += w;
  return s;
}

}  // namespace

int main() {
  // RTL_STOP
  reset();
  CHECK(handle_command("RTL_STOP", true, kHooks) && joined() == "RTL_STOPPING\n" && g_writes.size() == 2);
  reset();
  g_stopping = false;
  CHECK(handle_command("RTL_STOP", true, kHooks) && joined() == "RTL_STOP_RESULT ESP_OK\n");
  reset();
  CHECK(!handle_command("RTL_STOP", false, kHooks));   // signed out: silent fall-through, as before
  CHECK(g_calls.empty() && g_writes.empty());
  reset();
  CHECK(handle_command("RTL_STOP", false, kLabHooks) && joined() == "RTL_STOPPING\n");   // lab/test builds

  // Capture and listen: every spelling, band and continuous flag
  struct Case { const char* command; Band band; bool continuous; };
  const Case cases[] = {
      {"RTL_CAPTURE", Band::fm, false},       {"RTL_CAPTURE KZEL", Band::fm, false},
      {"RTL_LISTEN KZEL", Band::fm, true},    {"RTL_LISTEN FM", Band::fm, true},
      {"RTL_CAPTURE NOAA", Band::wx, false},  {"RTL_LISTEN NOAA", Band::wx, true},
      {"RTL_LISTEN WX", Band::wx, true},      {"RTL_CAPTURE AM", Band::am, false},
      {"RTL_LISTEN AM", Band::am, true},      {"RTL_CAPTURE LORA", Band::lora, false},
      {"RTL_LISTEN LORA", Band::lora, true}};
  for (const Case& c : cases) {
    reset();
    CHECK(handle_command(c.command, true, kHooks));
    CHECK(g_band == c.band && g_continuous == c.continuous && g_writes.size() == 1);
    const char* names[] = {"FM", "WX", "AM", "LORA"};
    const std::string want = std::string("RTL_CAPTURE_QUEUED band=") + names[static_cast<int>(c.band)] +
                             " frequency_hz=96100000 sample_rate_sps=960000 bytes=16384 continuous=" +
                             (c.continuous ? "true" : "false") + " volume=128\n";
    CHECK(!g_writes.empty() && g_writes[0] == want);
    reset();
    CHECK(!handle_command(c.command, false, kHooks) && g_calls.empty());   // signed out: silent, as before
  }
  reset();
  g_queue_ok = false;
  CHECK(handle_command("RTL_LISTEN FM", true, kHooks) && joined() == "RTL_CAPTURE_BUSY_OR_UNAVAILABLE\n");
  for (const char* other : {"RTL_LISTEN", "RTL_LISTEN X", "RTL_CAPTURE FM", "RTL_CAPTURE ", "RTL_LISTEN fm"}) {
    reset();
    CHECK(!handle_command(other, true, kHooks) && g_calls.empty() && g_writes.empty());
  }

  // RTL_VOLUME
  reset();
  CHECK(handle_command("RTL_VOLUME 0", true, kHooks) && g_volume == 0 && joined() == "RTL_VOLUME_OK volume=0\n");
  reset();
  CHECK(handle_command("RTL_VOLUME 255", true, kHooks) && g_volume == 255 && g_writes.size() == 1 &&
        g_writes[0] == "RTL_VOLUME_OK volume=255\n");
  reset();
  CHECK(handle_command("RTL_VOLUME 12abc", true, kHooks) && g_volume == 12);   // atoi, as before
  for (const char* bad : {"RTL_VOLUME 256", "RTL_VOLUME -1"}) {
    reset();
    CHECK(handle_command(bad, true, kHooks) && g_calls.empty() && joined() == "RTL_VOLUME_INVALID\n");
  }
  reset();
  CHECK(handle_command("RTL_VOLUME x", true, kHooks) && g_volume == 0);   // atoi("x") is 0, inside the range
  reset();
  CHECK(!handle_command("RTL_VOLUME 10", false, kHooks) && g_calls.empty());   // signed out: silent
  reset();
  CHECK(!handle_command("RTL_VOLUME", true, kHooks) && g_calls.empty());

  if (failures) {
    std::printf("serial_radio_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("serial_radio_tests: ok\n");
  return 0;
}
