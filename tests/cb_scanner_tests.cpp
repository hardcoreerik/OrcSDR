#include "cb_scanner.hpp"

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string_view>

namespace {
[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) \
  do { if (!(expression)) fail(#expression, __LINE__); } while (false)

using namespace orcsdr::cb;

struct Band {
  float levels[kChannelCount];
  Band() { for (auto& level : levels) level = -70.0f; }
  Band& on(size_t channel, float level) { levels[channel] = level; return *this; }
};

void observe(Monitor& monitor, const Scanner& scanner, uint32_t now, const Band& band) {
  monitor.observe(now, band.levels, noise_floor(band.levels),
                  scanner.settings().threshold_db);
}

void test_channel_plan() {
  for (size_t i = 0; i < kChannelCount; ++i) {
    CHECK(kChannelsHz[i] >= 26965000 && kChannelsHz[i] <= 27405000);
    CHECK(nearest_channel(kChannelsHz[i]) == i);
    for (size_t j = i + 1; j < kChannelCount; ++j) CHECK(kChannelsHz[i] != kChannelsHz[j]);
  }
  CHECK(kChannelsHz[kHighwayChannel] == 27185000);
  CHECK(kChannelsHz[kEmergencyChannel] == 27065000);
  CHECK(kChannelsHz[22] == 27255000);  // CH 23 is out of numeric order.
}

void test_channel_levels() {
  // 2048-bin, 2.4 Msps spectrum tuned to CH 1: the whole band must stay visible.
  static float bins[2048];
  for (auto& bin : bins) bin = -80.0f;
  const uint32_t rate = 2400000;
  const uint32_t center = kChannelsHz[0];
  const auto bin_for = [&](uint32_t hz) {
    return static_cast<size_t>(1024 + (static_cast<double>(hz) - center) * 2048.0 / rate + 0.5);
  };
  bins[bin_for(kChannelsHz[39]) + 2] = -30.0f;  // CH 40 carrier, 2.3 kHz high
  bins[bin_for(kChannelsHz[22])] = -35.0f;      // CH 23
  float levels[kChannelCount];
  channel_levels(bins, 2048, rate, center, levels);
  for (size_t i = 0; i < kChannelCount; ++i) CHECK(levels[i] > kNoLevel);
  CHECK(levels[39] == -30.0f);
  CHECK(levels[22] == -35.0f);
  CHECK(levels[38] == -80.0f);  // Adjacent channel is not smeared.
  CHECK(levels[23] == -80.0f);
  CHECK(noise_floor(levels) == -80.0f);

  // A narrow 240 kHz capture only covers nearby channels.
  channel_levels(bins, 2048, 240000, kChannelsHz[kHighwayChannel], levels);
  CHECK(levels[kHighwayChannel] > kNoLevel);
  CHECK(levels[0] == kNoLevel && levels[39] == kNoLevel);
  CHECK(nearest_channel(0) == 0 && nearest_channel(40000000) == 39);
}

void test_monitor_log() {
  Monitor monitor;
  Scanner scanner;
  observe(monitor, scanner, 0, Band().on(5, -50.0f));
  CHECK(monitor.active(5) && monitor.active_count() == 1);
  // Short fades inside the release window stay one transmission.
  observe(monitor, scanner, 300, Band());
  CHECK(monitor.active(5));
  observe(monitor, scanner, 500, Band().on(5, -48.0f));
  observe(monitor, scanner, 1100, Band());
  CHECK(monitor.active(5));
  observe(monitor, scanner, 1200, Band());
  CHECK(!monitor.active(5));
  CHECK(monitor.log_count() == 1);
  CHECK(monitor.log(0)->channel == 5 && monitor.log(0)->duration_ms == 500);
  CHECK(monitor.log(0)->peak_snr_db == 22.0f);
  CHECK(monitor.stats(5).hits == 1);
  // Blips shorter than kMinHitMs are not logged.
  observe(monitor, scanner, 2000, Band().on(7, -50.0f));
  observe(monitor, scanner, 2800, Band());
  CHECK(!monitor.active(7) && monitor.log_count() == 1);
  // Hysteresis: once open, 3 dB below threshold still counts.
  observe(monitor, scanner, 3000, Band().on(9, -58.0f));
  CHECK(monitor.active(9));
  observe(monitor, scanner, 3100, Band().on(9, -62.5f));
  observe(monitor, scanner, 4000, Band().on(9, -62.5f));
  CHECK(monitor.active(9));
  CHECK(monitor.log(0) != nullptr && monitor.log(1) == nullptr);
  // The ring keeps only the newest entries.
  uint32_t now = 10000;
  for (size_t i = 0; i < Monitor::kLogCapacity + 5; ++i) {
    observe(monitor, scanner, now, Band().on(i % 40, -40.0f));
    observe(monitor, scanner, now + 400, Band().on(i % 40, -40.0f));
    observe(monitor, scanner, now + 1200, Band());
    now += 2000;
  }
  CHECK(monitor.log_count() == Monitor::kLogCapacity);
  CHECK(monitor.log(0)->channel == (Monitor::kLogCapacity + 4) % 40);
  monitor.clear_log();
  CHECK(monitor.log_count() == 0 && monitor.stats(5).hits == 0);
}

void test_scan_stop_hang_resume() {
  Monitor monitor;
  Scanner scanner;
  scanner.settings().priority_enabled = false;
  CHECK(scanner.update(0, monitor) == -1);  // Off does nothing.
  scanner.start(0, kHighwayChannel);
  observe(monitor, scanner, 0, Band());
  CHECK(scanner.update(0, monitor) == -1 && scanner.state() == State::watching);

  observe(monitor, scanner, 100, Band().on(2, -55.0f).on(30, -45.0f));
  CHECK(scanner.update(100, monitor) == 30);  // Strongest active channel wins.
  CHECK(scanner.state() == State::settling);
  CHECK(scanner.update(200, monitor) == -1 && scanner.state() == State::settling);
  CHECK(scanner.update(100 + scanner.settings().settle_ms, monitor) == -1);
  CHECK(scanner.state() == State::receiving && scanner.channel() == 30);

  // CH 31 stops talking; the scanner hangs for a reply rather than jumping to CH 3.
  observe(monitor, scanner, 600, Band().on(2, -55.0f));
  observe(monitor, scanner, 1400, Band().on(2, -55.0f));
  CHECK(!monitor.active(30));
  CHECK(scanner.update(1400, monitor) == -1 && scanner.state() == State::hang);
  CHECK(scanner.hang_remaining_ms(1400) == scanner.settings().hang_ms);
  // A reply inside the hang window resumes reception on the same channel.
  observe(monitor, scanner, 2000, Band().on(2, -55.0f).on(30, -45.0f));
  CHECK(scanner.update(2000, monitor) == -1 && scanner.state() == State::receiving);
  observe(monitor, scanner, 3000, Band().on(2, -55.0f));
  CHECK(scanner.update(3000, monitor) == -1 && scanner.state() == State::hang);
  CHECK(scanner.update(3000 + scanner.settings().hang_ms - 1, monitor) == -1);
  CHECK(scanner.update(3000 + scanner.settings().hang_ms, monitor) == 2);
  CHECK(scanner.stops() == 2);
}

void test_priority_lockout_skip_hold() {
  Monitor monitor;
  Scanner scanner;
  scanner.start(0, 0);
  observe(monitor, scanner, 0, Band().on(20, -40.0f));
  CHECK(scanner.update(0, monitor) == 20);
  CHECK(scanner.update(1000, monitor) == -1 && scanner.state() == State::receiving);
  // CH 9 priority preempts a weaker or stronger busy channel.
  observe(monitor, scanner, 1100, Band().on(20, -40.0f).on(kEmergencyChannel, -58.0f));
  CHECK(scanner.update(1100, monitor) == static_cast<int>(kEmergencyChannel));

  // Lockout removes a channel from both selection and an active hold.
  scanner.set_lockout(kEmergencyChannel, true);
  CHECK(scanner.locked_out(kEmergencyChannel) && scanner.eligible_count() == 39);
  CHECK(scanner.update(2000, monitor) == 20);
  scanner.clear_lockouts();
  CHECK(scanner.lockout_mask() == 0);
  scanner.set_lockout_mask(~uint64_t{0});
  CHECK(scanner.eligible_count() == 0);
  scanner.set_lockout_mask(0);

  // Skip ignores the current carrier until it drops, then it is eligible again.
  scanner.settings().priority_enabled = false;
  CHECK(scanner.update(3000, monitor) == -1 && scanner.channel() == 20);
  scanner.skip(3100);
  CHECK(scanner.skipped(20));
  observe(monitor, scanner, 3100, Band().on(20, -40.0f));
  CHECK(scanner.update(3100, monitor) == -1);  // Nothing else eligible and active.
  observe(monitor, scanner, 4000, Band());
  CHECK(scanner.update(4000, monitor) == -1 && !scanner.skipped(20));

  // Hold pins the channel; manual tuning while scanning implies hold.
  scanner.hold(12);
  observe(monitor, scanner, 4100, Band().on(33, -30.0f));
  CHECK(scanner.state() == State::held && scanner.update(4100, monitor) == -1);
  scanner.release(4200);
  CHECK(scanner.update(4200, monitor) == 33);
  scanner.note_manual_tune(3);
  CHECK(scanner.state() == State::held && scanner.channel() == 3);
  scanner.stop();
  CHECK(!scanner.running());
  scanner.hold(5);
  CHECK(scanner.state() == State::off);
}

void test_max_hold() {
  Monitor monitor;
  Scanner scanner;
  scanner.settings().priority_enabled = false;
  scanner.settings().max_hold_s = 15;
  scanner.start(0, 0);
  observe(monitor, scanner, 0, Band().on(11, -30.0f).on(25, -50.0f));
  CHECK(scanner.update(0, monitor) == 11);
  CHECK(scanner.update(400, monitor) == -1);
  observe(monitor, scanner, 15399, Band().on(11, -30.0f).on(25, -50.0f));
  CHECK(scanner.update(15399, monitor) == -1);
  CHECK(scanner.update(15400, monitor) == 25);  // Stuck carrier released after 15 s.
  CHECK(scanner.skipped(11));
}

// Max hold is for a stuck carrier: it times one transmission. A conversation
// of replies separated by hangs must not add up to a skip.
void test_max_hold_times_each_reply() {
  Monitor monitor;
  Scanner scanner;
  scanner.settings().priority_enabled = false;
  scanner.settings().max_hold_s = 15;
  scanner.start(0, 0);
  observe(monitor, scanner, 0, Band().on(11, -30.0f).on(25, -50.0f));
  CHECK(scanner.update(0, monitor) == 11);
  CHECK(scanner.update(400, monitor) == -1 && scanner.state() == State::receiving);

  // First transmission ends; the scanner hangs for a reply.
  observe(monitor, scanner, 6000, Band().on(11, -30.0f).on(25, -50.0f));
  observe(monitor, scanner, 6800, Band().on(25, -50.0f));
  CHECK(scanner.update(6800, monitor) == -1 && scanner.state() == State::hang);

  // The reply resumes reception; max hold now starts at 7500, not at 400.
  observe(monitor, scanner, 7500, Band().on(11, -30.0f).on(25, -50.0f));
  CHECK(scanner.update(7500, monitor) == -1 && scanner.state() == State::receiving);
  observe(monitor, scanner, 22499, Band().on(11, -30.0f).on(25, -50.0f));
  CHECK(scanner.update(22499, monitor) == -1 && scanner.state() == State::receiving);
  CHECK(!scanner.skipped(11));
  CHECK(scanner.update(22500, monitor) == 25);  // 15 s of this one reply.
  CHECK(scanner.skipped(11));
}

void test_clock_wrap() {
  Monitor monitor;
  Scanner scanner;
  scanner.settings().priority_enabled = false;
  const uint32_t start = UINT32_MAX - 100;
  scanner.start(start, 0);
  observe(monitor, scanner, start, Band().on(6, -40.0f));
  CHECK(scanner.update(start, monitor) == 6);
  CHECK(scanner.update(start + 10, monitor) == -1 && scanner.state() == State::settling);
  CHECK(scanner.update(start + scanner.settings().settle_ms, monitor) == -1);
  CHECK(scanner.state() == State::receiving);
}

}  // namespace

int main() {
  test_channel_plan();
  test_channel_levels();
  test_monitor_log();
  test_scan_stop_hang_resume();
  test_priority_lockout_skip_hold();
  test_max_hold();
  test_max_hold_times_each_reply();
  test_clock_wrap();
  CHECK(Scanner::self_check());
  CHECK(std::string_view(state_name(State::hang)) == "HANG");
  std::puts("cb_scanner_tests: PASS");
  return 0;
}
