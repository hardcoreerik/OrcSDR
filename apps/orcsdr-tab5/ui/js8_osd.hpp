#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::osd {

// Bounded ordered-statistics decoding for the JS8 LDPC(174,87) code. The code's parity-check rows are dense (about 45 variables per check), which
// suits belief propagation poorly; OSD works from the code's generator instead. Evidence that it is needed: on the real 40 m capture the weakest
// station (K8IMT, hard-decision syndrome 50 of 87) is not recovered by belief propagation but is recovered here.
//
// Method: order the 174 bits by reliability |LLR|; row-reduce the generator so that the 87 most reliable independent bits carry an identity;
// re-encode from their hard decisions (order 0) and from every combination of up to `max_order` flips of those bits. Every candidate is a codeword by
// construction, so only the caller's acceptance test (the CRC) decides. No heap use; fixed work: 1 + 87 + 3741 candidates at order 2.
//
// False-accept control: with a 12-bit CRC a random codeword passes with probability 1/4096, so the more candidates are tried the more likely a wrong
// word passes. The search therefore stops at the first order that yields an accepted word, and among the accepted words of that order returns the one
// closest to the received hard decisions (smallest reliability-weighted disagreement).
constexpr size_t kBits = 174;
constexpr size_t kInfo = 87;
constexpr size_t kWords = 3;   // 174 bits in three 64-bit words

struct Workspace {
  uint64_t rows[kInfo][kWords]{};    // reduced generator rows, original column order
  uint16_t order[kBits]{};           // bit indices, most reliable first
  uint16_t pivot[kInfo]{};           // pivot column of each row
};

struct Config {
  uint8_t max_order = 2;   // 0..3
};

using AcceptFn = bool (*)(const uint8_t* codeword, void* context);

struct Result {
  bool found = false;
  uint8_t order = 0;                 // number of flipped bits of the accepted candidate
  uint32_t tested = 0;               // candidates examined
  uint32_t accepted = 0;             // candidates the acceptance test passed at the order that succeeded
  float discrepancy = 1.0f;          // reliability-weighted fraction of bits where the word disagrees with the hard decisions (0 = identical)
  uint16_t bit_corrections = 0;      // number of bits where the word differs from the hard decisions
  std::array<uint8_t, kBits> codeword{};
};

bool decode(const float* llr, const Config& config, Workspace* workspace, AcceptFn accept, void* context, Result* result);

bool self_check();

}  // namespace orcsdr::js8::osd
