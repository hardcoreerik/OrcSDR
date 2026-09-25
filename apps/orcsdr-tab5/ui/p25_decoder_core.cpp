#include "p25_decoder_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

// P25 physical-layer/FEC details were independently implemented from the
// published TIA-102 field layout and cross-checked against GopherTrunk
// (Apache-2.0, Matt Cheramie and contributors). No OP25/DSD GPL source is used.

namespace orcsdr::p25core {
namespace {

constexpr uint32_t kInputRate = 960000;
constexpr uint32_t kChannelRate = 48000;
constexpr uint32_t kSymbolRate = 4800;
constexpr uint32_t kPhase2SymbolRate = 6000;
constexpr size_t kInputDecimation = kInputRate / kChannelRate;
constexpr size_t kSamplesPerSymbol = kChannelRate / kSymbolRate;
constexpr size_t kPhase2SamplesPerSymbol = kChannelRate / kPhase2SymbolRate;
constexpr size_t kPhase2BurstDibits = 180;
constexpr float kOuterDeviationHz = 1800.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSlicerScale = 2.0f * kPi * kOuterDeviationHz / kChannelRate;

uint8_t hamming_distance_8(uint8_t value) {
  uint8_t count = 0;
  while (value != 0) {
    value &= static_cast<uint8_t>(value - 1u);
    ++count;
  }
  return count;
}

bool decode_phase2_duid(uint8_t codeword, uint8_t* duid, uint8_t* errors) {
  uint8_t best_duid = 0;
  uint8_t best_errors = 9;
  for (uint8_t candidate = 0; candidate < 16; ++candidate) {
    uint8_t parity = 0;
    if ((candidate & 1u) != 0) parity ^= 0x7u;
    if ((candidate & 2u) != 0) parity ^= 0xEu;
    if ((candidate & 4u) != 0) parity ^= 0xBu;
    if ((candidate & 8u) != 0) parity ^= 0xDu;
    const uint8_t encoded = static_cast<uint8_t>((candidate << 4) | parity);
    const uint8_t distance = hamming_distance_8(codeword ^ encoded);
    if (distance < best_errors) {
      best_errors = distance;
      best_duid = candidate;
    }
  }
  if (duid != nullptr) *duid = best_duid;
  if (errors != nullptr) *errors = best_errors;
  return best_errors <= 1;
}
constexpr float kSlicerThreshold = 2.0f * kSlicerScale / 3.0f;
constexpr float kAgcTarget = kSlicerThreshold;
constexpr size_t kCqpskRrcSpan = 8;
constexpr size_t kCqpskRrcTaps = 2 * kCqpskRrcSpan * kSamplesPerSymbol + 1;
constexpr float kCqpskRrcAlpha = 0.20f;
constexpr float kCqpskGardnerGain = 0.005f;
constexpr float kCqpskCostasAlpha = 0.008f;
constexpr uint32_t kLockTimeoutMs = 3000;
constexpr size_t kLduPayloadDibits = 784;
constexpr uint64_t kBchGenerator = 0xCD930BDD3B2Bull;
constexpr uint64_t kBchMask = (uint64_t{1} << 63) - 1;

constexpr std::array<uint8_t, 24> kFrameSync = {
    1, 1, 1, 1, 1, 3, 1, 1, 3, 3, 1, 1,
    3, 3, 3, 3, 1, 3, 1, 3, 3, 3, 3, 3};

constexpr std::array<uint8_t, 98> kDeinterleave = {
    0, 1, 26, 27, 50, 51, 74, 75, 2, 3, 28, 29, 52, 53, 76, 77,
    4, 5, 30, 31, 54, 55, 78, 79, 6, 7, 32, 33, 56, 57, 80, 81,
    8, 9, 34, 35, 58, 59, 82, 83, 10, 11, 36, 37, 60, 61, 84, 85,
    12, 13, 38, 39, 62, 63, 86, 87, 14, 15, 40, 41, 64, 65, 88, 89,
    16, 17, 42, 43, 66, 67, 90, 91, 18, 19, 44, 45, 68, 69, 92, 93,
    20, 21, 46, 47, 70, 71, 94, 95, 22, 23, 48, 49, 72, 73, 96, 97,
    24, 25};

constexpr std::array<uint8_t, 98> kInterleave = {
    0, 1, 8, 9, 16, 17, 24, 25, 32, 33, 40, 41, 48, 49, 56, 57,
    64, 65, 72, 73, 80, 81, 88, 89, 96, 97, 2, 3, 10, 11, 18, 19,
    26, 27, 34, 35, 42, 43, 50, 51, 58, 59, 66, 67, 74, 75, 82, 83,
    90, 91, 4, 5, 12, 13, 20, 21, 28, 29, 36, 37, 44, 45, 52, 53,
    60, 61, 68, 69, 76, 77, 84, 85, 92, 93, 6, 7, 14, 15, 22, 23,
    30, 31, 38, 39, 46, 47, 54, 55, 62, 63, 70, 71, 78, 79, 86, 87,
    94, 95};

constexpr uint8_t kTrellisStates[4][4] = {
    {0, 15, 12, 3}, {4, 11, 8, 7}, {13, 2, 1, 14}, {9, 6, 5, 10}};
constexpr uint8_t kTrellisPairs[16][2] = {
    {0, 2}, {2, 2}, {1, 3}, {3, 3}, {3, 2}, {1, 2}, {2, 3}, {0, 3},
    {3, 1}, {1, 1}, {2, 0}, {0, 0}, {0, 1}, {2, 1}, {1, 0}, {3, 0}};

struct BandPlanSlot {
  bool known = false;
  uint8_t slots_per_carrier = 1;
  uint32_t spacing_hz = 0;
  uint64_t base_hz = 0;
};

uint8_t slots_for_channel_type(uint8_t channel_type) {
  constexpr std::array<uint8_t, 6> kSlots = {1, 1, 1, 2, 4, 2};
  return channel_type < kSlots.size() ? kSlots[channel_type] : 0;
}

float wrap_pi(float angle);

uint8_t bit_errors_40(uint64_t value) {
  value &= 0xFFFFFFFFFFULL;
  uint8_t count = 0;
  while (value != 0) {
    value &= value - 1;
    ++count;
  }
  return count;
}

struct Phase2State {
  uint32_t symbols = 0;
  uint32_t sync_words = 0;
  uint32_t last_sync_ms = 0;
  uint8_t best_sync_errors = 40;
  uint32_t complete_bursts = 0;
  uint32_t truncated_bursts = 0;
  uint32_t last_burst_ms = 0;
  uint8_t last_duid_codeword = 0;
  uint8_t last_duid = 0;
  uint8_t last_duid_errors = 0;
  bool last_duid_valid = false;
  uint32_t voice_bursts = 0;
  uint32_t control_bursts = 0;
  uint32_t unknown_bursts = 0;
  bool reverse_polarity = false;
};

class Phase2Acquirer {
 public:
  void reset(uint32_t now_ms) {
    state_ = {};
    state_.best_sync_errors = 40;
    now_ms_ = now_ms;
    sample_index_ = 0;
    have_previous_.fill(false);
    primed_.fill(0);
    history_.fill(0);
    collecting_ = false;
    burst_size_ = 0;
  }

  void finish() {
    if (collecting_) ++state_.truncated_bursts;
    collecting_ = false;
    burst_size_ = 0;
  }

  void process(float i, float q, uint32_t now_ms) {
    now_ms_ = now_ms;
    const size_t phase_index = sample_index_++ % kPhase2SamplesPerSymbol;
    if (have_previous_[phase_index]) {
      const float cross = previous_i_[phase_index] * q - previous_q_[phase_index] * i;
      const float dot = previous_i_[phase_index] * i + previous_q_[phase_index] * q;
      const float phase = wrap_pi(atan2f(cross, dot) - kPi / 4.0f);
      const uint8_t quadrant = phase >= -kPi / 4.0f && phase < kPi / 4.0f ? 0
          : phase >= kPi / 4.0f && phase < 3.0f * kPi / 4.0f ? 1
          : phase >= -3.0f * kPi / 4.0f && phase < -kPi / 4.0f ? 3 : 2;
      constexpr uint8_t kDibit[4] = {0, 1, 3, 2};
      accept_dibit(phase_index, kDibit[quadrant], now_ms);
    }
    previous_i_[phase_index] = i;
    previous_q_[phase_index] = q;
    have_previous_[phase_index] = true;
  }

  void process_dibit(uint8_t dibit, uint32_t now_ms) {
    accept_dibit(0, dibit & 3u, now_ms);
  }

  const Phase2State& state() const { return state_; }

 private:
  void accept_dibit(size_t phase_index, uint8_t dibit, uint32_t now_ms) {
    if (phase_index == 0) ++state_.symbols;
    if (collecting_) {
      if (phase_index != active_phase_) return;
      burst_[burst_size_++] = state_.reverse_polarity ? dibit ^ 2u : dibit;
      if (burst_size_ == burst_.size()) {
        state_.last_duid_codeword = static_cast<uint8_t>(
            (burst_[20] << 6) | (burst_[57] << 4) |
            (burst_[142] << 2) | burst_[179]);
        state_.last_duid_valid = decode_phase2_duid(
            state_.last_duid_codeword, &state_.last_duid,
            &state_.last_duid_errors);
        if (!state_.last_duid_valid) {
          ++state_.unknown_bursts;
        } else if (state_.last_duid == 0 || state_.last_duid == 6) {
          ++state_.voice_bursts;
        } else if (state_.last_duid == 3 || state_.last_duid == 4 ||
                   state_.last_duid == 9 || state_.last_duid == 12 ||
                   state_.last_duid == 13 || state_.last_duid == 15) {
          ++state_.control_bursts;
        } else {
          ++state_.unknown_bursts;
        }
        ++state_.complete_bursts;
        state_.last_burst_ms = now_ms;
        collecting_ = false;
        burst_size_ = 0;
      }
      return;
    }

    history_[phase_index] = ((history_[phase_index] << 2) | dibit) & 0xFFFFFFFFFFULL;
    if (primed_[phase_index] < 20) ++primed_[phase_index];
    if (primed_[phase_index] < 20) return;
    constexpr uint64_t kSync = 0x575D57F7FFULL;
    constexpr uint64_t kReverse = kSync ^ 0xAAAAAAAAAAULL;
    const uint8_t normal_errors = bit_errors_40(history_[phase_index] ^ kSync);
    const uint8_t reverse_errors = bit_errors_40(history_[phase_index] ^ kReverse);
    const uint8_t errors = std::min(normal_errors, reverse_errors);
    state_.best_sync_errors = std::min(state_.best_sync_errors, errors);
    if (errors > 4) return;

    ++state_.sync_words;
    state_.last_sync_ms = now_ms;
    state_.reverse_polarity = reverse_errors < normal_errors;
    active_phase_ = phase_index;
    collecting_ = true;
    burst_size_ = 20;
    uint64_t normalized = state_.reverse_polarity ? history_[phase_index] ^
                                                       0xAAAAAAAAAAULL
                                                  : history_[phase_index];
    for (size_t index = 0; index < 20; ++index)
      burst_[19 - index] = static_cast<uint8_t>((normalized >> (index * 2)) & 3u);
  }

  Phase2State state_{};
  uint32_t now_ms_ = 0;
  uint32_t sample_index_ = 0;
  std::array<float, kPhase2SamplesPerSymbol> previous_i_{};
  std::array<float, kPhase2SamplesPerSymbol> previous_q_{};
  std::array<bool, kPhase2SamplesPerSymbol> have_previous_{};
  std::array<uint8_t, kPhase2SamplesPerSymbol> primed_{};
  std::array<uint64_t, kPhase2SamplesPerSymbol> history_{};
  std::array<uint8_t, kPhase2BurstDibits> burst_{};
  size_t active_phase_ = 0;
  size_t burst_size_ = 0;
  bool collecting_ = false;
};

constexpr std::array<size_t, 9> kVoiceBitOffsets = {
    0, 144, 328, 512, 696, 880, 1064, 1248, 1424};
constexpr std::array<size_t, 6> kEncryptionBitOffsets = {
    288, 472, 656, 840, 1024, 1208};

std::array<uint8_t, 126> g_gf_exp{};
std::array<int8_t, 64> g_gf_log{};
bool g_gf_ready = false;
std::array<float, kCqpskRrcTaps> g_cqpsk_rrc{};
bool g_cqpsk_rrc_ready = false;

float wrap_pi(float angle) {
  while (angle > kPi) angle -= 2.0f * kPi;
  while (angle <= -kPi) angle += 2.0f * kPi;
  return angle;
}

void ensure_cqpsk_rrc() {
  if (g_cqpsk_rrc_ready) return;
  float energy = 0.0f;
  constexpr int center = static_cast<int>(kCqpskRrcTaps / 2);
  for (int index = 0; index < static_cast<int>(kCqpskRrcTaps); ++index) {
    const float t = static_cast<float>(index - center) / kSamplesPerSymbol;
    float value;
    if (fabsf(t) < 1.0e-6f) {
      value = 1.0f + kCqpskRrcAlpha * (4.0f / kPi - 1.0f);
    } else if (fabsf(fabsf(4.0f * kCqpskRrcAlpha * t) - 1.0f) < 1.0e-5f) {
      const float angle = kPi / (4.0f * kCqpskRrcAlpha);
      value = kCqpskRrcAlpha / sqrtf(2.0f) *
              ((1.0f + 2.0f / kPi) * sinf(angle) +
               (1.0f - 2.0f / kPi) * cosf(angle));
    } else {
      value = (sinf(kPi * t * (1.0f - kCqpskRrcAlpha)) +
               4.0f * kCqpskRrcAlpha * t *
                   cosf(kPi * t * (1.0f + kCqpskRrcAlpha))) /
              (kPi * t *
               (1.0f - 16.0f * kCqpskRrcAlpha * kCqpskRrcAlpha * t * t));
    }
    g_cqpsk_rrc[index] = value;
    energy += value * value;
  }
  const float scale = energy > 0.0f ? 1.0f / sqrtf(energy) : 1.0f;
  for (float& tap : g_cqpsk_rrc) tap *= scale;
  g_cqpsk_rrc_ready = true;
}

void ensure_gf() {
  if (g_gf_ready) return;
  g_gf_log.fill(-1);
  uint8_t x = 1;
  for (int i = 0; i < 63; ++i) {
    g_gf_exp[i] = x;
    g_gf_log[x] = static_cast<int8_t>(i);
    uint16_t next = static_cast<uint16_t>(x) << 1;
    if (next & 0x40) next ^= 0x43;  // x^6 + x + 1
    x = static_cast<uint8_t>(next);
  }
  for (int i = 63; i < 126; ++i) g_gf_exp[i] = g_gf_exp[i - 63];
  g_gf_ready = true;
}

uint8_t gf_mul(uint8_t a, uint8_t b) {
  if (a == 0 || b == 0) return 0;
  return g_gf_exp[g_gf_log[a] + g_gf_log[b]];
}

uint8_t gf_pow(int exponent) {
  exponent %= 63;
  if (exponent < 0) exponent += 63;
  return g_gf_exp[exponent];
}

uint8_t gf_inv(uint8_t value) {
  return value == 0 ? 0 : g_gf_exp[63 - g_gf_log[value]];
}

uint64_t bch_encode(uint16_t data) {
  uint64_t remainder = static_cast<uint64_t>(data) << 47;
  for (int bit = 62; bit >= 47; --bit) {
    if (remainder & (uint64_t{1} << bit)) remainder ^= kBchGenerator << (bit - 47);
  }
  return (static_cast<uint64_t>(data) << 47) |
         (remainder & ((uint64_t{1} << 47) - 1));
}

int bch_decode(uint64_t received, uint16_t& data) {
  ensure_gf();
  received &= kBchMask;
  data = static_cast<uint16_t>(received >> 47);
  if (bch_encode(data) == received) return 0;

  std::array<uint8_t, 23> syndrome{};
  bool all_zero = true;
  for (int j = 1; j <= 22; ++j) {
    const uint8_t alpha = gf_pow(j);
    uint8_t value = 0;
    for (int bit = 62; bit >= 0; --bit) {
      value = gf_mul(value, alpha);
      if (received & (uint64_t{1} << bit)) value ^= 1;
    }
    syndrome[j] = value;
    all_zero &= value == 0;
  }
  if (all_zero) return 0;

  std::array<uint8_t, 46> current{};
  std::array<uint8_t, 46> previous{};
  current[0] = previous[0] = 1;
  uint8_t last_discrepancy = 1;
  int locator_degree = 0;
  int shift = 1;
  for (int n = 0; n < 22; ++n) {
    uint8_t discrepancy = syndrome[n + 1];
    for (int i = 1; i <= locator_degree; ++i)
      discrepancy ^= gf_mul(current[i], syndrome[n + 1 - i]);
    if (discrepancy == 0) {
      ++shift;
      continue;
    }
    const uint8_t coefficient = gf_mul(discrepancy, gf_inv(last_discrepancy));
    if (2 * locator_degree <= n) {
      const auto saved = current;
      for (int i = 0; i + shift < static_cast<int>(current.size()); ++i)
        if (previous[i]) current[i + shift] ^= gf_mul(coefficient, previous[i]);
      locator_degree = n + 1 - locator_degree;
      previous = saved;
      last_discrepancy = discrepancy;
      shift = 1;
    } else {
      for (int i = 0; i + shift < static_cast<int>(current.size()); ++i)
        if (previous[i]) current[i + shift] ^= gf_mul(coefficient, previous[i]);
      ++shift;
    }
  }
  if (locator_degree < 1 || locator_degree > 11) return -1;

  std::array<uint8_t, 11> positions{};
  int roots = 0;
  for (int bit = 0; bit < 63; ++bit) {
    uint8_t sum = 0;
    for (int i = 0; i <= locator_degree; ++i)
      if (current[i]) sum ^= gf_mul(current[i], gf_pow(-bit * i));
    if (sum == 0 && roots < static_cast<int>(positions.size()))
      positions[roots++] = static_cast<uint8_t>(bit);
  }
  if (roots != locator_degree) return -1;
  for (int i = 0; i < roots; ++i) received ^= uint64_t{1} << positions[i];
  data = static_cast<uint16_t>(received >> 47);
  return bch_encode(data) == received ? roots : -1;
}

uint16_t crc_augmented(const uint8_t* data, size_t size) {
  uint32_t crc = 0;
  for (size_t i = 0; i < size; ++i) {
    for (int bit = 7; bit >= 0; --bit) {
      crc = ((crc << 1) | ((data[i] >> bit) & 1u)) & 0x1FFFFu;
      if (crc & 0x10000u) crc = (crc & 0xFFFFu) ^ 0x1021u;
    }
  }
  return static_cast<uint16_t>((crc ^ 0xFFFFu) & 0xFFFFu);
}

int dibit_distance(uint8_t a, uint8_t b) {
  const uint8_t difference = (a ^ b) & 3;
  return difference == 0 ? 0 : difference == 3 ? 2 : 1;
}

int trellis_decode(const std::array<uint8_t, 98>& channel,
                   std::array<uint8_t, 48>& output) {
  constexpr int kInfinity = 1 << 28;
  std::array<int, 4> metric = {0, kInfinity, kInfinity, kInfinity};
  std::array<std::array<uint8_t, 4>, 49> trace{};
  for (int stage = 0; stage < 49; ++stage) {
    std::array<int, 4> next_metric = {kInfinity, kInfinity, kInfinity, kInfinity};
    const uint8_t high = channel[2 * stage];
    const uint8_t low = channel[2 * stage + 1];
    for (int current = 0; current < 4; ++current) {
      if (metric[current] >= kInfinity) continue;
      for (int next = 0; next < 4; ++next) {
        const uint8_t pair = kTrellisStates[current][next];
        const int cost = metric[current] + dibit_distance(kTrellisPairs[pair][0], high) +
                         dibit_distance(kTrellisPairs[pair][1], low);
        if (cost < next_metric[next]) {
          next_metric[next] = cost;
          trace[stage][next] = static_cast<uint8_t>(current);
        }
      }
    }
    metric = next_metric;
  }
  int state = static_cast<int>(std::min_element(metric.begin(), metric.end()) - metric.begin());
  const int final_metric = metric[state];
  std::array<uint8_t, 49> decoded{};
  for (int stage = 48; stage >= 0; --stage) {
    decoded[stage] = static_cast<uint8_t>(state);
    state = trace[stage][state];
  }
  std::copy_n(decoded.begin(), output.size(), output.begin());
  return final_metric;
}

std::array<uint8_t, 98> trellis_encode(const std::array<uint8_t, 48>& input) {
  std::array<uint8_t, 98> coding{};
  int state = 0;
  for (size_t i = 0; i < input.size(); ++i) {
    const int next = input[i] & 3;
    const uint8_t pair = kTrellisStates[state][next];
    coding[2 * i] = kTrellisPairs[pair][0];
    coding[2 * i + 1] = kTrellisPairs[pair][1];
    state = next;
  }
  const uint8_t pair = kTrellisStates[state][0];
  coding[96] = kTrellisPairs[pair][0];
  coding[97] = kTrellisPairs[pair][1];
  std::array<uint8_t, 98> channel{};
  for (size_t i = 0; i < channel.size(); ++i) channel[i] = coding[kInterleave[i]];
  return channel;
}

int bit_count(uint16_t value) {
  int count = 0;
  while (value != 0) {
    value &= static_cast<uint16_t>(value - 1);
    ++count;
  }
  return count;
}

uint16_t hamming_10_6_encode(uint8_t data) {
  constexpr uint16_t kGenerator[6] = {
      0x20E, 0x10D, 0x08B, 0x047, 0x023, 0x01C};
  uint16_t codeword = 0;
  for (int bit = 0; bit < 6; ++bit)
    if (data & (0x20u >> bit)) codeword ^= kGenerator[bit];
  return codeword;
}

int hamming_10_6_decode(uint16_t received, uint8_t* data) {
  int best_distance = 11;
  uint8_t best_data = 0;
  for (uint8_t candidate = 0; candidate < 64; ++candidate) {
    const int distance = bit_count(received ^ hamming_10_6_encode(candidate));
    if (distance < best_distance) {
      best_distance = distance;
      best_data = candidate;
    }
  }
  // A double-bit error is outside the inner code's correction radius. Keep
  // its systematic data bits and let the outer RS code correct the symbol.
  *data = best_distance <= 1 ? best_data : static_cast<uint8_t>(received >> 4);
  return best_distance <= 1 ? best_distance : 0;
}

void rs_syndromes(const std::array<uint8_t, 24>& codeword,
                  std::array<uint8_t, 8>* syndromes) {
  ensure_gf();
  for (int root = 1; root <= 8; ++root) {
    const uint8_t alpha = gf_pow(root);
    uint8_t value = 0;
    for (const uint8_t symbol : codeword) value = gf_mul(value, alpha) ^ symbol;
    (*syndromes)[root - 1] = value;
  }
}

uint8_t polynomial_value(const std::array<uint8_t, 9>& polynomial,
                         uint8_t value) {
  uint8_t result = 0;
  uint8_t power = 1;
  for (const uint8_t coefficient : polynomial) {
    result ^= gf_mul(coefficient, power);
    power = gf_mul(power, value);
  }
  return result;
}

int rs_24_16_decode(std::array<uint8_t, 24>* codeword) {
  std::array<uint8_t, 8> syndromes{};
  rs_syndromes(*codeword, &syndromes);
  if (std::all_of(syndromes.begin(), syndromes.end(),
                  [](uint8_t value) { return value == 0; })) return 0;

  std::array<uint8_t, 9> locator{};
  std::array<uint8_t, 9> previous{};
  locator[0] = previous[0] = 1;
  int degree = 0;
  int shift = 1;
  uint8_t last_discrepancy = 1;
  for (int index = 0; index < 8; ++index) {
    uint8_t discrepancy = syndromes[index];
    for (int term = 1; term <= degree; ++term)
      discrepancy ^= gf_mul(locator[term], syndromes[index - term]);
    if (discrepancy == 0) {
      ++shift;
      continue;
    }
    const auto saved = locator;
    const uint8_t scale = gf_mul(discrepancy, gf_inv(last_discrepancy));
    for (int term = 0; term + shift < static_cast<int>(locator.size()); ++term)
      locator[term + shift] ^= gf_mul(scale, previous[term]);
    if (2 * degree <= index) {
      degree = index + 1 - degree;
      previous = saved;
      last_discrepancy = discrepancy;
      shift = 1;
    } else {
      ++shift;
    }
  }
  if (degree < 1 || degree > 4) return -1;

  struct ErrorPosition { int index; uint8_t location; };
  std::array<ErrorPosition, 4> positions{};
  int position_count = 0;
  for (int power = 0; power < 24; ++power) {
    if (polynomial_value(locator, gf_pow(-power)) == 0) {
      if (position_count >= degree) return -1;
      positions[position_count++] = {23 - power, gf_pow(power)};
    }
  }
  if (position_count != degree) return -1;

  std::array<uint8_t, 9> evaluator{};
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j <= degree && i + j < 8; ++j)
      evaluator[i + j] ^= gf_mul(syndromes[i], locator[j]);
  std::array<uint8_t, 9> derivative{};
  for (int term = 1; term <= degree; term += 2) derivative[term - 1] = locator[term];

  for (int i = 0; i < position_count; ++i) {
    const uint8_t inverse = gf_inv(positions[i].location);
    const uint8_t denominator = polynomial_value(derivative, inverse);
    if (denominator == 0) return -1;
    const uint8_t magnitude = gf_mul(polynomial_value(evaluator, inverse),
                                     gf_inv(denominator));
    (*codeword)[positions[i].index] ^= magnitude;
  }
  rs_syndromes(*codeword, &syndromes);
  if (!std::all_of(syndromes.begin(), syndromes.end(),
                   [](uint8_t value) { return value == 0; })) return -1;
  return position_count;
}

bool decode_encryption_payload(const uint8_t* payload, size_t dibits,
                               EncryptionSync* result) {
  if (payload == nullptr || result == nullptr || dibits != kLduPayloadDibits) return false;
  *result = {};
  std::array<uint8_t, 24> codeword{};
  int inner_corrections = 0;
  size_t word = 0;
  for (const size_t block_offset : kEncryptionBitOffsets) {
    for (size_t block_word = 0; block_word < 4; ++block_word) {
      uint16_t received = 0;
      for (size_t bit = 0; bit < 10; ++bit) {
        const size_t source = block_offset + block_word * 10 + bit;
        received = static_cast<uint16_t>((received << 1) |
            ((payload[source / 2] >> (1 - source % 2)) & 1u));
      }
      inner_corrections += hamming_10_6_decode(received, &codeword[word++]);
    }
  }
  const int outer_corrections = rs_24_16_decode(&codeword);
  if (outer_corrections < 0) return false;

  std::array<uint8_t, 12> bytes{};
  size_t output_bit = 0;
  for (size_t symbol = 0; symbol < 16; ++symbol)
    for (int bit = 5; bit >= 0; --bit, ++output_bit)
      bytes[output_bit / 8] |=
          static_cast<uint8_t>(((codeword[symbol] >> bit) & 1u) << (7 - output_bit % 8));
  result->valid = true;
  result->algorithm_id = bytes[9];
  result->key_id = static_cast<uint16_t>(bytes[10]) << 8 | bytes[11];
  result->encrypted = result->algorithm_id != kClearAlgorithmId;
  result->corrected_errors = static_cast<uint8_t>(
      std::min(255, inner_corrections + outer_corrections));
  return true;
}

}  // namespace

class Decoder {
 public:
  void reset(uint32_t now_ms) {
    *this = Decoder{};
    now_ms_ = now_ms;
    started_ms_ = now_ms;
  }

  void prepare(uint32_t now_ms, VoiceSink voice_sink, void* voice_context) {
    now_ms_ = now_ms;
    voice_sink_ = voice_sink;
    voice_context_ = voice_context;
  }

  void finish(uint32_t now_ms) {
    now_ms_ = now_ms;
    refresh_health(now_ms_);
  }

  Snapshot state() const { return state_; }

  void set_sync_tolerance(int tolerance) { sync_tolerance_ = tolerance; }

  void set_cqpsk_gains(float timing_gain, float carrier_gain) {
    cqpsk_timing_gain_ = timing_gain;
    cqpsk_carrier_gain_ = carrier_gain;
  }

  static bool self_check() {
    // Self-check runs on the embedded startup task. Keep its large decoder
    // scratch objects out of that task's stack: heap, freed when the check
    // ends, rather than static storage that would pin ~14 KB of RAM forever.
    const auto decoder_storage = std::make_unique<Decoder>();
    Decoder& decoder = *decoder_storage;
    decoder.now_ms_ = 1000;
    constexpr uint16_t kNac = 0x1F0;
    const uint16_t nid_info = static_cast<uint16_t>((kNac << 4) | 0x7);
    const uint64_t codeword = bch_encode(nid_info);
    std::array<uint8_t, 64> nid_bits{};
    for (int i = 0; i < 63; ++i)
      nid_bits[i] = (codeword >> (62 - i)) & 1u;
    nid_bits[63] = 0;  // TSDU fixed trailing flag.
    for (int bit : {2, 9, 21, 37, 54}) nid_bits[bit] ^= 1;

    std::array<uint8_t, 12> tsbk{};
    tsbk[0] = 0x80 | 0x3B;  // last block, network status broadcast
    tsbk[1] = 0x00;
    tsbk[2] = 0x01;  // LRA
    tsbk[3] = 0xBE;
    tsbk[4] = 0xE0;
    tsbk[5] = 0x01;  // WACN low nibble + SYSID high nibble
    tsbk[6] = 0xF3;
    tsbk[7] = 0x10;
    tsbk[8] = 0x17;
    tsbk[9] = 0x01;
    const uint16_t crc = crc_augmented(tsbk.data(), tsbk.size());
    tsbk[10] = static_cast<uint8_t>(crc >> 8);
    tsbk[11] = static_cast<uint8_t>(crc);
    if (crc_augmented(tsbk.data(), tsbk.size()) != 0) return false;

    std::array<uint8_t, 48> info_dibits{};
    for (size_t i = 0; i < tsbk.size(); ++i) {
      info_dibits[4 * i] = (tsbk[i] >> 6) & 3;
      info_dibits[4 * i + 1] = (tsbk[i] >> 4) & 3;
      info_dibits[4 * i + 2] = (tsbk[i] >> 2) & 3;
      info_dibits[4 * i + 3] = tsbk[i] & 3;
    }
    auto channel = trellis_encode(info_dibits);
    channel[11] ^= 1;
    channel[63] ^= 2;

    std::array<uint8_t, 24 + 32 + 98> data{};
    std::copy(kFrameSync.begin(), kFrameSync.end(), data.begin());
    for (size_t i = 0; i < 32; ++i)
      data[24 + i] = static_cast<uint8_t>((nid_bits[2 * i] << 1) | nid_bits[2 * i + 1]);
    std::copy(channel.begin(), channel.end(), data.begin() + 56);
    size_t data_index = 0;
    while (data_index < data.size()) {
      decoder.feed_dibit(data[data_index++]);
      if (data_index < data.size() && data_index % 35 == 0) {
        decoder.feed_dibit(static_cast<uint8_t>((data_index / 35) & 3));
      }
    }
    const Snapshot result = decoder.state_;
    const bool control_ok = result.nac == kNac && result.nid_good == 1 &&
                            result.nid_corrected_bits == 5 && result.tsbk_good == 1 &&
                            result.wacn == 0xBEE00 && result.system_id == 0x1F3 &&
                            result.last_trellis_metric > 0;
    decoder.band_plan_[1] = {true, 1, 12500, 450000000};
    std::array<uint8_t, 12> explicit_grant{};
    explicit_grant[0] = 0x80 | 0x03;
    explicit_grant[2] = 0x80;
    explicit_grant[4] = 0x10;
    explicit_grant[5] = 0x07;
    explicit_grant[6] = 0x20;
    explicit_grant[7] = 0x08;
    explicit_grant[8] = 0x12;
    explicit_grant[9] = 0x34;
    decoder.dispatch_tsbk(explicit_grant);
    const Grant& grant = decoder.state_.current_grant;
    const bool explicit_grant_ok = grant.valid && grant.emergency &&
                                   grant.frequency_hz == 450087500 &&
                                   grant.talkgroup == 0x1234 && grant.source_id == 0;
    Decoder tdma_decoder;
    tdma_decoder.reset(100);
    std::array<uint8_t, 12> tdma_plan{};
    tdma_plan[0] = 0x33;
    tdma_plan[2] = 0x23;  // identifier 2, two-slot channel type 3
    tdma_plan[4] = 0x00;
    tdma_plan[5] = 100;   // 12.5 kHz spacing in 125 Hz units
    constexpr uint32_t base5 = 769000000 / 5;
    tdma_plan[6] = static_cast<uint8_t>(base5 >> 24);
    tdma_plan[7] = static_cast<uint8_t>(base5 >> 16);
    tdma_plan[8] = static_cast<uint8_t>(base5 >> 8);
    tdma_plan[9] = static_cast<uint8_t>(base5);
    tdma_decoder.dispatch_tsbk(tdma_plan);
    std::array<uint8_t, 12> tdma_grant{};
    tdma_grant[0] = 0x03;
    tdma_grant[2] = 0x04;
    tdma_grant[4] = 0x20;
    tdma_grant[5] = 41;
    tdma_grant[8] = 0x56;
    tdma_grant[9] = 0x78;
    tdma_decoder.dispatch_tsbk(tdma_grant);
    const Grant& tdma = tdma_decoder.state_.current_grant;
    const bool tdma_grant_ok = tdma.valid && tdma.tdma && tdma.slot == 1 &&
                               tdma.frequency_hz == 769250000 &&
                               tdma.channel_id == 2 && tdma.channel_number == 41 &&
                               tdma.talkgroup == 0x5678 &&
                               tdma_decoder.state_.phase2_band_plans == 1 &&
                               tdma_decoder.state_.phase2_grants == 1;
    struct VoiceCheck {
      VoiceFrame frames[9]{};
      size_t count = 0;
    };
    const auto voice_check_storage = std::make_unique<VoiceCheck>();
    VoiceCheck& voice_check = *voice_check_storage;
    const auto collect_voice = [](const VoiceFrame& frame, void* context) {
      auto& check = *static_cast<VoiceCheck*>(context);
      if (check.count >= std::size(check.frames)) return false;
      check.frames[check.count++] = frame;
      return true;
    };
    const auto voice_decoder_storage = std::make_unique<Decoder>();
    Decoder& voice_decoder = *voice_decoder_storage;
    voice_decoder.reset(0);
    voice_decoder.now_ms_ = 2000;
    voice_decoder.voice_sink_ = collect_voice;
    voice_decoder.voice_context_ = &voice_check;
    for (size_t source = 0; source < kLduPayloadDibits * 2; source += 2) {
      const auto pattern = [](size_t bit) { return static_cast<uint8_t>(((bit * 13) ^ 5) & 1); };
      voice_decoder.ldu_payload_[source / 2] =
          static_cast<uint8_t>((pattern(source) << 1) | pattern(source + 1));
    }
    voice_decoder.decode_ldu();
    bool voice_ok = voice_decoder.state_.voice_frames == 9;
    for (size_t frame_index = 0; frame_index < kVoiceBitOffsets.size(); ++frame_index) {
      const VoiceFrame& frame = voice_check.frames[frame_index];
      voice_ok &= voice_check.count == 9 && frame.sequence == frame_index + 1;
      for (size_t bit = 0; voice_ok && bit < kVoiceFrameBits; ++bit) {
        const size_t source = kVoiceBitOffsets[frame_index] + bit;
        voice_ok &= frame.bits[bit] == static_cast<uint8_t>(((source * 13) ^ 5) & 1);
      }
    }
    voice_decoder.voice_sink_ = nullptr;
    voice_decoder.decode_ldu();
    voice_ok &= voice_decoder.state_.voice_queue_drops == 0 &&
                voice_decoder.state_.voice_unrouted_frames == 9;
    constexpr std::array<uint8_t, 24> kEncryptedCodeword = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x21,0x01,0x08,0x34,0x21,0x37,0x13,0x34,0x0D,0x1F,0x24,0x10};
    static std::array<uint8_t, kLduPayloadDibits> encryption_payload;
    encryption_payload.fill(0);
    size_t encryption_word = 0;
    for (const size_t block_offset : kEncryptionBitOffsets) {
      for (size_t block_word = 0; block_word < 4; ++block_word) {
        const uint16_t encoded = hamming_10_6_encode(kEncryptedCodeword[encryption_word++]);
        for (size_t bit = 0; bit < 10; ++bit) {
          const size_t destination = block_offset + block_word * 10 + bit;
          encryption_payload[destination / 2] |= static_cast<uint8_t>(
              ((encoded >> (9 - bit)) & 1u) << (1 - destination % 2));
        }
      }
    }
    EncryptionSync encryption{};
    const bool encryption_ok = decode_encryption_payload(
        encryption_payload.data(), encryption_payload.size(), &encryption) &&
        encryption.valid && encryption.encrypted && encryption.algorithm_id == 0x84 &&
        encryption.key_id == 0x1234;

    const auto check_cqpsk = [&](float echo_gain) {
      // The embedded startup task has a small stack; keep this large scratch
      // decoder on the heap and reset it for each deterministic check.
      const auto cqpsk_storage = std::make_unique<Decoder>();
      Decoder& cqpsk = *cqpsk_storage;
      cqpsk.reset(0);
      cqpsk.set_sync_tolerance(5);
      float phase = 0.0f;
      std::array<float, 4> echo_i{};
      std::array<float, 4> echo_q{};
      size_t echo_pos = 0;
      for (int repeat = 0; repeat < 8; ++repeat) {
        size_t data_index = 0;
        while (data_index < data.size()) {
          const auto emit = [&](uint8_t dibit) {
            constexpr uint8_t kInverseRemap[4] = {0, 1, 3, 2};
            const uint8_t quadrant = kInverseRemap[dibit & 3];
            const float delta = kPi / 4.0f +
                (quadrant == 3 ? -kPi / 2.0f : quadrant * kPi / 2.0f);
            phase = wrap_pi(phase + delta);
            const float clean_i = cosf(phase);
            const float clean_q = sinf(phase);
            for (size_t sample = 0; sample < kSamplesPerSymbol; ++sample) {
              const size_t delayed = (echo_pos + 1) % echo_i.size();
              cqpsk.process_cqpsk_sample(clean_i + echo_gain * echo_i[delayed],
                                         clean_q + echo_gain * echo_q[delayed]);
              echo_i[echo_pos] = clean_i;
              echo_q[echo_pos] = clean_q;
              echo_pos = (echo_pos + 1) % echo_i.size();
            }
          };
          emit(data[data_index++]);
          if (data_index < data.size() && data_index % 35 == 0)
            emit(static_cast<uint8_t>(repeat & 3));
        }
      }
      cqpsk.finish(1000);
      return cqpsk.state_.nid_good > 0 && cqpsk.state_.tsbk_good > 0;
    };
    return control_ok && explicit_grant_ok && tdma_grant_ok && voice_ok && encryption_ok &&
           check_cqpsk(0.0f) && check_cqpsk(0.20f);
  }

  void process_c4fm_sample(float i, float q) {
    if (!have_previous_iq_) {
      previous_i_ = i;
      previous_q_ = q;
      have_previous_iq_ = true;
      return;
    }
    const float cross = previous_i_ * q - previous_q_ * i;
    const float dot = previous_i_ * i + previous_q_ * q;
    previous_i_ = i;
    previous_q_ = q;
    const float discriminator = atan2f(cross, dot);

    matched_sum_ -= matched_history_[matched_pos_];
    matched_history_[matched_pos_] = discriminator;
    matched_sum_ += discriminator;
    matched_pos_ = (matched_pos_ + 1) % matched_history_.size();
    afc_dc_ += (matched_sum_ - afc_dc_) / (64.0f * kSamplesPerSymbol);
    const float matched = matched_sum_ - afc_dc_;

    if (!have_previous_matched_) {
      previous_matched_ = matched;
      have_previous_matched_ = true;
      return;
    }
    clock_mu_ -= 1.0f;
    if (clock_mu_ <= 0.0f) {
      const float fraction = 1.0f + clock_mu_;
      float symbol = previous_matched_ * (1.0f - fraction) + matched * fraction;
      if (have_previous_symbol_) {
        const float error = sign(previous_symbol_) * symbol - sign(symbol) * previous_symbol_;
        state_.timing_error = error;
        clock_mu_ += kSamplesPerSymbol + 0.05f * error;
      } else {
        clock_mu_ += kSamplesPerSymbol;
        have_previous_symbol_ = true;
      }
      previous_symbol_ = symbol;
      const float magnitude = fabsf(symbol);
      if (!agc_seeded_ && magnitude > 1.0e-9f) {
        agc_level_ = magnitude;
        agc_seeded_ = true;
      } else if (agc_seeded_) {
        agc_level_ += (magnitude - agc_level_) / 256.0f;
      }
      if (agc_seeded_ && agc_level_ > 1.0e-9f) symbol *= kAgcTarget / agc_level_;
      const int8_t sliced = symbol >= kSlicerThreshold ? 3
                            : symbol >= 0.0f           ? 1
                            : symbol >= -kSlicerThreshold ? -1
                                                         : -3;
      const uint8_t dibit = sliced == 3 ? 1 : sliced == 1 ? 0 : sliced == -1 ? 2 : 3;
      feed_dibit(dibit);
    }
    previous_matched_ = matched;
  }

  void process_cqpsk_sample(float i, float q) {
    ensure_cqpsk_rrc();
    cqpsk_i_[cqpsk_pos_] = i;
    cqpsk_q_[cqpsk_pos_] = q;
    float filtered_i = 0.0f;
    float filtered_q = 0.0f;
    size_t source = cqpsk_pos_;
    for (size_t tap = 0; tap < kCqpskRrcTaps; ++tap) {
      filtered_i += g_cqpsk_rrc[tap] * cqpsk_i_[source];
      filtered_q += g_cqpsk_rrc[tap] * cqpsk_q_[source];
      source = source == 0 ? kCqpskRrcTaps - 1 : source - 1;
    }
    cqpsk_pos_ = (cqpsk_pos_ + 1) % kCqpskRrcTaps;

    const float power = filtered_i * filtered_i + filtered_q * filtered_q;
    cqpsk_power_ += 0.001f * (power - cqpsk_power_);
    if (cqpsk_power_ > 1.0e-9f) {
      const float gain = 0.95f / sqrtf(cqpsk_power_);
      filtered_i *= gain;
      filtered_q *= gain;
    }
    cqpsk_filtered_i_[cqpsk_filtered_pos_] = filtered_i;
    cqpsk_filtered_q_[cqpsk_filtered_pos_] = filtered_q;

    cqpsk_mu_ -= 1.0f;
    if (cqpsk_mu_ <= 0.0f) {
      const float fraction = 1.0f + cqpsk_mu_;
      const auto history = [&](const std::array<float, 12>& samples, size_t delay) {
        const size_t newer = (cqpsk_filtered_pos_ + samples.size() - delay) % samples.size();
        const size_t older = (newer + samples.size() - 1) % samples.size();
        return samples[older] * (1.0f - fraction) + samples[newer] * fraction;
      };
      float symbol_i = history(cqpsk_filtered_i_, 0);
      float symbol_q = history(cqpsk_filtered_q_, 0);
      const float midpoint_i = history(cqpsk_filtered_i_, kSamplesPerSymbol / 2);
      const float midpoint_q = history(cqpsk_filtered_q_, kSamplesPerSymbol / 2);
      if (cqpsk_have_symbol_) {
        const float error = (symbol_i - cqpsk_previous_symbol_i_) * midpoint_i +
                            (symbol_q - cqpsk_previous_symbol_q_) * midpoint_q;
        state_.timing_error = error;
        cqpsk_mu_ += kSamplesPerSymbol + cqpsk_timing_gain_ * error;
      } else {
        cqpsk_mu_ += kSamplesPerSymbol;
        cqpsk_have_symbol_ = true;
      }
      cqpsk_previous_symbol_i_ = symbol_i;
      cqpsk_previous_symbol_q_ = symbol_q;

      const float cs = cosf(cqpsk_phase_);
      const float sn = sinf(cqpsk_phase_);
      const float rotated_i = symbol_i * cs + symbol_q * sn;
      const float rotated_q = symbol_q * cs - symbol_i * sn;
      const float error = sign(rotated_i) * rotated_q - sign(rotated_q) * rotated_i;
      const float carrier_beta = 0.125f * cqpsk_carrier_gain_ * cqpsk_carrier_gain_;
      cqpsk_frequency_ = std::clamp(cqpsk_frequency_ + carrier_beta * error,
                                    -kPi / 8.0f, kPi / 8.0f);
      cqpsk_phase_ = wrap_pi(cqpsk_phase_ + cqpsk_frequency_ +
                            cqpsk_carrier_gain_ * error);
      state_.carrier_error_hz = cqpsk_frequency_ * kSymbolRate / (2.0f * kPi);

      if (cqpsk_have_differential_) {
        const float cross = cqpsk_previous_rotated_i_ * rotated_q -
                            cqpsk_previous_rotated_q_ * rotated_i;
        const float dot = cqpsk_previous_rotated_i_ * rotated_i +
                          cqpsk_previous_rotated_q_ * rotated_q;
        const float phase = wrap_pi(atan2f(cross, dot) - kPi / 4.0f);
        const uint8_t quadrant = phase >= -kPi / 4.0f && phase < kPi / 4.0f ? 0
                                 : phase >= kPi / 4.0f && phase < 3.0f * kPi / 4.0f ? 1
                                 : phase >= -3.0f * kPi / 4.0f && phase < -kPi / 4.0f ? 3
                                                                                       : 2;
        constexpr uint8_t kLsmDibitRemap[4] = {0, 1, 3, 2};
        feed_dibit(kLsmDibitRemap[quadrant]);
      } else {
        cqpsk_have_differential_ = true;
      }
      cqpsk_previous_rotated_i_ = rotated_i;
      cqpsk_previous_rotated_q_ = rotated_q;
    }
    cqpsk_filtered_pos_ = (cqpsk_filtered_pos_ + 1) % cqpsk_filtered_i_.size();
  }

 private:

  static float sign(float value) {
    return value > 0.0f ? 1.0f : value < 0.0f ? -1.0f : 0.0f;
  }

  void feed_dibit(uint8_t dibit) {
    sync_history_[sync_pos_] = dibit & 3;
    sync_pos_ = (sync_pos_ + 1) % sync_history_.size();
    if (sync_primed_ < sync_history_.size()) ++sync_primed_;
    if (sync_primed_ == sync_history_.size()) {
      int best_mismatch = 25;
      uint8_t best_rotation = 0;
      for (uint8_t rotation : {uint8_t{0}, uint8_t{2}}) {
        int mismatch = 0;
        size_t index = sync_pos_;
        for (size_t i = 0; i < kFrameSync.size(); ++i) {
          if (((sync_history_[index] + rotation) & 3) != kFrameSync[i]) ++mismatch;
          index = (index + 1) % sync_history_.size();
        }
        if (mismatch < best_mismatch) {
          best_mismatch = mismatch;
          best_rotation = rotation;
        }
      }
      best_sync_mismatch_ = std::min(best_sync_mismatch_, best_mismatch);
      if (best_mismatch <= sync_tolerance_) {
        ++state_.sync_words;
        frame_active_ = true;
        frame_rotation_ = best_rotation;
        frame_air_index_ = 24;
        frame_data_count_ = 0;
        tsbk_blocks_ = 0;
        collecting_nid_ = true;
        return;
      }
    }
    if (!frame_active_) return;
    const bool status_symbol = frame_air_index_ % 36 == 35;
    ++frame_air_index_;
    if (status_symbol) return;
    const uint8_t canonical = (dibit + frame_rotation_) & 3;
    if (collecting_nid_) {
      nid_dibits_[frame_data_count_++] = canonical;
      if (frame_data_count_ == nid_dibits_.size()) decode_nid();
      return;
    }
    if (frame_duid_ == 0x7) {
      tsbk_channel_[frame_data_count_++] = canonical;
      if (frame_data_count_ == tsbk_channel_.size()) decode_tsbk();
    } else {
      ldu_payload_[frame_data_count_++] = canonical;
      if (frame_data_count_ == ldu_payload_.size()) decode_ldu();
    }
  }

  void decode_nid() {
    uint64_t codeword = 0;
    for (size_t i = 0; i < nid_dibits_.size(); ++i) {
      const uint8_t d = nid_dibits_[i];
      for (int bit = 1; bit >= 0; --bit) {
        const size_t wire_bit = 2 * i + (1 - bit);
        if (wire_bit < 63) codeword = (codeword << 1) | ((d >> bit) & 1u);
      }
    }
    uint16_t decoded = 0;
    const int corrected = bch_decode(codeword, decoded);
    const uint8_t duid = decoded & 0x0F;
    const uint8_t trailing = nid_dibits_[31] & 1;
    const uint8_t expected_trailing = (duid == 0x5 || duid == 0xA) ? 1 : 0;
    if (corrected < 0 || trailing != expected_trailing ||
        (duid != 0x7 && duid != 0x5 && duid != 0xA)) {
      ++state_.nid_failed;
      frame_active_ = false;
      refresh_health(now_ms_);
      return;
    }
    state_.nac = (decoded >> 4) & 0x0FFF;
    ++state_.nid_good;
    state_.nid_corrected_bits += corrected;
    fec_error_bits_ += corrected;
    fec_total_bits_ += 64;
    last_valid_ms_ = now_ms_;
    collecting_nid_ = false;
    frame_data_count_ = 0;
    frame_duid_ = duid;
  }

  void decode_ldu() {
    uint8_t voice_bits[kVoiceFrameBits];
    ++state_.voice_ldus;
    state_.last_voice_ms = now_ms_;
    last_valid_ms_ = state_.last_voice_ms;
    if (frame_duid_ == 0xA) {
      EncryptionSync encryption{};
      if (decode_encryption_payload(ldu_payload_.data(), ldu_payload_.size(), &encryption)) {
        state_.voice_encryption = encryption;
        ++state_.encryption_sync_good;
      } else {
        state_.voice_encryption = {};
        ++state_.encryption_sync_failed;
      }
    }
    for (const size_t offset : kVoiceBitOffsets) {
      for (size_t bit = 0; bit < kVoiceFrameBits; ++bit) {
        const size_t source = offset + bit;
        const uint8_t dibit = ldu_payload_[source / 2];
        voice_bits[bit] = static_cast<uint8_t>((dibit >> (1 - source % 2)) & 1u);
      }
      VoiceFrame frame{};
      std::memcpy(frame.bits, voice_bits, sizeof(frame.bits));
      frame.sequence = ++state_.voice_frames;
      frame.encryption = state_.voice_encryption;
      if (voice_sink_ == nullptr)
        ++state_.voice_unrouted_frames;
      else if (!voice_sink_(frame, voice_context_))
        ++state_.voice_queue_drops;
    }
    frame_active_ = false;
    refresh_health(last_valid_ms_);
  }

  void decode_tsbk() {
    std::array<uint8_t, 98> coding{};
    for (size_t i = 0; i < coding.size(); ++i) coding[i] = tsbk_channel_[kDeinterleave[i]];
    std::array<uint8_t, 48> decoded{};
    const int metric = trellis_decode(coding, decoded);
    std::array<uint8_t, 12> bytes{};
    for (size_t i = 0; i < bytes.size(); ++i)
      bytes[i] = static_cast<uint8_t>((decoded[4 * i] << 6) | (decoded[4 * i + 1] << 4) |
                                      (decoded[4 * i + 2] << 2) | decoded[4 * i + 3]);
    state_.last_trellis_metric = static_cast<uint16_t>(std::max(0, metric));
    fec_error_bits_ += std::max(0, metric);
    fec_total_bits_ += 196;
    if (crc_augmented(bytes.data(), bytes.size()) != 0) {
      ++state_.tsbk_failed;
      frame_active_ = false;
      refresh_health(now_ms_);
      return;
    }
    ++state_.tsbk_good;
    last_valid_ms_ = now_ms_;
    dispatch_tsbk(bytes);
    const bool last_block = (bytes[0] & 0x80) != 0;
    if (last_block || ++tsbk_blocks_ >= 3) {
      frame_active_ = false;
    } else {
      frame_data_count_ = 0;
    }
    refresh_health(last_valid_ms_);
  }

  void dispatch_tsbk(const std::array<uint8_t, 12>& bytes) {
    const uint8_t opcode = bytes[0] & 0x3F;
    const uint8_t* payload = bytes.data() + 2;
    if (opcode == 0x3D || opcode == 0x34 || opcode == 0x33) {
      const uint8_t id = payload[0] >> 4;
      const uint8_t slots = opcode == 0x33
                                ? slots_for_channel_type(payload[0] & 0x0F)
                                : 1;
      const uint16_t step = static_cast<uint16_t>(payload[2] & 0x03) << 8 | payload[3];
      const uint32_t base5 = static_cast<uint32_t>(payload[4]) << 24 |
                             static_cast<uint32_t>(payload[5]) << 16 |
                             static_cast<uint32_t>(payload[6]) << 8 | payload[7];
      band_plan_[id] = {slots != 0, slots, static_cast<uint32_t>(step) * 125u,
                        static_cast<uint64_t>(base5) * 5u};
      if (opcode == 0x33 && slots != 0) ++state_.phase2_band_plans;
      return;
    }
    if (opcode == 0x3B) {
      state_.wacn = static_cast<uint32_t>(payload[1]) << 12 |
                    static_cast<uint32_t>(payload[2]) << 4 | payload[3] >> 4;
      state_.system_id = static_cast<uint16_t>(payload[3] & 0x0F) << 8 | payload[4];
      state_.identity_valid = state_.wacn != 0 && state_.system_id != 0;
      return;
    }
    if (opcode == 0x3A) {
      state_.system_id = static_cast<uint16_t>(payload[1] & 0x0F) << 8 | payload[2];
      state_.rfss = payload[3];
      state_.site = payload[4];
      return;
    }
    if (opcode == 0x2B && (payload[0] & 3) == 0) {
      state_.rfss = payload[3];
      state_.site = payload[4];
      return;
    }
    if (opcode == 0x00) {
      const uint16_t channel = static_cast<uint16_t>(payload[1]) << 8 | payload[2];
      add_grant(payload[0], channel >> 12, channel & 0x0FFF,
                static_cast<uint16_t>(payload[3]) << 8 | payload[4],
                static_cast<uint32_t>(payload[5]) << 16 |
                    static_cast<uint32_t>(payload[6]) << 8 | payload[7]);
      return;
    }
    if (opcode == 0x03) {
      const uint16_t channel = static_cast<uint16_t>(payload[2]) << 8 | payload[3];
      add_grant(payload[0], channel >> 12, channel & 0x0FFF,
                static_cast<uint16_t>(payload[6]) << 8 | payload[7], 0);
      return;
    }
    if (opcode == 0x02) {
      const uint16_t channel_a = static_cast<uint16_t>(payload[0]) << 8 | payload[1];
      const uint16_t channel_b = static_cast<uint16_t>(payload[4]) << 8 | payload[5];
      add_grant(0, channel_a >> 12, channel_a & 0x0FFF,
                static_cast<uint16_t>(payload[2]) << 8 | payload[3], 0);
      if (channel_b != 0)
        add_grant(0, channel_b >> 12, channel_b & 0x0FFF,
                  static_cast<uint16_t>(payload[6]) << 8 | payload[7], 0);
    }
  }

  void add_grant(uint8_t service, uint8_t channel_id, uint16_t channel_number,
                 uint16_t talkgroup, uint32_t source) {
    Grant grant;
    grant.valid = talkgroup != 0;
    grant.encrypted = (service & 0x40) != 0;
    grant.emergency = (service & 0x80) != 0;
    grant.service_options = service;
    grant.channel_id = channel_id;
    grant.channel_number = channel_number;
    grant.talkgroup = talkgroup;
    grant.source_id = source;
    grant.wacn = state_.wacn;
    grant.system_id = state_.system_id;
    grant.rfss = state_.rfss;
    grant.site = state_.site;
    grant.seen_ms = now_ms_;
    if (channel_id < band_plan_.size() && band_plan_[channel_id].known) {
      const BandPlanSlot& plan = band_plan_[channel_id];
      const ChannelAssignment assignment = map_channel(
          plan.base_hz, plan.spacing_hz, plan.slots_per_carrier, channel_number);
      grant.tdma = plan.slots_per_carrier > 1;
      grant.slot = assignment.slot;
      grant.frequency_hz = assignment.frequency_hz;
      if (grant.tdma) {
        ++state_.phase2_grants;
        if (!assignment.valid) ++state_.phase2_mapping_errors;
      }
    }
    if (!grant.valid) return;
    for (size_t i = kRecentGrantCount - 1; i > 0; --i)
      state_.recent_grants[i] = state_.recent_grants[i - 1];
    state_.recent_grants[0] = grant;
    state_.current_grant = grant;
  }

  void refresh_health(uint32_t now) {
    state_.frame_sync = last_valid_ms_ != 0 && now - last_valid_ms_ < kLockTimeoutMs;
    const uint32_t frame_total = state_.tsbk_good + state_.tsbk_failed;
    state_.frame_error_percent = frame_total == 0
                                     ? 0.0f
                                     : 100.0f * state_.tsbk_failed / frame_total;
    state_.estimated_ber_percent = fec_total_bits_ == 0
                                       ? 0.0f
                                       : 100.0f * fec_error_bits_ / fec_total_bits_;
    state_.afc_offset_hz = afc_dc_ * kSymbolRate / (2.0f * kPi);
    state_.symbol_level = agc_level_;
    const uint32_t valid = state_.nid_good + state_.tsbk_good;
    const uint32_t elapsed = now - started_ms_;
    state_.decode_rate_hz = elapsed == 0 ? 0.0f : 1000.0f * valid / elapsed;
    state_.lock_quality_percent = valid == 0
        ? 100.0f * (24 - std::min(24, best_sync_mismatch_)) / 24.0f
        : 100.0f * valid / (valid + state_.nid_failed + state_.tsbk_failed);
  }

  Snapshot state_{};
  uint32_t now_ms_ = 0;
  VoiceSink voice_sink_ = nullptr;
  void* voice_context_ = nullptr;
  std::array<BandPlanSlot, 16> band_plan_{};
  uint32_t last_valid_ms_ = 0;
  uint64_t fec_error_bits_ = 0;
  uint64_t fec_total_bits_ = 0;

  uint32_t started_ms_ = now_ms_;
  float previous_i_ = 0, previous_q_ = 0;
  bool have_previous_iq_ = false;
  std::array<float, kSamplesPerSymbol> matched_history_{};
  size_t matched_pos_ = 0;
  float matched_sum_ = 0;
  float afc_dc_ = 0;
  float previous_matched_ = 0;
  bool have_previous_matched_ = false;
  float clock_mu_ = kSamplesPerSymbol;
  float previous_symbol_ = 0;
  bool have_previous_symbol_ = false;
  float agc_level_ = 0;
  bool agc_seeded_ = false;
  std::array<float, kCqpskRrcTaps> cqpsk_i_{};
  std::array<float, kCqpskRrcTaps> cqpsk_q_{};
  size_t cqpsk_pos_ = 0;
  std::array<float, 12> cqpsk_filtered_i_{};
  std::array<float, 12> cqpsk_filtered_q_{};
  size_t cqpsk_filtered_pos_ = 0;
  float cqpsk_power_ = 0.0f;
  float cqpsk_mu_ = kSamplesPerSymbol / 2.0f;
  float cqpsk_previous_symbol_i_ = 0.0f;
  float cqpsk_previous_symbol_q_ = 0.0f;
  bool cqpsk_have_symbol_ = false;
  float cqpsk_phase_ = 0.0f;
  float cqpsk_frequency_ = 0.0f;
  float cqpsk_previous_rotated_i_ = 1.0f;
  float cqpsk_previous_rotated_q_ = 0.0f;
  bool cqpsk_have_differential_ = false;
  float cqpsk_timing_gain_ = kCqpskGardnerGain;
  float cqpsk_carrier_gain_ = kCqpskCostasAlpha;
  int best_sync_mismatch_ = 24;
  int sync_tolerance_ = 4;

  std::array<uint8_t, 24> sync_history_{};
  size_t sync_pos_ = 0;
  size_t sync_primed_ = 0;
  bool frame_active_ = false;
  bool collecting_nid_ = true;
  uint8_t frame_rotation_ = 0;
  uint8_t frame_duid_ = 0;
  size_t frame_air_index_ = 0;
  size_t frame_data_count_ = 0;
  uint8_t tsbk_blocks_ = 0;
  std::array<uint8_t, 32> nid_dibits_{};
  std::array<uint8_t, 98> tsbk_channel_{};
  std::array<uint8_t, kLduPayloadDibits> ldu_payload_{};
};

namespace {
class Receiver {
 public:
  void reset(uint32_t now_ms) {
    c4fm_.reset(now_ms);
    cqpsk_.reset(now_ms);
    cqpsk_.set_sync_tolerance(5);
    cqpsk_.set_cqpsk_gains(timing_gain_, carrier_gain_);
    selected_ = configured_ == Modulation::auto_detect ? Modulation::auto_detect : configured_;
    dc_i_ = dc_q_ = lpf_i1_ = lpf_q1_ = lpf_i2_ = lpf_q2_ = 0.0f;
    decim_i_ = decim_q_ = 0.0f;
    decim_count_ = 0;
    have_pending_iq_byte_ = false;
    if (phase2_enabled_) phase2_.reset(now_ms);
  }

  void set_modulation(Modulation modulation) {
    configured_ = modulation;
    reset(0);
  }

  void configure(Modulation modulation, float timing_gain, float carrier_gain) {
    configured_ = modulation;
    timing_gain_ = std::isfinite(timing_gain) && timing_gain >= 0.0001f && timing_gain <= 0.05f
                       ? timing_gain : kCqpskGardnerGain;
    carrier_gain_ = std::isfinite(carrier_gain) && carrier_gain >= 0.0001f && carrier_gain <= 0.1f
                        ? carrier_gain : kCqpskCostasAlpha;
    reset(0);
  }

  void process(const uint8_t* iq, size_t bytes, uint32_t now_ms,
               VoiceSink sink, void* context) {
    now_ms_ = now_ms;
    c4fm_.prepare(now_ms, selected_ == Modulation::c4fm ? sink : nullptr, context);
    cqpsk_.prepare(now_ms, selected_ == Modulation::cqpsk ? sink : nullptr, context);
    if (iq != nullptr) {
      size_t offset = 0;
      if (have_pending_iq_byte_ && bytes != 0) {
        process_pair(pending_iq_byte_, iq[0]);
        have_pending_iq_byte_ = false;
        offset = 1;
      }
      for (; offset + 1 < bytes; offset += 2) process_pair(iq[offset], iq[offset + 1]);
      if (offset < bytes) {
        pending_iq_byte_ = iq[offset];
        have_pending_iq_byte_ = true;
      }
    }
    c4fm_.finish(now_ms);
    cqpsk_.finish(now_ms);
    select_path(now_ms);
  }

  void set_phase2_acquisition(bool enabled, uint32_t now_ms) {
    if (!enabled && phase2_enabled_) phase2_.finish();
    phase2_enabled_ = enabled;
    if (enabled) phase2_.reset(now_ms);
  }

  void process_channel(float i, float q, uint32_t now_ms) {
    if (phase2_enabled_) phase2_.process(i, q, now_ms);
    else process_phase1_channel(i, q);
  }

  void process_phase2_dibit(uint8_t dibit, uint32_t now_ms) {
    if (phase2_enabled_) phase2_.process_dibit(dibit, now_ms);
  }

  Snapshot state() const {
    const Decoder& decoder = selected_ == Modulation::cqpsk ? cqpsk_ : c4fm_;
    Snapshot result = decoder.state();
    result.configured_modulation = configured_;
    result.selected_modulation = selected_;
    const Phase2State& phase2 = phase2_.state();
    result.phase2_acquisition = phase2_enabled_;
    result.phase2_symbols = phase2.symbols;
    result.phase2_sync_words = phase2.sync_words;
    result.phase2_last_sync_ms = phase2.last_sync_ms;
    result.phase2_best_sync_errors = phase2.best_sync_errors;
    result.phase2_complete_bursts = phase2.complete_bursts;
    result.phase2_truncated_bursts = phase2.truncated_bursts;
    result.phase2_last_burst_ms = phase2.last_burst_ms;
    result.phase2_last_duid_codeword = phase2.last_duid_codeword;
    result.phase2_last_duid = phase2.last_duid;
    result.phase2_last_duid_errors = phase2.last_duid_errors;
    result.phase2_last_duid_valid = phase2.last_duid_valid;
    result.phase2_voice_bursts = phase2.voice_bursts;
    result.phase2_control_bursts = phase2.control_bursts;
    result.phase2_unknown_bursts = phase2.unknown_bursts;
    result.phase2_reverse_polarity = phase2.reverse_polarity;
    return result;
  }

  Modulation modulation() const { return configured_; }

 private:
  void process_pair(uint8_t input_i, uint8_t input_q) {
    constexpr float kChannelAlpha = 0.06f;
    constexpr float kDcAlpha = 1.0f / kInputRate;
    const float raw_i = static_cast<float>(static_cast<int>(input_i) - 128);
    const float raw_q = static_cast<float>(static_cast<int>(input_q) - 128);
    dc_i_ += kDcAlpha * (raw_i - dc_i_);
    dc_q_ += kDcAlpha * (raw_q - dc_q_);
    lpf_i1_ += kChannelAlpha * (raw_i - dc_i_ - lpf_i1_);
    lpf_q1_ += kChannelAlpha * (raw_q - dc_q_ - lpf_q1_);
    lpf_i2_ += kChannelAlpha * (lpf_i1_ - lpf_i2_);
    lpf_q2_ += kChannelAlpha * (lpf_q1_ - lpf_q2_);
    decim_i_ += lpf_i2_;
    decim_q_ += lpf_q2_;
    if (++decim_count_ < kInputDecimation) return;
    const float i = decim_i_ / kInputDecimation;
    const float q = decim_q_ / kInputDecimation;
    decim_i_ = decim_q_ = 0.0f;
    decim_count_ = 0;
    process_channel(i, q, now_ms_);
  }

  void process_phase1_channel(float i, float q) {
    if (selected_ == Modulation::auto_detect || selected_ == Modulation::c4fm)
      c4fm_.process_c4fm_sample(i, q);
    if (selected_ == Modulation::auto_detect || selected_ == Modulation::cqpsk)
      cqpsk_.process_cqpsk_sample(i, q);
  }

  void select_path(uint32_t now_ms) {
    if (configured_ != Modulation::auto_detect) {
      selected_ = configured_;
      return;
    }
    if (selected_ != Modulation::auto_detect) {
      if ((selected_ == Modulation::c4fm ? c4fm_ : cqpsk_).state().frame_sync) return;
      selected_ = Modulation::auto_detect;
      c4fm_.reset(now_ms);
      cqpsk_.reset(now_ms);
      cqpsk_.set_sync_tolerance(5);
      cqpsk_.set_cqpsk_gains(timing_gain_, carrier_gain_);
      return;
    }
    const Snapshot c4 = c4fm_.state();
    const Snapshot cq = cqpsk_.state();
    const uint32_t c4_score = 4 * c4.nid_good + 8 * c4.tsbk_good;
    const uint32_t cq_score = 4 * cq.nid_good + 8 * cq.tsbk_good;
    if (c4_score >= 8 || cq_score >= 8)
      selected_ = cq_score > c4_score ? Modulation::cqpsk : Modulation::c4fm;
  }

  Decoder c4fm_{};
  Decoder cqpsk_{};
  Modulation configured_ = Modulation::auto_detect;
  Modulation selected_ = Modulation::auto_detect;
  float timing_gain_ = kCqpskGardnerGain;
  float carrier_gain_ = kCqpskCostasAlpha;
  float dc_i_ = 0, dc_q_ = 0;
  float lpf_i1_ = 0, lpf_q1_ = 0, lpf_i2_ = 0, lpf_q2_ = 0;
  float decim_i_ = 0, decim_q_ = 0;
  size_t decim_count_ = 0;
  uint8_t pending_iq_byte_ = 0;
  bool have_pending_iq_byte_ = false;
  uint32_t now_ms_ = 0;
  bool phase2_enabled_ = false;
  Phase2Acquirer phase2_{};
};

Receiver g_receiver;
}

void reset(uint32_t now_ms) { g_receiver.reset(now_ms); }

void configure(Modulation modulation, float timing_gain, float carrier_gain) {
  g_receiver.configure(modulation, timing_gain, carrier_gain);
}

void set_modulation(Modulation modulation) { g_receiver.set_modulation(modulation); }

Modulation modulation() { return g_receiver.modulation(); }

const char* modulation_name(Modulation modulation) {
  switch (modulation) {
    case Modulation::c4fm: return "c4fm";
    case Modulation::cqpsk: return "cqpsk";
    default: return "auto";
  }
}

void process_cu8(const uint8_t* iq, size_t bytes, uint32_t now_ms,
                 VoiceSink voice_sink, void* voice_context) {
  g_receiver.process(iq, bytes, now_ms, voice_sink, voice_context);
}

Snapshot snapshot() { return g_receiver.state(); }

ChannelAssignment map_channel(uint64_t base_hz, uint32_t spacing_hz,
                              uint8_t slots_per_carrier,
                              uint16_t channel_number) {
  ChannelAssignment result;
  if (spacing_hz == 0 || slots_per_carrier == 0) return result;
  const uint64_t carrier = base_hz +
      static_cast<uint64_t>(channel_number / slots_per_carrier) * spacing_hz;
  if (carrier > UINT32_MAX) return result;
  result.valid = true;
  result.frequency_hz = static_cast<uint32_t>(carrier);
  result.slot = static_cast<uint8_t>(channel_number % slots_per_carrier);
  return result;
}

void set_phase2_acquisition(bool enabled, uint32_t now_ms) {
  g_receiver.set_phase2_acquisition(enabled, now_ms);
}

void process_channel_iq(float i, float q, uint32_t now_ms) {
  g_receiver.process_channel(i, q, now_ms);
}

void process_phase2_dibit(uint8_t dibit, uint32_t now_ms) {
  g_receiver.process_phase2_dibit(dibit, now_ms);
}

bool decode_ldu2_encryption(const uint8_t* payload, size_t dibits,
                            EncryptionSync* result) {
  return decode_encryption_payload(payload, dibits, result);
}

bool self_check() { return Decoder::self_check(); }

}  // namespace orcsdr::p25core
