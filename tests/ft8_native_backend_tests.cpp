#include "ft8_native_backend.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace orcsdr::ftx;

int main() {
  assert(native::self_check());
  native::Backend b;
  native::Config config;
  assert(b.begin(12000, Mode::ft8, config));
  orcsdr::ft8::Decode out[4];
  // Silence: nothing is decoded and nothing crashes; a slot shorter than a frame reports it.
  assert(b.begin_slot(1791440160000ull));
  std::vector<int16_t> quiet(180000, 0);
  assert(b.offer_audio(quiet.data(), quiet.size()));
  assert(b.finish_slot(out, 4) == 0);
  assert(b.begin_slot(1791440175000ull));
  assert(b.offer_audio(quiet.data(), 1000));
  assert(b.finish_slot(out, 4) == 0 && b.stats().slot_too_short);
  // An incomplete slot is skipped, never reported as a quiet decode.
  assert(b.begin_slot(1791440190000ull));
  assert(b.offer_audio(quiet.data(), quiet.size()));
  assert(b.finish_slot(out, 4, true) == 0);
  // FT4 mode switch and JS8 refusal through the seam.
  const auto seam = native::make_seam(&b);
  assert(orcsdr::ft8::backend_set_mode(seam, orcsdr::ft8::DigitalMode::ft4));
  assert(!orcsdr::ft8::backend_set_mode(seam, orcsdr::ft8::DigitalMode::js8_normal));
  assert(orcsdr::ft8::backend_set_mode(seam, orcsdr::ft8::DigitalMode::ft8));
  std::puts("ft8_native_backend_tests: PASS");
  return 0;
}
