#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::ftx {

enum class Mode : uint8_t {
  ft8,
  ft4,
  js8_normal,
  js8_fast,
  js8_40,
  js8_slow,
  js8_60_experimental,
  count,
};

enum class CodeFamily : uint8_t {
  ftx_77_crc14_ldpc174_91,
  js8_75_crc12_ldpc174_87,
};

enum class PayloadTransform : uint8_t {
  none,
  ft4_xor,
};

enum class SyncFamily : uint8_t {
  ft8_costas_7,
  ft4_costas_4,
  js8_original_7,
  js8_modified_7,
};

enum class Readiness : uint8_t {
  implementation_ready,
  research_pending,
  experimental,
};

struct SyncBlock {
  uint16_t first_symbol = 0;
  uint8_t length = 0;
  std::array<uint8_t, 7> tones{};
};

struct ModeProfile {
  Mode mode = Mode::ft8;
  const char* name = "FT8";

  uint32_t sample_rate_hz = 12000;
  uint32_t slot_ms = 15000;
  uint16_t symbol_samples = 1920;
  uint16_t channel_symbols = 79;
  uint16_t data_symbols = 58;
  uint8_t ramp_symbols = 0;

  uint8_t tone_count = 8;
  uint8_t bits_per_tone = 3;
  uint32_t tone_spacing_millihz = 6250;

  SyncFamily sync_family = SyncFamily::ft8_costas_7;
  uint8_t sync_block_count = 3;
  std::array<SyncBlock, 4> sync{};

  CodeFamily code_family = CodeFamily::ftx_77_crc14_ldpc174_91;
  PayloadTransform payload_transform = PayloadTransform::none;
  Readiness readiness = Readiness::implementation_ready;
};

constexpr std::size_t kModeCount = static_cast<std::size_t>(Mode::count);

const ModeProfile& profile(Mode mode);
bool profile_valid(const ModeProfile& value);
bool implementation_ready(Mode mode);
const char* mode_name(Mode mode);
uint32_t symbol_duration_us(const ModeProfile& value);
uint32_t occupied_bandwidth_millihz(const ModeProfile& value);
bool self_check();

}  // namespace orcsdr::ftx
