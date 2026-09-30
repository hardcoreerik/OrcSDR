#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "airband_scanner.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

void test_channel_rasters() {
  using namespace orcsdr::airband;
  CHECK(in_band(kMinFrequencyHz));
  CHECK(in_band(kMaxFrequencyHz));
  CHECK(!in_band(kMinFrequencyHz - 1));
  CHECK(spacing_hz(Spacing::khz25) == 25000);
  CHECK(spacing_hz(Spacing::khz833) == 8333);
  CHECK(step_frequency(118000000, 1, Spacing::khz833) == 118008333);
  CHECK(step_frequency(118008333, 1, Spacing::khz833) == 118016667);
  CHECK(step_frequency(118016667, 1, Spacing::khz833) == 118025000);
  CHECK(step_frequency(118025000, -1, Spacing::khz833) == 118016667);
  CHECK(snap_frequency(121501000, Spacing::khz25) == 121500000);
  CHECK(step_frequency(kMaxFrequencyHz, 1, Spacing::khz833) == kMaxFrequencyHz);
}

void test_airport_bank_scan() {
  using namespace orcsdr::airband;
  Scanner scanner;
  scanner.settings().priority_guard = false;
  scanner.settings().settle_ms = 50;
  scanner.settings().hang_ms = 500;
  BankEntry bank[3]{};
  bank[0].frequency_hz = 118700000;
  std::strcpy(bank[0].label, "TEST ATIS");
  bank[1].frequency_hz = 121900000;
  std::strcpy(bank[1].label, "TEST GROUND");
  bank[2].frequency_hz = 124900000;
  std::strcpy(bank[2].label, "TEST TOWER");
  scanner.set_bank(bank, 3);
  scanner.start(0, bank[0].frequency_hz);

  const uint32_t target = scanner.service(0, bank[0].frequency_hz, 0.0f);
  CHECK(target == bank[1].frequency_hz);
  scanner.note_retuned(0, target);
  CHECK(scanner.state() == ScanState::settling);
  CHECK(scanner.service(49, target, 20.0f) == 0);
  CHECK(scanner.state() == ScanState::settling);
  CHECK(scanner.service(50, target, 20.0f) == 0);
  CHECK(scanner.state() == ScanState::receiving);
  CHECK(scanner.stops() == 1);

  CHECK(scanner.service(200, target, -58.0f) == 0);
  CHECK(scanner.service(500, target, 0.0f) == 0);
  CHECK(scanner.state() == ScanState::hang);
  CHECK(scanner.service(1000, target, 0.0f) == 0);
  CHECK(scanner.state() == ScanState::scanning);
  CHECK(scanner.activity_count() == 1);
  CHECK(scanner.activity(0)->frequency_hz == target);
  CHECK(std::string_view(scanner.activity(0)->label) == "TEST GROUND");
}

void test_guard_and_controls() {
  using namespace orcsdr::airband;
  Scanner scanner;
  scanner.settings().priority_guard = true;
  scanner.settings().priority_every = 1;
  BankEntry bank{};
  bank.frequency_hz = 124900000;
  std::strcpy(bank.label, "TEST TOWER");
  scanner.set_bank(&bank, 1);
  CHECK(scanner.bank_count() == 2);
  scanner.start(0, 124900000);
  const uint32_t first = scanner.service(0, 124900000, 0.0f);
  CHECK(first == kGuardFrequencyHz || first == 124900000);
  scanner.hold(1, 124900000);
  CHECK(scanner.state() == ScanState::held);
  scanner.resume(2);
  CHECK(scanner.state() == ScanState::scanning);
  scanner.skip(3, 124900000);
  CHECK(scanner.state() == ScanState::scanning);
  scanner.stop();
  CHECK(scanner.state() == ScanState::off);
}

void test_channel_squelch() {
  using namespace orcsdr::airband;
  CHECK(std::fabs(carrier_level_db(127.5f)) < 0.01f);
  CHECK(std::fabs(carrier_level_db(12.75f) + 20.0f) < 0.01f);
  CHECK(std::isfinite(carrier_level_db(0.0f)));

  ChannelSquelch sq;
  sq.set_threshold_db(8);
  sq.reset();
  uint32_t now = 0;
  // Noise with +/-1.5 dB jitter around -38 dBFS never opens the squelch.
  for (int i = 0; i < 400; ++i, now += 30)
    sq.update(now, -38.0f + ((i * 7) % 5 - 2) * 0.75f);
  CHECK(sq.floor_valid());
  CHECK(!sq.open());
  CHECK(sq.snr_db() < 4.0f);
  // A carrier 14 dB above the floor opens it...
  for (int i = 0; i < 10; ++i, now += 30) sq.update(now, -24.0f);
  CHECK(sq.open());
  CHECK(sq.snr_db() > 10.0f);
  // ...and it stays open through a fade to just above the close point (hysteresis)...
  for (int i = 0; i < 10; ++i, now += 30) sq.update(now, -38.0f + 7.0f);
  CHECK(sq.open());
  // ...and closes when the carrier goes away.
  for (int i = 0; i < 10; ++i, now += 30) sq.update(now, -38.0f);
  CHECK(!sq.open());
  // A long carrier must not drag the floor up and mute itself.
  for (int i = 0; i < 1000; ++i, now += 30) sq.update(now, -24.0f);
  CHECK(sq.open());
  CHECK(sq.floor_db() < -34.0f);

  // Slow upward drift (0.4 dB/s) is followed, so it never falsely opens.
  ChannelSquelch drift;
  drift.set_threshold_db(8);
  drift.reset();
  now = 0;
  for (int i = 0; i < 3000; ++i, now += 30)
    drift.update(now, -45.0f + 0.4f * static_cast<float>(now) / 1000.0f * 0.25f);
  CHECK(!drift.open());

  // A downward step (AGC pulling gain) lowers the floor at once.
  ChannelSquelch step;
  step.set_threshold_db(8);
  step.reset();
  now = 0;
  for (int i = 0; i < 50; ++i, now += 30) step.update(now, -30.0f);
  for (int i = 0; i < 50; ++i, now += 30) step.update(now, -40.0f);
  CHECK(step.floor_db() < -37.0f);

  // Threshold 0 means always open; reset clears the floor but keeps that.
  ChannelSquelch always;
  always.set_threshold_db(0);
  always.reset();
  always.update(0, -60.0f);
  CHECK(always.open());
  // A demodulator that has not produced a level yet (envelope ~0 => about -68 dB) must never
  // become the noise floor, or every real signal would look 40 dB strong.
  ChannelSquelch dead;
  dead.set_threshold_db(8);
  dead.reset();
  now = 0;
  for (int i = 0; i < 40; ++i, now += 30) dead.update(now, -68.1f);
  CHECK(!dead.floor_valid());
  for (int i = 0; i < 40; ++i, now += 30) dead.update(now, -36.0f);
  CHECK(dead.floor_valid());
  CHECK(std::fabs(dead.floor_db() + 36.0f) < 0.5f);
  CHECK(!dead.open());
  // After a reset the floor is re-learned only after a short warm-up.
  dead.reset();
  now += 30;
  dead.update(now, -36.0f);            // starts the warm-up window
  CHECK(!dead.floor_valid());
  now += 200;
  dead.update(now, -36.0f);
  CHECK(dead.floor_valid());
  // Non-finite input is ignored.
  ChannelSquelch bad;
  bad.set_threshold_db(8);
  bad.reset();
  bad.update(0, NAN);
  CHECK(!bad.floor_valid());
}

void test_filter_bandwidth() {
  using namespace orcsdr::airband;
  CHECK(filter_bandwidth_hz(Spacing::khz25) == 10000u);
  CHECK(filter_bandwidth_hz(Spacing::khz833) == 6000u);
  CHECK(filter_bandwidth_hz(Spacing::khz833) < spacing_hz(Spacing::khz25));
}

void test_radius_choices() {
  using namespace orcsdr::airband;
  CHECK(next_radius_nm(25) == 50);
  CHECK(next_radius_nm(100) == 250);
  CHECK(next_radius_nm(500) == 0);   // then "any"
  CHECK(next_radius_nm(0) == 25);    // and back around
  CHECK(next_radius_nm(77) == 100);  // unknown values fall back to the default
}

void test_gain_steps() {
  using namespace orcsdr::airband;
  const int16_t steps[] = {0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254, 280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 445, 480, 496};
  const size_t count = sizeof(steps) / sizeof(steps[0]);
  CHECK(step_gain(steps, count, 297, 1) == 328);
  CHECK(step_gain(steps, count, 297, -1) == 280);
  CHECK(step_gain(steps, count, 300, 1) == 328);   // snaps to nearest step first
  CHECK(step_gain(steps, count, 0, -1) == 0);      // clamps at the bottom
  CHECK(step_gain(steps, count, 496, 1) == 496);   // clamps at the top
  CHECK(step_gain(nullptr, 0, 123, 1) == 123);     // no table: unchanged
}

}  // namespace

int main() {
  test_channel_squelch();
  test_filter_bandwidth();
  test_radius_choices();
  test_gain_steps();
  test_channel_rasters();
  test_airport_bank_scan();
  test_guard_and_controls();
  CHECK(orcsdr::airband::Scanner::self_check());
  std::puts("airband_scanner_tests: PASS");
  return 0;
}
