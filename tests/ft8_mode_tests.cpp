#include "ft8_mode.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

using namespace orcsdr::ftx;

static void test_ft8() {
  const auto& p = profile(Mode::ft8);
  assert(std::strcmp(p.name, "FT8") == 0);
  assert(p.slot_ms == 15000);
  assert(p.symbol_samples == 1920);
  assert(symbol_duration_us(p) == 160000);
  assert(p.channel_symbols == 79);
  assert(p.data_symbols == 58);
  assert(p.tone_count == 8);
  assert(p.tone_spacing_millihz == 6250);
  assert(occupied_bandwidth_millihz(p) == 50000);
  assert(p.sync_block_count == 3);
  assert(p.sync[0].first_symbol == 0 && p.sync[1].first_symbol == 36 &&
         p.sync[2].first_symbol == 72);
  const uint8_t expected[7] = {3, 1, 4, 0, 6, 5, 2};
  for (int b = 0; b < 3; ++b)
    for (int i = 0; i < 7; ++i) assert(p.sync[b].tones[i] == expected[i]);
  assert(p.code_family == CodeFamily::ftx_77_crc14_ldpc174_91);
  assert(p.payload_transform == PayloadTransform::none);
  assert(implementation_ready(Mode::ft8));
}

static void test_ft4() {
  const auto& p = profile(Mode::ft4);
  assert(std::strcmp(p.name, "FT4") == 0);
  assert(p.slot_ms == 7500);
  assert(p.symbol_samples == 576);
  assert(symbol_duration_us(p) == 48000);
  assert(p.channel_symbols == 105);
  assert(p.data_symbols == 87);
  assert(p.ramp_symbols == 2);
  assert(p.tone_count == 4);
  assert(p.bits_per_tone == 2);
  assert(p.tone_spacing_millihz == 20833);
  assert(occupied_bandwidth_millihz(p) == 83332);
  assert(p.sync_block_count == 4);
  assert(p.sync[0].first_symbol == 1);
  assert(p.sync[1].first_symbol == 34);
  assert(p.sync[2].first_symbol == 67);
  assert(p.sync[3].first_symbol == 100);
  const uint8_t expected[4][4] = {
      {0, 1, 3, 2}, {1, 0, 2, 3}, {2, 3, 1, 0}, {3, 2, 0, 1}};
  for (int b = 0; b < 4; ++b)
    for (int i = 0; i < 4; ++i) assert(p.sync[b].tones[i] == expected[b][i]);
  assert(p.code_family == CodeFamily::ftx_77_crc14_ldpc174_91);
  assert(p.payload_transform == PayloadTransform::ft4_xor);
  assert(implementation_ready(Mode::ft4));
}

static void test_js8_profiles_are_descriptive_not_enabled() {
  const auto& normal = profile(Mode::js8_normal);
  assert(normal.slot_ms == 15000 && normal.symbol_samples == 1920);
  assert(normal.sync_block_count == 3);
  assert(normal.code_family == CodeFamily::js8_75_crc12_ldpc174_87);
  assert(normal.readiness == Readiness::research_pending);

  const auto& fast = profile(Mode::js8_fast);
  assert(fast.slot_ms == 10000 && fast.symbol_samples == 1200 &&
         fast.tone_spacing_millihz == 10000);
  assert(fast.sync_block_count == 0 && fast.readiness == Readiness::research_pending);

  const auto& js840 = profile(Mode::js8_40);
  assert(js840.slot_ms == 6000 && js840.symbol_samples == 600 &&
         js840.tone_spacing_millihz == 20000);

  const auto& slow = profile(Mode::js8_slow);
  assert(slow.slot_ms == 30000 && slow.symbol_samples == 3840 &&
         slow.tone_spacing_millihz == 3125);

  const auto& ultra = profile(Mode::js8_60_experimental);
  assert(ultra.slot_ms == 4000 && ultra.symbol_samples == 384 &&
         ultra.tone_spacing_millihz == 31250);
  assert(ultra.readiness == Readiness::experimental);

  assert(!implementation_ready(Mode::js8_normal));
  assert(!implementation_ready(Mode::js8_fast));
  assert(!implementation_ready(Mode::js8_40));
  assert(!implementation_ready(Mode::js8_slow));
  assert(!implementation_ready(Mode::js8_60_experimental));
}

static void test_invalid_profile_rejected() {
  auto p = profile(Mode::ft8);
  p.tone_count = 7;
  assert(!profile_valid(p));
  p = profile(Mode::ft8);
  p.sync[0].tones[0] = 8;
  assert(!profile_valid(p));
  p = profile(Mode::ft8);
  p.sync[2].first_symbol = 78;
  assert(!profile_valid(p));
}

int main() {
  assert(self_check());
  test_ft8();
  test_ft4();
  test_js8_profiles_are_descriptive_not_enabled();
  test_invalid_profile_rejected();
  return 0;
}
