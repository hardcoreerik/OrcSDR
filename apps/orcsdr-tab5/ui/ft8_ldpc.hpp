#pragma once

#include "ft8_codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8::ldpc {

constexpr std::size_t kCheckCount = 83;
using Syndrome = std::array<uint8_t, kCheckCount>;

codec::CodewordBits encode(const codec::MessageBits& message);
Syndrome syndrome(const codec::CodewordBits& codeword);
std::size_t syndrome_weight(const Syndrome& value);
bool valid(const codec::CodewordBits& codeword);
bool self_check();

}  // namespace orcsdr::ft8::ldpc
