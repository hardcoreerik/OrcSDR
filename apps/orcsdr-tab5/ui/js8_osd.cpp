#include "js8_osd.hpp"

#include "js8_codec.hpp"
#include "js8_ldpc_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orcsdr::js8::osd {

static_assert(kInfo == codec::kParityBits && kInfo == codec::kInfoBits && kBits == codec::kCodewordBits && kWords * 64 >= kBits, "OSD word geometry must match the JS8 codeword layout");

namespace {

using Word = uint64_t;

inline bool bit_of(const uint64_t* row, size_t i) { return ((row[i >> 6] >> (i & 63u)) & 1u) != 0; }
inline void set_bit(uint64_t* row, size_t i) { row[i >> 6] |= (Word{1} << (i & 63u)); }
inline void xor_row(uint64_t* dst, const uint64_t* src) {
  dst[0] ^= src[0];
  dst[1] ^= src[1];
  dst[2] ^= src[2];
}

void to_bytes(const uint64_t* words, uint8_t* out) {
  for (size_t i = 0; i < kBits; ++i) out[i] = static_cast<uint8_t>((words[i >> 6] >> (i & 63u)) & 1u);
}

// CRC residue of a packed codeword: 0 when the CRC field matches the CRC of the first 75 information bits. Affine over GF(2).
uint16_t crc_residue(const uint64_t* words) {
  uint8_t bytes[kBits];
  to_bytes(words, bytes);
  const uint8_t* info = bytes + codec::kParityBits;
  uint16_t field = 0;
  for (size_t i = 0; i < 12; ++i) field = static_cast<uint16_t>((field << 1) | (info[75 + i] & 1u));
  return static_cast<uint16_t>(codec::crc12(info) ^ field);
}

struct Best {
  bool found = false;
  uint32_t accepted = 0;
  float discrepancy = 2.0f;
  uint8_t order = 0;
  std::array<uint8_t, kBits> bytes{};
};

}  // namespace

bool decode(const float* llr, const Config& config, Workspace* ws, AcceptFn accept, void* context, Result* result) {
  if (llr == nullptr || ws == nullptr || accept == nullptr || result == nullptr) return false;
  *result = Result{};

  // ---- generator in codeword order (parity first, information after): row j = information bit j plus the parity bits whose check contains it
  for (size_t j = 0; j < kInfo; ++j) {
    std::memset(ws->rows[j], 0, sizeof(ws->rows[j]));
    set_bit(ws->rows[j], kInfo + j);
  }
  for (size_t check = 0; check < ldpc_graph::kChecks; ++check)
    for (size_t e = ldpc_graph::kCheckOffsets[check]; e < ldpc_graph::kCheckOffsets[check + 1]; ++e) {
      const size_t v = ldpc_graph::kCheckVariables[e];
      if (v >= kInfo) set_bit(ws->rows[v - kInfo], check);   // parity bit `check` depends on information bit v - 87
    }

  // ---- reliability order, most reliable first
  float magnitude[kBits];
  for (size_t i = 0; i < kBits; ++i) {
    ws->order[i] = static_cast<uint16_t>(i);
    magnitude[i] = std::isfinite(llr[i]) ? std::fabs(llr[i]) : 0.0f;
  }
  std::sort(ws->order, ws->order + kBits, [&](uint16_t a, uint16_t b) { return magnitude[a] > magnitude[b] || (magnitude[a] == magnitude[b] && a < b); });

  // ---- reduce: the first 87 independent columns in reliability order become identity columns
  size_t rank = 0;
  for (size_t k = 0; k < kBits && rank < kInfo; ++k) {
    const size_t column = ws->order[k];
    size_t pivot_row = kInfo;
    for (size_t r = rank; r < kInfo; ++r)
      if (bit_of(ws->rows[r], column)) {
        pivot_row = r;
        break;
      }
    if (pivot_row == kInfo) continue;
    if (pivot_row != rank)
      for (size_t w = 0; w < kWords; ++w) std::swap(ws->rows[rank][w], ws->rows[pivot_row][w]);
    for (size_t r = 0; r < kInfo; ++r)
      if (r != rank && bit_of(ws->rows[r], column)) xor_row(ws->rows[r], ws->rows[rank]);
    ws->pivot[rank] = static_cast<uint16_t>(column);
    ++rank;
  }
  if (rank != kInfo) return false;

  // ---- order 0: re-encode from the hard decisions of the most reliable independent bits
  uint64_t base[kWords] = {0, 0, 0};
  for (size_t r = 0; r < kInfo; ++r)
    if (llr[ws->pivot[r]] < 0.0f) xor_row(base, ws->rows[r]);

  // CRC linearity: residue(a ^ b) = residue(a) ^ residue(b) ^ K, where K = residue(0). With l(x) = residue(x) ^ K the sum is linear, and a
  // candidate base ^ rows... has a valid CRC exactly when its l equals K.
  const uint64_t zero_word[kWords] = {0, 0, 0};
  const uint16_t k = crc_residue(zero_word);
  for (size_t r = 0; r < kInfo; ++r) ws->crc_delta[r] = static_cast<uint16_t>(crc_residue(ws->rows[r]) ^ k);
  const uint16_t l_base = static_cast<uint16_t>(crc_residue(base) ^ k);

  float total = 0.0f;
  for (size_t i = 0; i < kBits; ++i) total += magnitude[i];
  uint8_t hard[kBits];
  for (size_t i = 0; i < kBits; ++i) hard[i] = llr[i] < 0.0f ? 1u : 0u;

  Best best;
  uint8_t bytes[kBits];
  uint32_t tested = 0;
  const auto consider = [&](const uint64_t* words, uint8_t order) {
    to_bytes(words, bytes);
    if (!accept(bytes, context)) return;
    float disagree = 0.0f;
    for (size_t i = 0; i < kBits; ++i)
      if (bytes[i] != hard[i]) disagree += magnitude[i];
    const float fraction = total > 0.0f ? disagree / total : 1.0f;
    ++best.accepted;
    if (!best.found || fraction < best.discrepancy) {
      best.found = true;
      best.discrepancy = fraction;
      best.order = order;
      std::memcpy(best.bytes.data(), bytes, kBits);
    }
  };

  const uint8_t max_order = std::min<uint8_t>(config.max_order, 3);
  ++tested;
  if (l_base == k) consider(base, 0);
  if (!best.found && max_order >= 1) {
    for (size_t i = 0; i < kInfo; ++i) {
      ++tested;
      if (static_cast<uint16_t>(l_base ^ ws->crc_delta[i]) != k) continue;
      uint64_t w[kWords] = {base[0], base[1], base[2]};
      xor_row(w, ws->rows[i]);
      consider(w, 1);
    }
  }
  if (!best.found && max_order >= 2) {
    for (size_t i = 0; i < kInfo; ++i) {
      uint64_t wi[kWords] = {base[0], base[1], base[2]};
      xor_row(wi, ws->rows[i]);
      const uint16_t li = static_cast<uint16_t>(l_base ^ ws->crc_delta[i]);
      for (size_t j = i + 1; j < kInfo; ++j) {
        ++tested;
        if (static_cast<uint16_t>(li ^ ws->crc_delta[j]) != k) continue;
        uint64_t w[kWords] = {wi[0], wi[1], wi[2]};
        xor_row(w, ws->rows[j]);
        consider(w, 2);
      }
    }
  }
  if (!best.found && max_order >= 3) {
    for (size_t i = 0; i < kInfo; ++i) {
      uint64_t wi[kWords] = {base[0], base[1], base[2]};
      xor_row(wi, ws->rows[i]);
      for (size_t j = i + 1; j < kInfo; ++j) {
        uint64_t wj[kWords] = {wi[0], wi[1], wi[2]};
        xor_row(wj, ws->rows[j]);
        const uint16_t lj = static_cast<uint16_t>(l_base ^ ws->crc_delta[i] ^ ws->crc_delta[j]);
        for (size_t m = j + 1; m < kInfo; ++m) {
          ++tested;
          if (static_cast<uint16_t>(lj ^ ws->crc_delta[m]) != k) continue;
          uint64_t w[kWords] = {wj[0], wj[1], wj[2]};
          xor_row(w, ws->rows[m]);
          consider(w, 3);
        }
      }
    }
  }

  result->tested = tested;
  result->accepted = best.accepted;
  if (!best.found) return false;
  result->found = true;
  result->order = best.order;
  result->discrepancy = best.discrepancy;
  result->codeword = best.bytes;
  for (size_t i = 0; i < kBits; ++i) result->bit_corrections = static_cast<uint16_t>(result->bit_corrections + (best.bytes[i] != hard[i] ? 1u : 0u));
  return true;
}

bool self_check() { return kBits == 174 && kInfo == ldpc_graph::kInfoBits && kWords * 64 >= kBits; }

}  // namespace orcsdr::js8::osd
