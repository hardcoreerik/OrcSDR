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

  const uint32_t target = scanner.service(0, bank[0].frequency_hz, -100.0f);
  CHECK(target == bank[1].frequency_hz);
  scanner.note_retuned(0, target);
  CHECK(scanner.state() == ScanState::settling);
  CHECK(scanner.service(49, target, -60.0f) == 0);
  CHECK(scanner.state() == ScanState::settling);
  CHECK(scanner.service(50, target, -60.0f) == 0);
  CHECK(scanner.state() == ScanState::receiving);
  CHECK(scanner.stops() == 1);

  CHECK(scanner.service(200, target, -58.0f) == 0);
  CHECK(scanner.service(500, target, -100.0f) == 0);
  CHECK(scanner.state() == ScanState::hang);
  CHECK(scanner.service(1000, target, -100.0f) == 0);
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
  const uint32_t first = scanner.service(0, 124900000, -100.0f);
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

}  // namespace

int main() {
  test_channel_rasters();
  test_airport_bank_scan();
  test_guard_and_controls();
  CHECK(orcsdr::airband::Scanner::self_check());
  std::puts("airband_scanner_tests: PASS");
  return 0;
}
