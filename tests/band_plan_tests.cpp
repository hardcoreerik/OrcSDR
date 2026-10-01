#include <cstdio>
#include <cstdlib>

#include "band_plan.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

using namespace orcsdr::band_plan;

void test_demod_choice() {
  CHECK(demod_for(96100000) == Demod::wfm);
  CHECK(demod_for(87500000) == Demod::wfm && demod_for(108000000) == Demod::wfm);
  CHECK(demod_for(87499999) == Demod::nfm);
  CHECK(demod_for(108000001) == Demod::nfm);   // aero nav 108-118 is not broadcast FM
  CHECK(demod_for(118000000) == Demod::am && demod_for(121500000) == Demod::am);
  CHECK(demod_for(136975000) == Demod::am && demod_for(137000000) == Demod::am);
  CHECK(demod_for(137000001) == Demod::nfm);
  CHECK(demod_for(7074000) == Demod::am && demod_for(530000) == Demod::am && demod_for(30000000) == Demod::am);
  CHECK(demod_for(30000001) == Demod::nfm);
  CHECK(demod_for(162475000) == Demod::nfm && demod_for(462562500) == Demod::nfm);
  CHECK(demod_for(1090000000) == Demod::nfm);
  CHECK(demod_name(Demod::wfm)[0] == 'W' && demod_name(Demod::am)[0] == 'A' && demod_name(Demod::nfm)[0] == 'N');
}

void test_clamp_and_step() {
  CHECK(clamp(0) == kMinHz && clamp(5) == kMinHz);
  CHECK(clamp(2000000000u) == kMaxHz);
  CHECK(clamp(96100000) == 96100000);
  CHECK(step_frequency(96100000, 100000, 1) == 96200000);
  CHECK(step_frequency(96100000, 100000, -1) == 96000000);
  CHECK(step_frequency(kMaxHz - 1000, 1000000, 1) == kMaxHz);      // stops at the top
  CHECK(step_frequency(kMinHz + 100, 1000000, -1) == kMinHz);      // stops at the bottom
  CHECK(step_frequency(kMinHz, 1000, -1) == kMinHz);
  CHECK(step_frequency(1765999999u, 4000000000u, 1) == kMaxHz);    // no overflow
}

void test_step_cycle() {
  CHECK(cycle_step(12500, 1) == 25000);
  CHECK(cycle_step(12500, -1) == 10000);
  CHECK(cycle_step(1000, -1) == 1000);                  // stays at the smallest
  CHECK(cycle_step(1000000, 1) == 1000000);             // stays at the largest
  CHECK(cycle_step(12000, 1) == 12500);                 // an unlisted value snaps first
  CHECK(cycle_step(kDefaultStepHz, 0) == 10000);        // direction 0 behaves as down
  uint32_t step = 1000;
  for (int i = 0; i < 20; ++i) step = cycle_step(step, 1);
  CHECK(step == 1000000);
}

void test_parse() {
  CHECK(parse_mhz("96.1") == 96100000);
  CHECK(parse_mhz("118.9") == 118900000);
  CHECK(parse_mhz("118.925") == 118925000);
  CHECK(parse_mhz("7.074") == 7074000);
  CHECK(parse_mhz("0.5") == 500000 && parse_mhz(".5") == 500000);
  CHECK(parse_mhz("0.024") == 24000);
  CHECK(parse_mhz("1090") == 1090000000);               // below 2000 is MHz
  CHECK(parse_mhz("1766") == 1766000000);
  CHECK(parse_mhz("5000") == 5000000);                  // 2000 or more without a point is kHz
  CHECK(parse_mhz("14200") == 14200000);
  CHECK(parse_mhz("162.4755") == 162475500);
  CHECK(parse_mhz("1.23456789") == 1234567);            // sub-hertz digits are dropped
  CHECK(parse_mhz("1767") == 0);                        // above the driver's range
  CHECK(parse_mhz("0.023") == 0);                       // below 24 kHz
  CHECK(parse_mhz("") == 0 && parse_mhz(nullptr) == 0 && parse_mhz(".") == 0);
  CHECK(parse_mhz("1.2.3") == 0 && parse_mhz("12a") == 0 && parse_mhz("-5") == 0);
  CHECK(parse_mhz("99999999999999999999") == 0);        // overflow guard
}

}  // namespace

int main() {
  test_demod_choice();
  test_clamp_and_step();
  test_step_cycle();
  test_parse();
  std::puts("band_plan_tests: PASS");
  return 0;
}
