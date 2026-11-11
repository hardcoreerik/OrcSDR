#include "receiver_source_policy.hpp"

#include <cstdio>

using namespace orcsdr::source;

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

static Inputs make(Mode mode, bool usb, bool remote, bool net, uint32_t ms,
                   Active active = Active::none) {
  Inputs in;
  in.mode = mode;
  in.usb_ready = usb;
  in.remote_configured = remote;
  in.network_up = net;
  in.ms_since_boot = ms;
  in.active = active;
  return in;
}

int main() {
  // Local-only boot: no network at all, USB present.
  CHECK(choose(make(Mode::local_usb, true, false, false, 0)) == Active::local_usb);
  CHECK(choose(make(Mode::automatic, true, false, false, 0)) == Active::local_usb);
  // Remote-only boot: no dongle, server saved, Wi-Fi up.
  CHECK(choose(make(Mode::remote_tcp, false, true, true, 0)) == Active::remote_tcp);
  // Remote needs a configured server and a network; it never invents either.
  CHECK(choose(make(Mode::remote_tcp, false, false, true, 0)) == Active::none);
  CHECK(choose(make(Mode::remote_tcp, false, true, false, 0)) == Active::none);
  // Local USB mode never goes remote, even with a saved server and a dead dongle.
  CHECK(choose(make(Mode::local_usb, false, true, true, 60000)) == Active::none);
  // Automatic fallback waits for the grace period, then uses the saved server.
  CHECK(choose(make(Mode::automatic, false, true, true, 1000)) == Active::none);
  CHECK(choose(make(Mode::automatic, false, true, true, 8000)) == Active::remote_tcp);
  CHECK(choose(make(Mode::automatic, false, false, true, 60000)) == Active::none);
  CHECK(choose(make(Mode::automatic, false, true, false, 60000)) == Active::none);
  // A dongle appearing later does not interrupt a running remote stream.
  CHECK(choose(make(Mode::automatic, true, true, true, 20000, Active::remote_tcp)) ==
        Active::remote_tcp);
  // A running USB stream is kept when a server is also configured.
  CHECK(choose(make(Mode::automatic, true, true, true, 20000, Active::local_usb)) ==
        Active::local_usb);
  // Network loss ends the remote source; USB then wins if present.
  CHECK(choose(make(Mode::automatic, true, true, false, 20000, Active::remote_tcp)) ==
        Active::local_usb);
  CHECK(choose(make(Mode::automatic, false, true, false, 20000, Active::remote_tcp)) ==
        Active::none);
  // Unplugging the dongle in Automatic falls back after the grace period.
  CHECK(choose(make(Mode::automatic, false, true, true, 20000, Active::local_usb)) ==
        Active::remote_tcp);
  // Forcing a mode releases a source the mode forbids.
  CHECK(choose(make(Mode::remote_tcp, true, true, true, 20000, Active::local_usb)) ==
        Active::remote_tcp);
  CHECK(choose(make(Mode::local_usb, true, true, true, 20000, Active::remote_tcp)) ==
        Active::local_usb);
  // Parsing.
  Mode mode = Mode::automatic;
  CHECK(parse_mode("remote", &mode) && mode == Mode::remote_tcp);
  CHECK(parse_mode("usb", &mode) && mode == Mode::local_usb);
  CHECK(parse_mode("auto", &mode) && mode == Mode::automatic);
  CHECK(!parse_mode("bogus", &mode) && mode == Mode::automatic);
  CHECK(!parse_mode(nullptr, &mode));
  std::printf("RECEIVER_SOURCE_POLICY_HOST_TEST failures=%d pass=%d\n", g_failures,
              g_failures == 0);
  return g_failures == 0 ? 0 : 1;
}
