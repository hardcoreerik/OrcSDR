#include "../main/network_clock.hpp"

#include <cassert>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <sys/time.h>

namespace {
uint32_t fake_ms = 1000;
time_t fake_utc = 0;
bool fake_sntp_enabled = false;
bool fake_set_ok = true;
uint32_t begin_count = 0;
uint32_t end_count = 0;
void (*fake_callback)(timeval*) = nullptr;
}

extern "C" uint32_t network_clock_test_monotonic_ms() { return fake_ms; }
extern "C" bool network_clock_test_sntp_enabled() { return fake_sntp_enabled; }
extern "C" void network_clock_test_sntp_begin(void (*callback)(timeval*)) {
  ++begin_count;
  fake_sntp_enabled = true;
  fake_callback = callback;
}
extern "C" void network_clock_test_sntp_end() {
  ++end_count;
  fake_sntp_enabled = false;
}
extern "C" time_t network_clock_test_wallclock_now() { return fake_utc; }
extern "C" int network_clock_test_set_wallclock(time_t utc) {
  if (!fake_set_ok) return -1;
  fake_utc = utc;
  return 0;
}

int main() {
  using network_clock::Error;
  using orcsdr::clock_sync::Level;

  // Connected boot requests one asynchronous sync; callback completion commits plausible UTC.
  fake_utc = 0;
  network_clock::service(true);
  auto s = network_clock::status();
  assert(s.syncing && begin_count == 1 && s.attempts == 1);
  fake_utc = 1800000000;
  timeval received{};
  assert(fake_callback != nullptr);
  fake_callback(&received);
  network_clock::service(true);
  s = network_clock::status();
  assert(!s.syncing && !s.failed && s.wallclock_valid);
  assert(s.last_success_utc == 1800000000u && s.time_step_revision == 1);
  assert(s.level == Level::synced && end_count == 1);

  // Wi-Fi loss preserves the successful sync and does not make the clock fresh again.
  fake_ms += 5000;
  fake_utc += 5;
  network_clock::service(false);
  s = network_clock::status();
  assert(s.last_success_utc == 1800000000u && s.freshness_age_s == 5);

  // Explicit no-Wi-Fi requests expose the condition and reconnect permits immediate auto sync.
  assert(network_clock::request_sync());
  network_clock::service(false);
  s = network_clock::status();
  assert(s.last_error == Error::no_wifi && !s.syncing);
  network_clock::service(true);
  s = network_clock::status();
  assert(s.syncing && begin_count == 2);

  // A lost link during an attempt is stopped and reported; an old successful stamp becomes retry-eligible after reconnect/backoff.
  network_clock::service(false);
  s = network_clock::status();
  assert(!s.syncing && s.failed && s.last_error == Error::no_wifi && end_count == 2);
  fake_utc += 2 * 60 * 60;
  network_clock::service(true);
  assert(!network_clock::status().syncing);  // failed attempts obey the shared five-minute retry interval
  fake_ms += 5 * 60 * 1000;
  network_clock::service(true);
  assert(network_clock::status().syncing && begin_count == 3);

  // An engine owned elsewhere is reported busy and never stopped/reconfigured.
  network_clock::service(false);
  fake_utc += 2 * 60 * 60;
  fake_sntp_enabled = true;
  fake_ms += 61000;
  assert(network_clock::request_sync());
  network_clock::service(true);
  s = network_clock::status();
  assert(s.failed && s.last_error == Error::busy && end_count == 3);
  fake_sntp_enabled = false;

  // Timeout releases only the owned engine and records failure.
  fake_ms += 5 * 60 * 1000;
  network_clock::service(true);
  assert(network_clock::status().syncing && begin_count == 4);
  fake_ms += 20000;
  network_clock::service(true);
  s = network_clock::status();
  assert(!s.syncing && s.failed && s.last_error == Error::timeout && end_count == 4);

  // Setter validation follows Settings bounds and a failed POSIX set preserves the prior trust stamp.
  constexpr uint32_t kManualUtc = 1800000600u;
  constexpr uint32_t kPreviousSync = 1800000000u;
  assert(!network_clock::request_set_utc(4102444800u));
  s = network_clock::status();
  assert(s.last_error == Error::invalid_time);
  fake_set_ok = false;
  assert(network_clock::request_set_utc(kManualUtc));
  network_clock::service(true);
  s = network_clock::status();
  assert(s.last_error == Error::set_time && s.last_success_utc == kPreviousSync);

  // A queued manual set cancels an in-flight SNTP result before it can restore freshness.
  fake_set_ok = true;
  assert(network_clock::request_sync());
  network_clock::service(true);
  assert(network_clock::status().syncing);
  assert(network_clock::request_set_utc(kManualUtc));
  fake_utc = 1800000700u;  // simulate a callback that completed just before the owner handles the set
  fake_callback(&received);
  network_clock::service(true);
  s = network_clock::status();
  assert(!s.syncing && s.last_success_utc == 0 && s.time_step_revision == 3);
  assert(s.wallclock_valid && fake_utc == kManualUtc && end_count == 5);
  network_clock::service(true);
  s = network_clock::status();
  assert(s.last_success_utc == 0);  // the stale callback was discarded

  // A POSIX wall clock outside Settings' supported years is not considered valid UTC.
  fake_utc = 4102444800LL;
  s = network_clock::status();
  assert(!s.wallclock_valid);

  std::puts("p4_network_clock_test: PASS");
  return 0;
}
