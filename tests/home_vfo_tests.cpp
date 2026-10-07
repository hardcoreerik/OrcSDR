#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "home_vfo.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

using namespace orcsdr::home_vfo;
namespace bp = orcsdr::band_profile;
constexpr bp::Region us = bp::Region::us;

void test_demod_codes() {
  // These are the OrcDial's mode codes on the wire.
  CHECK(static_cast<int>(Demod::by_band) == 0 && static_cast<int>(Demod::nfm) == 1 && static_cast<int>(Demod::am) == 2);
  CHECK(static_cast<int>(Demod::wfm) == 3 && static_cast<int>(Demod::usb) == 4 && static_cast<int>(Demod::lsb) == 5);
  CHECK(std::strcmp(demod_name(Demod::by_band), "AUTO") == 0 && std::strcmp(demod_name(Demod::wfm), "WFM") == 0);
  CHECK(std::strcmp(demod_name(Demod::lsb), "LSB") == 0);
  CHECK(demod_for_mode(bp::Mode::wfm) == Demod::wfm && demod_for_mode(bp::Mode::usb) == Demod::usb);
  CHECK(demod_for_mode(bp::Mode::none) == Demod::by_band);
}

void test_filter_kind() {
  using Kind = orcsdr::filter_standards::Kind;
  CHECK(filter_kind(Demod::wfm, 96100000) == Kind::wfm);
  CHECK(filter_kind(Demod::nfm, 146520000) == Kind::nfm);
  CHECK(filter_kind(Demod::usb, 28400000) == Kind::cb_ssb && filter_kind(Demod::lsb, 7100000) == Kind::cb_ssb);
  CHECK(filter_kind(Demod::am, 121500000) == Kind::airband_am);     // airband width inside 118 to 137 MHz
  CHECK(filter_kind(Demod::am, 117999999) == Kind::am_broadcast);
  CHECK(filter_kind(Demod::am, 137000001) == Kind::am_broadcast);
  CHECK(filter_kind(Demod::by_band, 100000000) == Kind::nfm);       // never `fixed`
}

void test_steps() {
  Memory memory;
  const auto& fm = bp::resolve(us, 96100000);
  const auto& uhf = bp::resolve(us, 444150000);
  CHECK(memory.step_hz(fm) == bp::default_step_hz(fm));
  CHECK(memory.remember_step(fm, 100000) && memory.step_hz(fm) == 100000);
  CHECK(!memory.remember_step(fm, 100000));                         // unchanged: nothing to store
  CHECK(memory.step_hz(uhf) == bp::default_step_hz(uhf));           // other bands are independent
  // A remembered value that is not on the list falls back to the default.
  CHECK(memory.remember_step(fm, 12345) && memory.step_hz(fm) == bp::default_step_hz(fm));

  // Loading drops values that are not on their band's list and keeps the rest.
  uint32_t stored[Memory::kBands] = {};
  stored[static_cast<size_t>(fm.id)] = 500000;
  stored[static_cast<size_t>(uhf.id)] = 7;                          // invalid
  Memory loaded;
  loaded.load_steps(stored, Memory::kBands);
  CHECK(loaded.step_hz(fm) == 500000 && loaded.step_hz(uhf) == bp::default_step_hz(uhf));
  loaded.load_steps(nullptr, 0);
  CHECK(loaded.step_hz(fm) == bp::default_step_hz(fm));
  CHECK(Memory::kStepsBytes == Memory::kBands * 4);
}

void test_pins_and_auto() {
  Memory memory;
  const auto& fm = bp::resolve(us, 96100000);
  const auto& air = bp::resolve(us, 121500000);
  const auto& ham10 = bp::resolve(us, 28400000);
  Demod mode = Demod::by_band;

  // Entering a band applies its usual mode; staying in it changes nothing.
  CHECK(memory.enter_band(fm, &mode) && mode == Demod::wfm);
  CHECK(!memory.enter_band(fm, &mode));
  CHECK(memory.enter_band(air, &mode) && mode == Demod::am);
  CHECK(memory.enter_band(ham10, &mode) && mode == Demod::usb);

  // A pinned mode wins on entering the band, and AUTO (by_band) clears it.
  CHECK(memory.set_pin(air, Demod::nfm) && memory.pin(air) == static_cast<uint8_t>(Demod::nfm));
  CHECK(!memory.set_pin(air, Demod::nfm));
  CHECK(memory.enter_band(air, &mode) && mode == Demod::nfm);
  CHECK(memory.set_pin(air, Demod::by_band) && memory.pin(air) == 0);
  memory.forget_band();
  CHECK(memory.enter_band(air, &mode) && mode == Demod::am);

  // A manual choice marks the band as entered, so the next tune inside it keeps the choice.
  memory.note_band(fm);
  CHECK(!memory.enter_band(fm, &mode));
  memory.forget_band();                                              // Home came back on screen
  CHECK(memory.enter_band(fm, &mode) && mode == Demod::wfm);

  // Bands with no usual mode leave it to the band.
  const auto& adsb = bp::resolve(us, 1090000000);
  CHECK(memory.enter_band(adsb, &mode) && mode == Demod::by_band);
  CHECK(memory.enter_band(fm, nullptr));                             // a null out-parameter is accepted
}

void test_pin_storage() {
  uint8_t stored[Memory::kBands] = {};
  stored[3] = static_cast<uint8_t>(Demod::lsb);
  stored[4] = 6;                                                     // not a mode: dropped
  stored[5] = 200;
  Memory memory;
  memory.load_pins(stored, Memory::kBands);
  CHECK(memory.pins_data()[3] == 5 && memory.pins_data()[4] == 0 && memory.pins_data()[5] == 0);
  memory.load_pins(stored, 4);                                       // a shorter table leaves the rest cleared
  CHECK(memory.pins_data()[3] == 5 && memory.pins_data()[10] == 0);
  memory.load_pins(nullptr, 0);
  CHECK(memory.pins_data()[3] == 0);
  CHECK(Memory::kPinsBytes == Memory::kBands);
}

}  // namespace

int main() {
  CHECK(orcsdr::home_vfo::self_check());
  test_demod_codes();
  test_filter_kind();
  test_steps();
  test_pins_and_auto();
  test_pin_storage();
  std::printf("HOME_VFO_TESTS pass\n");
  return 0;
}
