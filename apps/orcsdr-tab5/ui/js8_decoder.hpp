#pragma once

#include "js8_codec.hpp"
#include "js8_message.hpp"
#include "js8_osd.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::decoder {

// Soft-decision decoder for one JS8 Normal frame: 79x8 tone energies in, a verified message out. Stages, each logged in Result:
//   1. LLRs from the tone energies (codec::tone_llrs) and the hard-decision syndrome weight of the raw codeword.
//   2. Belief propagation on the LDPC(174,87) graph (generic js8::fec engine, graph generated from docs/js8/spec).
//   3. If BP does not reach an accepted word: ordered-statistics decoding (js8_osd) restricted to words that pass the CRC and, by default, the
//      message-plausibility rules (kind 3, flags 3, command 29, plausible callsigns, SNR present).
// A message is published only when parity (87 checks) AND the CRC-12 hold. Nothing is allocated; the caller owns the Workspace (about 33 KB; place it
// in PSRAM on the Tab5/P4).
constexpr size_t kBpEdges = 3920;

struct Workspace {
  osd::Workspace osd;
  float variable_to_check[kBpEdges];
  float check_to_variable[kBpEdges];
  float posterior[codec::kCodewordBits];
};

struct Config {
  float llr_gain = 3.0f;           // amplitude-domain gain of the tone LLRs (codec::tone_llrs)
  uint8_t bp_iterations = 25;
  bool use_osd = true;
  uint8_t osd_order = 2;
  bool osd_require_rendered = true;   // OSD words must also be a plausible, renderable message (false-accept control)
  float max_osd_discrepancy = 0.065f;  // reject OSD words further than this reliability-weighted fraction from the hard decisions (synthetic: noise-only best words 0.078-0.14, true words near threshold <= 0.06)
};

enum class Method : uint8_t { none = 0, hard = 1, bp = 2, osd = 3 };

struct Result {
  bool crc_valid = false;            // parity and CRC both hold (the word is a valid JS8 frame)
  bool rendered = false;             // ... and it is a message this decoder can print
  Method method = Method::none;
  uint16_t initial_syndrome = 0;     // unsatisfied checks of the raw hard decisions
  uint16_t final_syndrome = 0;       // of the published word (0 when crc_valid)
  uint8_t bp_iterations = 0;
  uint8_t osd_order = 0;
  uint32_t osd_tested = 0;
  uint16_t hard_corrections = 0;     // bits where the published word differs from the raw hard decisions
  float osd_discrepancy = 0.0f;
  codec::Fields fields{};
  message::Rendered message{};
  uint8_t codeword[codec::kCodewordBits]{};
};

bool decode(const float energy[codec::kChannelTones][8], const Config& config, Workspace* workspace, Result* result);
// Same from precomputed LLRs (positive = bit 0).
bool decode_llrs(const float llr[codec::kCodewordBits], const Config& config, Workspace* workspace, Result* result);

bool self_check();

}  // namespace orcsdr::js8::decoder
