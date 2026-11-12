// SPDX-License-Identifier: AGPL-3.0-or-later
// Deterministic synthetic 4FSK IQ workload; not a recording or RF evidence.
#pragma once
#include "frontend.hpp"
#include "orcsdr_dmr/d0.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dmr::device_probe {
// Two 144-dibit BS frames at 10 IQ samples per dibit (60ms total).
inline constexpr std::size_t kPatternSamples = 2880;
using Pattern = std::array<orcsdr::dmr::d1::ComplexSample,kPatternSamples>;
using DibitPattern = std::array<std::uint8_t, 288>;
void make_dibit_pattern(DibitPattern& output) noexcept;
// Prepared once outside timing. Contains valid BS sync and Golay LC type;
// deliberately does not claim valid RS/LC or on-air identity.
bool make_pattern(Pattern& out) noexcept;
} // namespace orcsdr::dmr::device_probe
