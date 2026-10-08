#include "ft8_mode.hpp"

#include <algorithm>

namespace orcsdr::ftx {
namespace {

constexpr SyncBlock sync7(uint16_t first, std::array<uint8_t, 7> tones) {
  return SyncBlock{first, 7, tones};
}

constexpr SyncBlock sync4(uint16_t first, std::array<uint8_t, 4> tones) {
  SyncBlock block{};
  block.first_symbol = first;
  block.length = 4;
  for (std::size_t i = 0; i < tones.size(); ++i) block.tones[i] = tones[i];
  return block;
}

constexpr std::array<uint8_t, 7> kFt8Costas{{3, 1, 4, 0, 6, 5, 2}};

constexpr ModeProfile make_ft8() {
  ModeProfile p{};
  p.mode = Mode::ft8;
  p.name = "FT8";
  p.sample_rate_hz = 12000;
  p.slot_ms = 15000;
  p.symbol_samples = 1920;
  p.channel_symbols = 79;
  p.data_symbols = 58;
  p.ramp_symbols = 0;
  p.tone_count = 8;
  p.bits_per_tone = 3;
  p.tone_spacing_millihz = 6250;
  p.tone_bits = {{0, 1, 3, 2, 6, 4, 5, 7}};
  p.data_block_count = 2;
  p.data[0] = DataBlock{7, 29};
  p.data[1] = DataBlock{43, 29};
  p.sync_family = SyncFamily::ft8_costas_7;
  p.sync_block_count = 3;
  p.sync[0] = sync7(0, kFt8Costas);
  p.sync[1] = sync7(36, kFt8Costas);
  p.sync[2] = sync7(72, kFt8Costas);
  p.code_family = CodeFamily::ftx_77_crc14_ldpc174_91;
  p.payload_transform = PayloadTransform::none;
  p.readiness = Readiness::implementation_ready;
  return p;
}

constexpr ModeProfile make_ft4() {
  ModeProfile p{};
  p.mode = Mode::ft4;
  p.name = "FT4";
  p.sample_rate_hz = 12000;
  p.slot_ms = 7500;
  p.symbol_samples = 576;
  p.channel_symbols = 105;
  p.data_symbols = 87;
  p.ramp_symbols = 2;
  p.tone_count = 4;
  p.bits_per_tone = 2;
  p.tone_spacing_millihz = 20833;
  p.tone_bits = {{0, 1, 3, 2, 0, 0, 0, 0}};
  p.data_block_count = 3;
  p.data[0] = DataBlock{5, 29};
  p.data[1] = DataBlock{38, 29};
  p.data[2] = DataBlock{71, 29};
  p.sync_family = SyncFamily::ft4_costas_4;
  p.sync_block_count = 4;
  // FT4 includes one ramp symbol before the first sync and one after the frame.
  p.sync[0] = sync4(1, {{0, 1, 3, 2}});
  p.sync[1] = sync4(34, {{1, 0, 2, 3}});
  p.sync[2] = sync4(67, {{2, 3, 1, 0}});
  p.sync[3] = sync4(100, {{3, 2, 0, 1}});
  p.code_family = CodeFamily::ftx_77_crc14_ldpc174_91;
  p.payload_transform = PayloadTransform::ft4_xor;
  p.readiness = Readiness::implementation_ready;
  return p;
}

constexpr ModeProfile make_js8(Mode mode, const char* name, uint32_t slot_ms,
                               uint16_t symbol_samples, uint32_t spacing_millihz,
                               bool normal, Readiness readiness) {
  ModeProfile p{};
  p.mode = mode;
  p.name = name;
  p.sample_rate_hz = 12000;
  p.slot_ms = slot_ms;
  p.symbol_samples = symbol_samples;
  p.channel_symbols = 79;
  p.data_symbols = 58;
  p.ramp_symbols = 0;
  p.tone_count = 8;
  p.bits_per_tone = 3;
  p.tone_spacing_millihz = spacing_millihz;
  p.sync_family = normal ? SyncFamily::js8_original_7 : SyncFamily::js8_modified_7;
  // Only Normal-mode sync is independently identical to the published FT8-family
  // sequence. Modified JS8 arrays stay unset until an acceptable independent
  // protocol source or interoperability derivation exists.
  if (normal) {
    p.sync_block_count = 3;
    p.sync[0] = sync7(0, kFt8Costas);
    p.sync[1] = sync7(36, kFt8Costas);
    p.sync[2] = sync7(72, kFt8Costas);
  } else {
    p.sync_block_count = 0;
  }
  p.code_family = CodeFamily::js8_75_crc12_ldpc174_87;
  p.payload_transform = PayloadTransform::none;
  p.readiness = readiness;
  return p;
}

constexpr std::array<ModeProfile, kModeCount> kProfiles{{
    make_ft8(),
    make_ft4(),
    make_js8(Mode::js8_normal, "JS8 NORMAL", 15000, 1920, 6250, true,
             Readiness::research_pending),
    make_js8(Mode::js8_fast, "JS8 FAST", 10000, 1200, 10000, false,
             Readiness::research_pending),
    make_js8(Mode::js8_40, "JS8 40", 6000, 600, 20000, false,
             Readiness::research_pending),
    make_js8(Mode::js8_slow, "JS8 SLOW", 30000, 3840, 3125, false,
             Readiness::research_pending),
    make_js8(Mode::js8_60_experimental, "JS8 60", 4000, 384, 31250, false,
             Readiness::experimental),
}};

constexpr bool is_power_of_two(uint8_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace

const ModeProfile& profile(Mode mode) {
  const auto index = static_cast<std::size_t>(mode);
  return index < kProfiles.size() ? kProfiles[index] : kProfiles[0];
}

bool profile_valid(const ModeProfile& p) {
  if (p.name == nullptr || p.name[0] == '\0') return false;
  if (p.sample_rate_hz == 0 || p.slot_ms == 0 || p.symbol_samples == 0 ||
      p.channel_symbols == 0 || p.data_symbols == 0) return false;
  if (!is_power_of_two(p.tone_count) || p.tone_count < 2 || p.bits_per_tone == 0) return false;
  if ((1u << p.bits_per_tone) != p.tone_count || p.tone_spacing_millihz == 0) return false;
  if (p.data_symbols >= p.channel_symbols) return false;
  if (p.sync_block_count > p.sync.size() || p.data_block_count > p.data.size())
    return false;

  if (p.readiness == Readiness::implementation_ready) {
    if (p.sync_block_count == 0 || p.data_block_count == 0) return false;
    bool labels[8]{};
    const uint8_t label_limit = static_cast<uint8_t>(1u << p.bits_per_tone);
    for (uint8_t tone = 0; tone < p.tone_count; ++tone) {
      const uint8_t label = p.tone_bits[tone];
      if (label >= label_limit || labels[label]) return false;
      labels[label] = true;
    }
  }

  std::array<bool, 128> occupied{};
  if (p.channel_symbols > occupied.size()) return false;
  uint16_t data_count = 0;
  for (std::size_t i = 0; i < p.data_block_count; ++i) {
    const auto& block = p.data[i];
    if (block.length == 0 ||
        static_cast<uint32_t>(block.first_symbol) + block.length >
            p.channel_symbols)
      return false;
    for (std::size_t k = 0; k < block.length; ++k) {
      const std::size_t symbol = block.first_symbol + k;
      if (occupied[symbol]) return false;
      occupied[symbol] = true;
      ++data_count;
    }
  }
  if (p.readiness == Readiness::implementation_ready &&
      data_count != p.data_symbols)
    return false;

  uint16_t sync_count = 0;
  for (std::size_t i = 0; i < p.sync_block_count; ++i) {
    const auto& block = p.sync[i];
    if (block.length == 0 || block.length > block.tones.size()) return false;
    if (static_cast<uint32_t>(block.first_symbol) + block.length >
        p.channel_symbols)
      return false;
    for (std::size_t t = 0; t < block.length; ++t) {
      if (block.tones[t] >= p.tone_count) return false;
      const std::size_t symbol = block.first_symbol + t;
      if (occupied[symbol]) return false;
      occupied[symbol] = true;
      ++sync_count;
    }
  }

  if (p.readiness == Readiness::implementation_ready &&
      static_cast<uint16_t>(data_count + sync_count + p.ramp_symbols) !=
          p.channel_symbols)
    return false;
  return true;
}

bool implementation_ready(Mode mode) {
  return profile(mode).readiness == Readiness::implementation_ready;
}

const char* mode_name(Mode mode) {
  return profile(mode).name;
}

uint32_t symbol_duration_us(const ModeProfile& p) {
  if (p.sample_rate_hz == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(p.symbol_samples) * 1000000ull +
                                p.sample_rate_hz / 2u) /
                               p.sample_rate_hz);
}

uint32_t occupied_bandwidth_millihz(const ModeProfile& p) {
  return p.tone_count * p.tone_spacing_millihz;
}

bool self_check() {
  for (std::size_t i = 0; i < kProfiles.size(); ++i) {
    if (!profile_valid(kProfiles[i])) return false;
    if (static_cast<std::size_t>(kProfiles[i].mode) != i) return false;
  }
  if (!implementation_ready(Mode::ft8) || !implementation_ready(Mode::ft4)) return false;
  if (implementation_ready(Mode::js8_normal) || implementation_ready(Mode::js8_fast) ||
      implementation_ready(Mode::js8_40) || implementation_ready(Mode::js8_slow) ||
      implementation_ready(Mode::js8_60_experimental)) return false;
  return true;
}

}  // namespace orcsdr::ftx
