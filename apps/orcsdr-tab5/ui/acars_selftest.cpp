#include "acars_selftest.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#include "acars_core.hpp"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace orcsdr::acars_selftest {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 240000.0;
constexpr int kSamplesPerBit = 100;                     // 240 kS/s / 2400 bit/s
constexpr double kCarrierHz = 60000.0;                  // where Frontend240k expects the carrier
constexpr double kDepth = 0.5;
constexpr double kAmplitude = 0.5;
constexpr char kTail[] = "N123456";
constexpr char kText[] = "1234UAL111 TEST MESSAGE";

// ARINC 618 block: mode, address, NAK, label, block id, STX, text, ETX, BCS (CRC-16), DEL.
size_t build_block(uint8_t* out, size_t capacity) {
  size_t n = 0;
  auto put = [&](uint8_t c) { if (n < capacity) out[n++] = acars::odd_parity_byte(c); };
  put('2');
  for (int i = 0; i < 7; ++i) put(static_cast<uint8_t>(kTail[i]));
  put(0x15);
  put('Q'); put('0');
  put('1');
  put(0x02);
  for (const char* c = kText; *c; ++c) put(static_cast<uint8_t>(*c));
  put(0x03);
  const uint16_t crc = acars::crc16_kermit(out, n);
  if (n + 3 <= capacity) {
    out[n++] = static_cast<uint8_t>(crc);
    out[n++] = static_cast<uint8_t>(crc >> 8);
    out[n++] = 0x7f;
  }
  return n;
}

}  // namespace

const char* Signal::tail() { return kTail; }
const char* Signal::text() { return kText; }

Signal::Signal(int lead_in_samples, double noise_sigma, uint32_t seed)
    : lead_(lead_in_samples), trailer_(4000), noise_sigma_(noise_sigma), rng_(seed ? seed : 1) {
  uint8_t block[64];
  const size_t block_bytes = build_block(block, sizeof(block));
  size_t n = 0;
  for (int i = 0; i < 20 * 8 && n < kMaxBits; ++i) bits_[n++] = true;          // preamble
  auto byte = [&](uint8_t v) { for (int bit = 0; bit < 8 && n < kMaxBits; ++bit) bits_[n++] = ((v >> bit) & 1) != 0; };
  byte(acars::odd_parity_byte('+'));
  byte(acars::odd_parity_byte('*'));
  byte(0x16); byte(0x16); byte(0x01);
  for (size_t i = 0; i < block_bytes; ++i) byte(block[i]);
  for (int i = 0; i < 16 && n < kMaxBits; ++i) bits_[n++] = true;
  bit_count_ = n;
}

float Signal::noise() {
  // Sum of four uniforms: close enough to Gaussian for a robustness check, no heavy math.
  float sum = 0.f;
  for (int i = 0; i < 4; ++i) {
    rng_ = rng_ * 1664525u + 1013904223u;
    sum += static_cast<float>(rng_ >> 8) / 16777216.0f - 0.5f;
  }
  return sum * 1.7320508f * static_cast<float>(noise_sigma_);   // variance of the sum of 4 U(-.5,.5) is 1/3
}

size_t Signal::next(uint8_t* out, size_t max_bytes) {
  size_t written = 0;
  const double carrier_step = 2.0 * kPi * kCarrierHz / kRate;
  auto q8 = [](double v) { return static_cast<uint8_t>(std::lround(std::fmax(0.0, std::fmin(255.0, 127.5 + 127.5 * v)))); };
  while (written + 2 <= max_bytes) {
    double i_val = 0, q_val = 0;
    if (lead_ > 0) {
      --lead_;
    } else if (bit_index_ < bit_count_) {
      if (sample_in_bit_ == 0) {
        const bool state = bits_[bit_index_];
        tone_step_ = 2.0 * kPi * ((state != previous_state_) ? 1200.0 : 2400.0) / kRate;   // change = 1200 Hz, hold = 2400 Hz
        previous_state_ = state;
      }
      audio_phase_ += tone_step_;
      const double env = (1.0 + kDepth * std::sin(audio_phase_)) / (1.0 + kDepth);
      i_val = kAmplitude * env * std::cos(carrier_phase_);
      q_val = kAmplitude * env * std::sin(carrier_phase_);
      if (++sample_in_bit_ == kSamplesPerBit) { sample_in_bit_ = 0; ++bit_index_; }
    } else if (trailer_ > 0) {
      --trailer_;
    } else {
      break;
    }
    carrier_phase_ += carrier_step;
    if (carrier_phase_ > 2.0 * kPi) carrier_phase_ -= 2.0 * kPi;
    if (noise_sigma_ > 0) { i_val += noise(); q_val += noise(); }
    out[written++] = q8(i_val);
    out[written++] = q8(q_val);
  }
  return written;
}

namespace {

struct Chain {
  acars::Frontend240k front;
  acars::Decoder decoder;
  unsigned blocks = 0;
  bool matched = false;
};

void on_block(void* context, const acars::Message& m) {
  Chain* chain = static_cast<Chain*>(context);
  ++chain->blocks;
  chain->matched = std::strcmp(m.tail, kTail) == 0 && m.label[0] == 'Q' && m.label[1] == '0' && m.has_text &&
                   m.text_length == std::strlen(kText) && std::memcmp(m.text, kText, m.text_length) == 0 && !m.corrected;
}

void on_sample(void* context, float i, float q) { static_cast<Chain*>(context)->decoder.process_channel_iq(i, q); }

void* allocate(size_t bytes) {
#if defined(ESP_PLATFORM)
  void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  return memory;
#else
  return std::malloc(bytes);
#endif
}

void release(void* memory) {
#if defined(ESP_PLATFORM)
  heap_caps_free(memory);
#else
  std::free(memory);
#endif
}

}  // namespace

Result run(uint32_t (*now_us)()) {
  Result result;
  void* memory = allocate(sizeof(Chain));
  void* signal_memory = allocate(sizeof(Signal));
  uint8_t* buffer = static_cast<uint8_t*>(allocate(4096));
  if (!memory || !signal_memory || !buffer) {
    result.allocated = false;
    release(memory); release(signal_memory); release(buffer);
    return result;
  }
  Chain* chain = new (memory) Chain();
  static const int kOffsets[] = {3000, 3023, 3047, 3071, 3099};   // lead-in in samples: five different bit phases
  static const double kNoise[] = {0.0, 0.05};
  for (double sigma : kNoise) {
    for (int offset : kOffsets) {
      chain->front.reset();
      chain->decoder.reset();
      chain->decoder.set_callback(on_block, chain);
      chain->blocks = 0;
      chain->matched = false;
      Signal* signal = new (signal_memory) Signal(offset, sigma, 0x1234u + static_cast<uint32_t>(offset));
      for (;;) {
        const size_t bytes = signal->next(buffer, 4096);
        if (bytes == 0) break;
        const uint32_t t0 = now_us ? now_us() : 0;
        chain->front.process_cu8(buffer, bytes / 2, on_sample, chain);
        if (now_us) result.total_us += now_us() - t0;
        result.samples += static_cast<uint32_t>(bytes / 2);
      }
      ++result.cases;
      if (chain->blocks == 1 && chain->matched) ++result.passed;
      else if (chain->blocks > 1) ++result.duplicates;
      else if (chain->blocks == 0) ++result.missed;
    }
  }
  chain->~Chain();
  release(memory);
  release(signal_memory);
  release(buffer);
  return result;
}

}  // namespace orcsdr::acars_selftest
