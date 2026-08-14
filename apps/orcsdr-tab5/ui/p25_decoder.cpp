#include "p25_decoder.hpp"

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

#include "freertos/FreeRTOS.h"

// P25 physical-layer/FEC details were independently implemented from the
// published TIA-102 field layout and cross-checked against GopherTrunk
// (Apache-2.0, Matt Cheramie and contributors). No OP25/DSD GPL source is used.

namespace orcsdr::p25decoder {
namespace {

constexpr uint32_t kInputRate = 960000;
constexpr uint32_t kChannelRate = 48000;
constexpr uint32_t kSymbolRate = 4800;
constexpr size_t kInputDecimation = kInputRate / kChannelRate;
constexpr size_t kSamplesPerSymbol = kChannelRate / kSymbolRate;
constexpr float kOuterDeviationHz = 1800.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSlicerScale = 2.0f * kPi * kOuterDeviationHz / kChannelRate;
constexpr float kSlicerThreshold = 2.0f * kSlicerScale / 3.0f;
constexpr float kAgcTarget = kSlicerThreshold;
constexpr uint32_t kLockTimeoutMs = 3000;
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
  bool tdma = false;
  uint32_t spacing_hz = 0;
  uint64_t base_hz = 0;
};

std::array<uint8_t, 126> g_gf_exp{};
std::array<int8_t, 64> g_gf_log{};
bool g_gf_ready = false;

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

class Decoder {
 public:
  void reset() { *this = Decoder{}; }

  void process_cu8(const uint8_t* iq, size_t bytes) {
    if (iq == nullptr) return;
    constexpr float kChannelAlpha = 0.06f;
    constexpr float kDcAlpha = 1.0f / kInputRate;
    for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
      const float raw_i = static_cast<float>(static_cast<int>(iq[offset]) - 128);
      const float raw_q = static_cast<float>(static_cast<int>(iq[offset + 1]) - 128);
      dc_i_ += kDcAlpha * (raw_i - dc_i_);
      dc_q_ += kDcAlpha * (raw_q - dc_q_);
      const float centered_i = raw_i - dc_i_;
      const float centered_q = raw_q - dc_q_;
      lpf_i1_ += kChannelAlpha * (centered_i - lpf_i1_);
      lpf_q1_ += kChannelAlpha * (centered_q - lpf_q1_);
      lpf_i2_ += kChannelAlpha * (lpf_i1_ - lpf_i2_);
      lpf_q2_ += kChannelAlpha * (lpf_q1_ - lpf_q2_);
      decim_i_ += lpf_i2_;
      decim_q_ += lpf_q2_;
      if (++decim_count_ < kInputDecimation) continue;
      const float i = decim_i_ / kInputDecimation;
      const float q = decim_q_ / kInputDecimation;
      decim_i_ = decim_q_ = 0;
      decim_count_ = 0;
      process_channel_sample(i, q);
    }
    refresh_health(millis());
  }

  Snapshot state() const { return state_; }

  static bool self_check() {
    Decoder decoder;
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
    return result.nac == kNac && result.nid_good == 1 &&
           result.nid_corrected_bits == 5 && result.tsbk_good == 1 &&
           result.wacn == 0xBEE00 && result.system_id == 0x1F3 &&
           result.last_trellis_metric > 0;
  }

 private:
  void process_channel_sample(float i, float q) {
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
      if (best_mismatch <= 4) {
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
    tsbk_channel_[frame_data_count_++] = canonical;
    if (frame_data_count_ == tsbk_channel_.size()) decode_tsbk();
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
    if (corrected < 0 || trailing != expected_trailing || duid != 0x7) {
      ++state_.nid_failed;
      frame_active_ = false;
      refresh_health(millis());
      return;
    }
    state_.nac = (decoded >> 4) & 0x0FFF;
    ++state_.nid_good;
    state_.nid_corrected_bits += corrected;
    fec_error_bits_ += corrected;
    fec_total_bits_ += 64;
    collecting_nid_ = false;
    frame_data_count_ = 0;
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
      refresh_health(millis());
      return;
    }
    ++state_.tsbk_good;
    last_valid_ms_ = millis();
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
      const uint16_t step = static_cast<uint16_t>(payload[2] & 0x03) << 8 | payload[3];
      const uint32_t base5 = static_cast<uint32_t>(payload[4]) << 24 |
                             static_cast<uint32_t>(payload[5]) << 16 |
                             static_cast<uint32_t>(payload[6]) << 8 | payload[7];
      band_plan_[id] = {true, opcode == 0x33, static_cast<uint32_t>(step) * 125u,
                        static_cast<uint64_t>(base5) * 5u};
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
    if (opcode == 0x00 || opcode == 0x03) {
      const uint16_t channel = static_cast<uint16_t>(payload[1]) << 8 | payload[2];
      add_grant(payload[0], channel >> 12, channel & 0x0FFF,
                static_cast<uint16_t>(payload[3]) << 8 | payload[4],
                static_cast<uint32_t>(payload[5]) << 16 |
                    static_cast<uint32_t>(payload[6]) << 8 | payload[7]);
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
    grant.talkgroup = talkgroup;
    grant.source_id = source;
    grant.seen_ms = millis();
    if (channel_id < band_plan_.size() && band_plan_[channel_id].known) {
      const BandPlanSlot& slot = band_plan_[channel_id];
      const uint64_t frequency = slot.base_hz +
                                 static_cast<uint64_t>(channel_number) * slot.spacing_hz;
      if (frequency <= UINT32_MAX) grant.frequency_hz = static_cast<uint32_t>(frequency);
      grant.tdma = slot.tdma;
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
  }

  Snapshot state_{};
  std::array<BandPlanSlot, 16> band_plan_{};
  uint32_t last_valid_ms_ = 0;
  uint64_t fec_error_bits_ = 0;
  uint64_t fec_total_bits_ = 0;

  float dc_i_ = 0, dc_q_ = 0;
  float lpf_i1_ = 0, lpf_q1_ = 0, lpf_i2_ = 0, lpf_q2_ = 0;
  float decim_i_ = 0, decim_q_ = 0;
  size_t decim_count_ = 0;
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

  std::array<uint8_t, 24> sync_history_{};
  size_t sync_pos_ = 0;
  size_t sync_primed_ = 0;
  bool frame_active_ = false;
  bool collecting_nid_ = true;
  uint8_t frame_rotation_ = 0;
  size_t frame_air_index_ = 0;
  size_t frame_data_count_ = 0;
  uint8_t tsbk_blocks_ = 0;
  std::array<uint8_t, 32> nid_dibits_{};
  std::array<uint8_t, 98> tsbk_channel_{};
};

Decoder g_decoder;
Snapshot g_public_snapshot{};
portMUX_TYPE g_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;

void publish_snapshot() {
  const Snapshot value = g_decoder.state();
  portENTER_CRITICAL(&g_snapshot_mux);
  g_public_snapshot = value;
  portEXIT_CRITICAL(&g_snapshot_mux);
}

}  // namespace

void reset() {
  g_decoder.reset();
  publish_snapshot();
}

void process_cu8(const uint8_t* iq, size_t bytes) {
  g_decoder.process_cu8(iq, bytes);
  publish_snapshot();
}

Snapshot snapshot() {
  Snapshot value;
  portENTER_CRITICAL(&g_snapshot_mux);
  value = g_public_snapshot;
  portEXIT_CRITICAL(&g_snapshot_mux);
  return value;
}

bool self_check() { return Decoder::self_check(); }

}  // namespace orcsdr::p25decoder
