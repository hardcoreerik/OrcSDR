// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr_probe/pattern.hpp"
#include <cmath>

namespace orcsdr::dmr::device_probe {
namespace {
constexpr float kPi = 3.14159265358979323846f;
void fill_burst(DibitPattern& d, std::size_t offset, bool ts2) noexcept {
  // Exact 48-bit BS data SYNC, known Golay(20,8) CC=2 / LC type=1.
  const std::uint64_t sync = sync_word(SyncKind::bs_data);
  constexpr std::uint32_t golay = 0x21e26U;
  for (unsigned i = 0; i < 24; ++i)
    d[offset + 12 + 54 + i] = static_cast<std::uint8_t>((sync >> (46 - 2 * i)) & 3U);
  for (unsigned i = 0; i < 10; ++i) {
    const auto bit = static_cast<std::uint8_t>((golay >> (19U - i)) & 1U);
    const auto pos = 98 + i;
    const auto prev = static_cast<std::uint8_t>(d[offset + 12 + pos/2]);
    d[offset + 12 + pos/2] = static_cast<std::uint8_t>(prev | (bit << (1U - pos%2)));
  }
  for (unsigned i = 0; i < 10; ++i) {
    const auto bit = static_cast<std::uint8_t>((golay >> (9U - i)) & 1U);
    const auto pos = 156 + i;
    d[offset + 12 + pos/2] = static_cast<std::uint8_t>(d[offset + 12 + pos/2] | (bit << (1U - pos%2)));
  }
  CachFields fields{};
  fields.tdma_channel = ts2;
  fields.payload = 0x145a;
  CachWire wire{};
  if (!pack_cach(fields,wire)) return;
  for (unsigned i=0;i<12;++i)
    d[offset+i] = static_cast<std::uint8_t>((wire[i/4] >> (6-2*(i%4))) & 3U);
}
} // namespace
void make_dibit_pattern(DibitPattern& output) noexcept {
  output.fill(0);
  fill_burst(output, 0, false);
  fill_burst(output, 144, true);
}
bool make_pattern(Pattern& out) noexcept {
  // Uncorrected repeated pattern, not ETSI transmission-grade RF generation.
  DibitPattern dibits{};
  make_dibit_pattern(dibits);
  float phase = 0;
  for (std::size_t s=0;s<out.size();++s) {
    const auto d=dibits[s/10U];
    const float f=static_cast<float>(orcsdr::dmr::d1::dibit_level(d))*648.0F;
    phase+=2.0F*kPi*f/48000.0F;
    if (phase>2.0F*kPi) phase-=2.0F*kPi;
    if (phase<-2.0F*kPi) phase+=2.0F*kPi;
    out[s]={std::cos(phase),std::sin(phase)};
  }
  return true;
}
} // namespace orcsdr::dmr::device_probe
