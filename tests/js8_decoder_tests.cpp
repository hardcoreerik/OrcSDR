// JS8 Normal soft decoder tests (LLR -> BP -> OSD, parity + CRC-12). A host-only encoder is built from the same generated parity graph:
// the code is systematic with H = [I | P], so parity bit i is the XOR of the information bits in check row i. Synthetic frames use a
// non-coherent 8-FSK energy model (|signal + complex Gaussian noise|^2), so SNR sweeps and noise-only false-accept runs are repeatable.
// The real-signal evidence (WO7I/K8IMT/K7YXZ/KD7WPQ decoded from the 40 m capture) is produced by tools/js8-decode-wav.cpp and recorded
// in docs/js8/results; it is not a unit test because the capture is outside this repository.
#include "js8_codec.hpp"
#include "js8_decoder.hpp"
#include "js8_ldpc_graph.hpp"
#include "js8_message.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {
using namespace orcsdr::js8;

int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

uint64_t rng_state = 0x9E3779B97F4A7C15ull;
uint32_t rnd() {
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
  return static_cast<uint32_t>(rng_state >> 16);
}
float uniform() { return (static_cast<float>(rnd() & 0xFFFFFFu) + 0.5f) / 16777216.0f; }
float gauss() { return std::sqrt(-2.0f * std::log(uniform())) * std::cos(6.2831853f * uniform()); }

// 72 text bits from 12 characters of the 6-bit alphabet.
void text_bits(const char* text, uint8_t info[87]) {
  for (int c = 0; c < 12; ++c) {
    unsigned v = 0;
    while (v < 64 && codec::kAlphabet[v] != text[c]) ++v;
    for (int b = 0; b < 6; ++b) info[6 * c + b] = static_cast<uint8_t>((v >> (5 - b)) & 1u);
  }
}

void encode(const char* text, uint8_t flags, uint8_t codeword[174]) {
  uint8_t info[87];
  text_bits(text, info);
  info[72] = (flags >> 2) & 1u; info[73] = (flags >> 1) & 1u; info[74] = flags & 1u;
  const uint16_t crc = codec::crc12(info);
  for (int i = 0; i < 12; ++i) info[75 + i] = static_cast<uint8_t>((crc >> (11 - i)) & 1u);
  for (int i = 0; i < 87; ++i) codeword[87 + i] = info[i];
  for (size_t check = 0; check < 87; ++check) {
    uint8_t p = 0;
    for (size_t e = ldpc_graph::kCheckOffsets[check]; e < ldpc_graph::kCheckOffsets[check + 1]; ++e) {
      const size_t v = ldpc_graph::kCheckVariables[e];
      if (v >= 87) p ^= codeword[v];
    }
    codeword[check] = p;
  }
}

void frame_energies(const uint8_t codeword[174], float snr_amplitude, float energy[79][8]) {
  static const uint8_t sync[7] = {4, 2, 5, 6, 1, 3, 0};
  uint8_t tones[79];
  for (int i = 0; i < 7; ++i) tones[i] = tones[36 + i] = tones[72 + i] = sync[i];
  for (size_t s = 0; s < 58; ++s) {
    const uint8_t t = static_cast<uint8_t>((codeword[3 * s] << 2) | (codeword[3 * s + 1] << 1) | codeword[3 * s + 2]);
    tones[codec::data_tone_position(s)] = t;
  }
  for (int sym = 0; sym < 79; ++sym)
    for (int t = 0; t < 8; ++t) {
      const float re = (t == tones[sym] ? snr_amplitude : 0.0f) + gauss();
      const float im = gauss();
      energy[sym][t] = re * re + im * im;
    }
}

bool decode_text(const uint8_t cw[174], float amp, decoder::Workspace* ws, decoder::Result* r) {
  static float e[79][8];
  frame_energies(cw, amp, e);
  return decoder::decode(e, decoder::Config{}, ws, r);
}
}  // namespace

int main() {
  CHECK(decoder::self_check());
  auto ws = std::make_unique<decoder::Workspace>();

  // The encoder produces valid words: parity holds and the CRC verifies.
  uint8_t wo7i[174];
  encode("UvnVIpm34Fqg", 3, wo7i);   // the 12 raw characters of the WO7I frame in the real capture
  CHECK(codec::syndrome_weight(wo7i) == 0);
  CHECK(codec::crc_valid(wo7i));
  CHECK(codec::crc12(reinterpret_cast<const uint8_t*>("\0")) == codec::crc12(reinterpret_cast<const uint8_t*>("\0")));

  // A single flipped information bit breaks both parity and CRC.
  uint8_t bad[174];
  std::memcpy(bad, wo7i, sizeof(bad));
  bad[100] ^= 1u;
  CHECK(codec::syndrome_weight(bad) > 0);
  CHECK(!codec::crc_valid(bad));

  // Noiseless frame decodes by hard decision to the message the real signal carried.
  decoder::Result r;
  CHECK(decode_text(wo7i, 12.0f, ws.get(), &r));
  CHECK(r.crc_valid && r.rendered);
  CHECK(std::strcmp(r.message.text, "WO7I: ND7M HEARTBEAT SNR +11") == 0);
  CHECK(r.fields.kind == 3 && r.fields.flags == 3);

  // Soft decoding corrects errors: at a level where raw tone decisions fail the parity check, most frames still decode.
  int decoded = 0, raw_clean = 0, trials = 200;
  for (int i = 0; i < trials; ++i) {
    static float e[79][8];
    frame_energies(wo7i, 3.2f, e);
    uint8_t tones[79], hard[174];
    for (int s = 0; s < 79; ++s) { int b = 0; for (int t = 1; t < 8; ++t) if (e[s][t] > e[s][b]) b = t; tones[s] = static_cast<uint8_t>(b); }
    codec::hard_bits_from_tones(tones, hard);
    if (codec::syndrome_weight(hard) == 0) ++raw_clean;
    decoder::Result rr;
    if (decoder::decode(e, decoder::Config{}, ws.get(), &rr) && rr.rendered && std::strcmp(rr.message.text, "WO7I: ND7M HEARTBEAT SNR +11") == 0) { ++decoded; if (rr.method == decoder::Method::osd) std::printf("  true osd: order=%u disc=%.3f\n", rr.osd_order, (double)rr.osd_discrepancy); }
  }
  std::printf("soft: %d/%d decoded, %d/%d had a clean raw hard decision\n", decoded, trials, raw_clean, trials);
  CHECK(decoded > raw_clean + trials / 4);

  // False-accept control: pure noise and random tones must never produce a message.
  int false_messages = 0, false_valid = 0;
  const int noise_trials = std::getenv("JS8_NOISE_TRIALS") ? std::atoi(std::getenv("JS8_NOISE_TRIALS")) : 3000;
  for (int i = 0; i < noise_trials; ++i) {
    static float e[79][8];
    for (int s = 0; s < 79; ++s)
      for (int t = 0; t < 8; ++t) { const float a = gauss(), b = gauss(); e[s][t] = a * a + b * b; }
    decoder::Result rr;
    if (decoder::decode(e, decoder::Config{}, ws.get(), &rr)) {
      ++false_valid;
      if (rr.rendered) { ++false_messages; std::printf("  false: method=%d order=%u disc=%.3f tested=%u text=%s\n", (int)rr.method, rr.osd_order, (double)rr.osd_discrepancy, rr.osd_tested, rr.message.text); }
    }
  }
  std::printf("noise-only: %d valid frames, %d messages in %d trials\n", false_valid, false_messages, noise_trials);
  CHECK(false_messages == 0);

  // Garbage energies are handled without a result.
  static float bad_e[79][8];
  for (auto& row : bad_e) for (float& v : row) v = std::nanf("");
  decoder::Result rr;
  CHECK(!decoder::decode(bad_e, decoder::Config{}, ws.get(), &rr));

  if (failures == 0) std::printf("js8_decoder_tests OK\n");
  return failures == 0 ? 0 : 1;
}
