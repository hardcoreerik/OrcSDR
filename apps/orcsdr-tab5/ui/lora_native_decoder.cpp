#include "lora_native_decoder.hpp"

#include "rf_analysis.hpp"

#include <dsps_fft2r.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/aes.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace orcsdr::lora_native {
namespace {

constexpr uint32_t kDecodeRate = 500000;
constexpr uint8_t kMinSf = 7;
constexpr uint8_t kMaxSf = 12;
constexpr size_t kFftPadding = 4;
constexpr size_t kMaxFft = (1u << (kMaxSf + 1u)) * kFftPadding;
constexpr int kTimingSearchSamples = 4;
constexpr float kPayloadClockSkews[] = {0.0f};
constexpr float kLoraLowpass[][5] = {
    {1.55166027e-05f, 3.10332055e-05f, 1.55166027e-05f, -0.794469113f, 0.162197278f},
    {1.0f, 2.0f, 1.0f, -0.828439251f, 0.211890842f},
    {1.0f, 2.0f, 1.0f, -0.901782183f, 0.319181301f},
    {1.0f, 2.0f, 1.0f, -1.02691495f, 0.502233045f},
    {1.0f, 2.0f, 1.0f, -1.22708148f, 0.795048706f},
};
constexpr size_t kMaxBytes = 257;
constexpr size_t kMaxNibbles = kMaxBytes * 2;
constexpr uint8_t kDefaultPsk[] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01,
};

struct Scratch {
  float* fft = nullptr;
  float* downchirp = nullptr;
  uint8_t* resampled = nullptr;
  size_t resampled_capacity = 0;
  uint8_t sf = 0;
  size_t symbol_samples = 0;
  size_t fft_size = 0;
};

Scratch g_scratch{};

uint32_t now_millis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void copy_literal(char* output, size_t output_size, const char* text) {
  if (output == nullptr || output_size == 0) return;
  std::strncpy(output, text, output_size - 1);
  output[output_size - 1] = '\0';
}

uint32_t read_le32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) |
         (static_cast<uint32_t>(value[3]) << 24);
}

void write_le32(uint8_t* value, uint32_t input) {
  value[0] = static_cast<uint8_t>(input);
  value[1] = static_cast<uint8_t>(input >> 8);
  value[2] = static_cast<uint8_t>(input >> 16);
  value[3] = static_cast<uint8_t>(input >> 24);
}

uint8_t parity(uint16_t value) {
  value ^= value >> 8;
  value ^= value >> 4;
  value ^= value >> 2;
  value ^= value >> 1;
  return static_cast<uint8_t>(value & 1u);
}

bool read_varint(const uint8_t* data, size_t size, size_t* offset, uint64_t* value) {
  if (data == nullptr || offset == nullptr || value == nullptr) return false;
  uint64_t result = 0;
  for (uint8_t shift = 0; shift < 64 && *offset < size; shift += 7) {
    const uint8_t byte = data[(*offset)++];
    result |= static_cast<uint64_t>(byte & 0x7fu) << shift;
    if ((byte & 0x80u) == 0) {
      *value = result;
      return true;
    }
  }
  return false;
}

bool skip_field(const uint8_t* data, size_t size, size_t* offset, uint8_t wire) {
  uint64_t length = 0;
  switch (wire) {
    case 0: return read_varint(data, size, offset, &length);
    case 1:
      if (*offset + 8 > size) return false;
      *offset += 8;
      return true;
    case 2:
      if (!read_varint(data, size, offset, &length) || length > size - *offset) return false;
      *offset += static_cast<size_t>(length);
      return true;
    case 5:
      if (*offset + 4 > size) return false;
      *offset += 4;
      return true;
    default: return false;
  }
}

void copy_text(char* output, size_t output_size, const uint8_t* input, size_t input_size) {
  if (output == nullptr || output_size == 0) return;
  const size_t count = std::min(input_size, output_size - 1);
  for (size_t i = 0; i < count; ++i) {
    const uint8_t value = input[i];
    output[i] = value >= 0x20 && value != 0x7f ? static_cast<char>(value) : ' ';
  }
  output[count] = '\0';
}

bool parse_data(const uint8_t* data, size_t size, uint16_t* port, const uint8_t** payload,
                size_t* payload_size) {
  if (port == nullptr || payload == nullptr || payload_size == nullptr) return false;
  bool have_port = false;
  bool have_payload = false;
  size_t offset = 0;
  while (offset < size) {
    uint64_t tag = 0;
    if (!read_varint(data, size, &offset, &tag)) return false;
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const uint8_t wire = static_cast<uint8_t>(tag & 7u);
    if (field == 1 && wire == 0) {
      uint64_t value = 0;
      if (!read_varint(data, size, &offset, &value) || value > 511) return false;
      *port = static_cast<uint16_t>(value);
      have_port = true;
    } else if (field == 2 && wire == 2) {
      uint64_t length = 0;
      if (!read_varint(data, size, &offset, &length) || length > size - offset) return false;
      *payload = data + offset;
      *payload_size = static_cast<size_t>(length);
      offset += *payload_size;
      have_payload = true;
    } else if (!skip_field(data, size, &offset, wire)) {
      return false;
    }
  }
  return have_port && have_payload;
}

bool decrypt_ctr(const uint8_t* encrypted, size_t encrypted_size, const uint8_t* key,
                 size_t key_size, uint32_t sender, uint32_t packet_id, uint8_t* output) {
  if (encrypted == nullptr || output == nullptr || key == nullptr ||
      (key_size != 16 && key_size != 32)) return false;
  uint8_t nonce[16]{};
  write_le32(nonce, packet_id);
  write_le32(nonce + 8, sender);
  uint8_t stream_block[16]{};
  size_t nonce_offset = 0;
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  const int result = mbedtls_aes_setkey_enc(&aes, key, static_cast<unsigned>(key_size * 8)) == 0
                         ? mbedtls_aes_crypt_ctr(&aes, encrypted_size, &nonce_offset, nonce,
                                                 stream_block, encrypted, output)
                         : -1;
  mbedtls_aes_free(&aes);
  return result == 0;
}

bool parse_position(const uint8_t* data, size_t size, int32_t* latitude, int32_t* longitude) {
  if (latitude == nullptr || longitude == nullptr) return false;
  int32_t lat = 0;
  int32_t lon = 0;
  bool have_lat = false;
  bool have_lon = false;
  size_t offset = 0;
  while (offset < size) {
    uint64_t tag = 0;
    if (!read_varint(data, size, &offset, &tag)) return false;
    const uint32_t field = static_cast<uint32_t>(tag >> 3);
    const uint8_t wire = static_cast<uint8_t>(tag & 7u);
    if ((field == 1 || field == 2) && wire == 5) {
      if (offset + 4 > size) return false;
      const int32_t value = static_cast<int32_t>(read_le32(data + offset));
      offset += 4;
      if (field == 1) {
        lat = value;
        have_lat = true;
      } else {
        lon = value;
        have_lon = true;
      }
    } else if (!skip_field(data, size, &offset, wire)) {
      return false;
    }
  }
  if (!have_lat || !have_lon || (lat == 0 && lon == 0)) return false;
  *latitude = lat;
  *longitude = lon;
  return true;
}

void summarize_telemetry(const uint8_t* data, size_t size, char* output, size_t output_size) {
  int battery = -1;
  float voltage = -1.0f;
  size_t offset = 0;
  while (offset < size) {
    uint64_t tag = 0;
    if (!read_varint(data, size, &offset, &tag)) break;
    const uint8_t wire = static_cast<uint8_t>(tag & 7u);
    if (wire != 2) {
      if (!skip_field(data, size, &offset, wire)) break;
      continue;
    }
    uint64_t length = 0;
    if (!read_varint(data, size, &offset, &length) || length > size - offset) break;
    const uint8_t* nested = data + offset;
    const size_t nested_size = static_cast<size_t>(length);
    offset += nested_size;
    size_t nested_offset = 0;
    while (nested_offset < nested_size) {
      uint64_t nested_tag = 0;
      if (!read_varint(nested, nested_size, &nested_offset, &nested_tag)) break;
      const uint32_t field = static_cast<uint32_t>(nested_tag >> 3);
      const uint8_t nested_wire = static_cast<uint8_t>(nested_tag & 7u);
      if (field == 1 && nested_wire == 0) {
        uint64_t value = 0;
        if (!read_varint(nested, nested_size, &nested_offset, &value)) break;
        battery = value <= 100 ? static_cast<int>(value) : -1;
      } else if (field == 2 && nested_wire == 5 && nested_offset + 4 <= nested_size) {
        const uint32_t bits = read_le32(nested + nested_offset);
        std::memcpy(&voltage, &bits, sizeof(voltage));
        nested_offset += 4;
      } else if (!skip_field(nested, nested_size, &nested_offset, nested_wire)) {
        break;
      }
    }
  }
  if (battery >= 0 && voltage > 0.0f) {
    snprintf(output, output_size, "Battery %d%%  %.2fV", battery, static_cast<double>(voltage));
  } else if (battery >= 0) {
    snprintf(output, output_size, "Battery %d%%", battery);
  } else if (voltage > 0.0f) {
    snprintf(output, output_size, "Voltage %.2fV", static_cast<double>(voltage));
  } else {
    strlcpy(output, "TELEMETRY", output_size);
  }
}

void summarize_packet(uint16_t port, const uint8_t* payload, size_t payload_size,
                      Packet* packet) {
  if (packet == nullptr) return;
  if (port == 1 || port == 7) {
    copy_text(packet->text, sizeof(packet->text), payload, payload_size);
  } else if (port == 3) {
    if (parse_position(payload, payload_size, &packet->latitude_e7, &packet->longitude_e7)) {
      snprintf(packet->text, sizeof(packet->text), "GPS %.5f, %.5f",
               packet->latitude_e7 / 10000000.0, packet->longitude_e7 / 10000000.0);
    } else {
      copy_literal(packet->text, sizeof(packet->text), "POSITION");
    }
  } else if (port == 67) {
    summarize_telemetry(payload, payload_size, packet->text, sizeof(packet->text));
  } else if (port == 4) {
    copy_literal(packet->text, sizeof(packet->text), "NODEINFO");
  } else if (port == 5) {
    copy_literal(packet->text, sizeof(packet->text), "ROUTING");
  } else {
    snprintf(packet->text, sizeof(packet->text), "PORT %u", static_cast<unsigned>(port));
  }
}

uint16_t rotate_left(uint16_t value, uint8_t amount, uint8_t width) {
  if (amount == 0) return value;
  const uint16_t mask = static_cast<uint16_t>((1u << width) - 1u);
  return static_cast<uint16_t>(((value << amount) & mask) | (value >> (width - amount)));
}

size_t deinterleave(const uint16_t* symbols, size_t symbol_count, uint8_t ppm,
                    uint16_t* codewords, size_t capacity) {
  if (symbols == nullptr || codewords == nullptr || ppm > capacity || ppm == 0) return 0;
  for (uint8_t column = 0; column < ppm; ++column) {
    uint16_t value = 0;
    for (size_t row = 0; row < symbol_count; ++row) {
      if (rotate_left(symbols[row], static_cast<uint8_t>(row % ppm), ppm) & (1u << column))
        value |= static_cast<uint16_t>(1u << row);
    }
    codewords[column] = value;
  }
  return ppm;
}

uint8_t hamming_decode(uint16_t codeword, uint8_t redundant_bits) {
  if (redundant_bits == 7 || redundant_bits == 8) {
    const uint8_t p2 = parity(codeword & 0b01001011);
    const uint8_t p3 = parity(codeword & 0b00010111);
    const uint8_t p5 = parity(codeword & 0b00101110);
    switch (static_cast<uint8_t>((p2 << 2) | (p3 << 1) | p5)) {
      case 3: codeword ^= 4; break;
      case 5: codeword ^= 8; break;
      case 6: codeword ^= 1; break;
      case 7: codeword ^= 2; break;
      default: break;
    }
  }
  return static_cast<uint8_t>(codeword & 0x0fu);
}

uint16_t gray_code(uint16_t symbol, size_t index, uint8_t sf) {
  const uint16_t mask = static_cast<uint16_t>((1u << sf) - 1u);
  uint16_t value = index < 8 ? static_cast<uint16_t>(symbol / 4u)
                              : static_cast<uint16_t>((symbol + mask) & mask);
  return static_cast<uint16_t>((value >> 1) ^ value);
}

bool parse_header(const uint16_t* symbols, uint8_t sf, uint8_t* payload_len, uint8_t* coding_rate,
                  bool* has_crc, uint8_t* initial_nibbles, size_t* initial_count) {
  uint16_t codewords[16]{};
  const size_t codeword_count = deinterleave(symbols, 8, static_cast<uint8_t>(sf - 2),
                                             codewords, std::size(codewords));
  if (codeword_count < 5) return false;
  uint8_t nibbles[16]{};
  for (size_t i = 0; i < codeword_count; ++i) nibbles[i] = hamming_decode(codewords[i], 8);
  *payload_len = static_cast<uint8_t>((nibbles[0] << 4) | nibbles[1]);
  *coding_rate = static_cast<uint8_t>(nibbles[2] >> 1);
  *has_crc = (nibbles[2] & 1u) != 0;
  if (*payload_len == 0 || *coding_rate < 1 || *coding_rate > 4) return false;
  static constexpr uint8_t kChecksum[5][12] = {
      {1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
      {1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1},
      {0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0},
      {0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1},
      {0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1},
  };
  uint8_t bits[12]{};
  for (size_t nibble = 0; nibble < 3; ++nibble)
    for (size_t bit = 0; bit < 4; ++bit)
      bits[nibble * 4 + bit] = static_cast<uint8_t>((nibbles[nibble] >> (3 - bit)) & 1u);
  const uint8_t received[5] = {
      static_cast<uint8_t>(nibbles[3] & 1u),
      static_cast<uint8_t>((nibbles[4] >> 3) & 1u),
      static_cast<uint8_t>((nibbles[4] >> 2) & 1u),
      static_cast<uint8_t>((nibbles[4] >> 1) & 1u),
      static_cast<uint8_t>(nibbles[4] & 1u),
  };
  for (size_t row = 0; row < 5; ++row) {
    uint8_t value = 0;
    for (size_t bit = 0; bit < 12; ++bit) value ^= static_cast<uint8_t>(kChecksum[row][bit] & bits[bit]);
    if (value != received[row]) return false;
  }
  *initial_count = codeword_count - 5;
  for (size_t i = 0; i < *initial_count; ++i) initial_nibbles[i] = nibbles[i + 5];
  return true;
}

bool parse_raw_header(const uint16_t* raw_symbols, uint8_t sf, uint8_t* payload_len,
                      uint8_t* coding_rate, bool* has_crc) {
  uint16_t gray[8]{};
  uint8_t initial[16]{};
  size_t initial_count = 0;
  for (size_t i = 0; i < std::size(gray); ++i) gray[i] = gray_code(raw_symbols[i], i, sf);
  return parse_header(gray, sf, payload_len, coding_rate, has_crc, initial, &initial_count);
}

void dewhiten(uint8_t* data, size_t size) {
  uint8_t reg = 0xff;
  for (size_t i = 0; i < size; ++i) {
    data[i] ^= reg;
    reg = static_cast<uint8_t>((reg << 1) ^ ((reg >> 7) & 1u) ^ ((reg >> 5) & 1u) ^
                               ((reg >> 4) & 1u) ^ ((reg >> 3) & 1u));
  }
}

uint16_t crc_xmodem(const uint8_t* data, size_t size) {
  uint16_t crc = 0;
  for (size_t i = 0; i < size; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = static_cast<uint16_t>((crc & 0x8000u) ? (crc << 1) ^ 0x1021u : crc << 1);
  }
  return crc;
}

bool phy_crc_matches(const uint8_t* data, size_t payload_len, const uint8_t* physical_crc) {
  if (payload_len < 2 || physical_crc == nullptr) return false;
  const uint16_t crc = crc_xmodem(data, payload_len - 2);
  const uint8_t low = static_cast<uint8_t>(crc) ^ data[payload_len - 1];
  const uint8_t high = static_cast<uint8_t>(crc >> 8) ^ data[payload_len - 2];
  return physical_crc[0] == low && physical_crc[1] == high;
}

bool configure_chirp(uint8_t sf, uint32_t bandwidth_hz) {
  if (sf < kMinSf || sf > kMaxSf || bandwidth_hz != 250000 || g_scratch.downchirp == nullptr)
    return false;
  const size_t symbol_samples = 1u << (sf + 1u);
  const size_t fft_size = symbol_samples * kFftPadding;
  if (g_scratch.sf == sf && g_scratch.fft_size == fft_size) return true;
  constexpr float kPi = 3.14159265358979323846f;
  const float bandwidth = static_cast<float>(bandwidth_hz);
  const float symbols = static_cast<float>(1u << sf);
  const float seconds_per_symbol = symbols / bandwidth;
  const float dfdt = -bandwidth / seconds_per_symbol;
  for (size_t i = 0; i < symbol_samples; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kDecodeRate);
    const float phase = 2.0f * kPi * (bandwidth * 0.5f + 0.5f * dfdt * t) * t;
    g_scratch.downchirp[i * 2] = cosf(phase);
    g_scratch.downchirp[i * 2 + 1] = sinf(phase);
  }
  g_scratch.sf = sf;
  g_scratch.symbol_samples = symbol_samples;
  g_scratch.fft_size = fft_size;
  return true;
}

void interpolated_iq(const uint8_t* cu8, size_t samples, uint32_t sample_rate, size_t output_index,
                     float* real, float* imaginary) {
  // ponytail: linear resampling is the bounded baseline; use ESP-DSP polyphase FIR only if
  // archived-vector or weak-signal acceptance shows it is needed.
  const uint64_t scaled = static_cast<uint64_t>(output_index) * sample_rate;
  const size_t first = static_cast<size_t>(scaled / kDecodeRate);
  const uint32_t remainder = static_cast<uint32_t>(scaled % kDecodeRate);
  const size_t second = std::min(first + 1, samples - 1);
  const float ratio = static_cast<float>(remainder) / static_cast<float>(kDecodeRate);
  const float a_real = (static_cast<int>(cu8[first * 2]) - 127.5f) / 127.5f;
  const float a_imag = (static_cast<int>(cu8[first * 2 + 1]) - 127.5f) / 127.5f;
  const float b_real = (static_cast<int>(cu8[second * 2]) - 127.5f) / 127.5f;
  const float b_imag = (static_cast<int>(cu8[second * 2 + 1]) - 127.5f) / 127.5f;
  *real = a_real + (b_real - a_real) * ratio;
  *imaginary = a_imag + (b_imag - a_imag) * ratio;
}

bool filter_capture(const uint8_t* cu8, size_t samples, uint32_t sample_rate,
                    const uint8_t** output, size_t* output_samples) {
  if (cu8 == nullptr || output == nullptr || output_samples == nullptr || sample_rate != 960000)
    return false;
  if (g_scratch.resampled_capacity < samples) {
    if (g_scratch.resampled != nullptr) heap_caps_free(g_scratch.resampled);
    g_scratch.resampled = static_cast<uint8_t*>(
        heap_caps_malloc(samples * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_scratch.resampled_capacity = g_scratch.resampled == nullptr ? 0 : samples;
  }
  if (g_scratch.resampled == nullptr) return false;
  float i_state[std::size(kLoraLowpass)][2]{};
  float q_state[std::size(kLoraLowpass)][2]{};
  for (size_t index = 0; index < samples; ++index) {
    float real = (static_cast<int>(cu8[index * 2]) - 127.5f) / 127.5f;
    float imaginary = (static_cast<int>(cu8[index * 2 + 1]) - 127.5f) / 127.5f;
    for (size_t section = 0; section < std::size(kLoraLowpass); ++section) {
      const float* coeffs = kLoraLowpass[section];
      const float filtered_real = coeffs[0] * real + i_state[section][0];
      i_state[section][0] = coeffs[1] * real - coeffs[3] * filtered_real + i_state[section][1];
      i_state[section][1] = coeffs[2] * real - coeffs[4] * filtered_real;
      const float filtered_imaginary = coeffs[0] * imaginary + q_state[section][0];
      q_state[section][0] = coeffs[1] * imaginary - coeffs[3] * filtered_imaginary + q_state[section][1];
      q_state[section][1] = coeffs[2] * imaginary - coeffs[4] * filtered_imaginary;
      real = filtered_real;
      imaginary = filtered_imaginary;
    }
    g_scratch.resampled[index * 2] = static_cast<uint8_t>(std::clamp(lroundf(real * 127.5f + 127.5f), 0l, 255l));
    g_scratch.resampled[index * 2 + 1] = static_cast<uint8_t>(std::clamp(lroundf(imaginary * 127.5f + 127.5f), 0l, 255l));
    if ((index & 0x3ffffu) == 0) vTaskDelay(1);
  }
  *output = g_scratch.resampled;
  *output_samples = samples;
  return true;
}

bool linear_resample_capture(const uint8_t* cu8, size_t samples, uint32_t sample_rate,
                             const uint8_t** output, size_t* output_samples) {
  if (cu8 == nullptr || output == nullptr || output_samples == nullptr || sample_rate < kDecodeRate)
    return false;
  const size_t needed = static_cast<size_t>(static_cast<uint64_t>(samples) * kDecodeRate / sample_rate);
  if (g_scratch.resampled_capacity < needed) {
    if (g_scratch.resampled != nullptr) heap_caps_free(g_scratch.resampled);
    g_scratch.resampled = static_cast<uint8_t*>(
        heap_caps_malloc(needed * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_scratch.resampled_capacity = g_scratch.resampled == nullptr ? 0 : needed;
  }
  if (g_scratch.resampled == nullptr) return false;
  for (size_t index = 0; index < needed; ++index) {
    const uint64_t scaled = static_cast<uint64_t>(index) * sample_rate;
    const size_t first = static_cast<size_t>(scaled / kDecodeRate);
    const size_t second = std::min(first + 1, samples - 1);
    const uint32_t remainder = static_cast<uint32_t>(scaled % kDecodeRate);
    for (size_t component = 0; component < 2; ++component) {
      const int a = cu8[first * 2 + component];
      const int b = cu8[second * 2 + component];
      g_scratch.resampled[index * 2 + component] = static_cast<uint8_t>(
          (a * static_cast<int>(kDecodeRate - remainder) + b * static_cast<int>(remainder) +
           static_cast<int>(kDecodeRate / 2)) / static_cast<int>(kDecodeRate));
    }
    if ((index & 0x3ffffu) == 0) vTaskDelay(1);
  }
  *output = g_scratch.resampled;
  *output_samples = needed;
  return true;
}

bool dechirp_peak(const uint8_t* cu8, size_t samples, uint32_t sample_rate, size_t start,
                  bool input_is_up, uint16_t* peak, float* height, float cfo_hz = 0.0f) {
  if (peak == nullptr || height == nullptr || g_scratch.fft == nullptr || g_scratch.fft_size == 0)
    return false;
  const size_t n = g_scratch.fft_size;
  const size_t symbol_samples = g_scratch.symbol_samples;
  const uint64_t output_samples = static_cast<uint64_t>(samples) * kDecodeRate / sample_rate;
  if (start + symbol_samples > output_samples) return false;
  float phase_real = 1.0f;
  float phase_imaginary = 0.0f;
  float step_real = 1.0f;
  float step_imaginary = 0.0f;
  if (cfo_hz != 0.0f) {
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float phase = -kTwoPi * cfo_hz * static_cast<float>(start) / kDecodeRate;
    const float step = -kTwoPi * cfo_hz / kDecodeRate;
    phase_real = cosf(phase);
    phase_imaginary = sinf(phase);
    step_real = cosf(step);
    step_imaginary = sinf(step);
  }
  std::memset(g_scratch.fft, 0, sizeof(float) * n * 2);
  for (size_t i = 0; i < symbol_samples; ++i) {
    if ((i & 0x3ffu) == 0) vTaskDelay(1);
    float real = 0;
    float imaginary = 0;
    interpolated_iq(cu8, samples, sample_rate, start + i, &real, &imaginary);
    if (cfo_hz != 0.0f) {
      const float corrected_real = real * phase_real - imaginary * phase_imaginary;
      imaginary = real * phase_imaginary + imaginary * phase_real;
      real = corrected_real;
      const float next_real = phase_real * step_real - phase_imaginary * step_imaginary;
      phase_imaginary = phase_real * step_imaginary + phase_imaginary * step_real;
      phase_real = next_real;
    }
    const float chirp_real = g_scratch.downchirp[i * 2];
    const float chirp_imag = input_is_up ? g_scratch.downchirp[i * 2 + 1]
                                         : -g_scratch.downchirp[i * 2 + 1];
    g_scratch.fft[i * 2] = real * chirp_real - imaginary * chirp_imag;
    g_scratch.fft[i * 2 + 1] = real * chirp_imag + imaginary * chirp_real;
  }
  if (dsps_fft2r_fc32(g_scratch.fft, static_cast<int>(n)) != ESP_OK ||
      dsps_bit_rev_fc32(g_scratch.fft, static_cast<int>(n)) != ESP_OK) return false;
  const size_t bin_count = n / 2;
  float best = -1.0f;
  size_t best_bin = 0;
  for (size_t bin = 0; bin < bin_count; ++bin) {
    const float a_real = g_scratch.fft[bin * 2];
    const float a_imag = g_scratch.fft[bin * 2 + 1];
    const float b_real = g_scratch.fft[(bin + bin_count) * 2];
    const float b_imag = g_scratch.fft[(bin + bin_count) * 2 + 1];
    const float magnitude = sqrtf(a_real * a_real + a_imag * a_imag) +
                            sqrtf(b_real * b_real + b_imag * b_imag);
    if (magnitude > best) {
      best = magnitude;
      best_bin = bin;
    }
  }
  *peak = static_cast<uint16_t>(best_bin);
  *height = best;
  return true;
}

uint16_t peak_to_symbol(uint16_t peak, uint16_t preamble_peak, size_t fft_bins, size_t bins) {
  const size_t delta = (static_cast<size_t>(peak) + fft_bins - preamble_peak) % fft_bins;
  return static_cast<uint16_t>(((delta + kFftPadding / 2) / kFftPadding) % bins);
}

float refined_cfo_hz(uint16_t peak, size_t fft_bins, uint32_t bandwidth_hz) {
  if (g_scratch.fft == nullptr || fft_bins == 0) return 0.0f;
  const auto magnitude = [fft_bins](size_t bin) {
    const float a_real = g_scratch.fft[bin * 2];
    const float a_imaginary = g_scratch.fft[bin * 2 + 1];
    const float b_real = g_scratch.fft[(bin + fft_bins) * 2];
    const float b_imaginary = g_scratch.fft[(bin + fft_bins) * 2 + 1];
    return sqrtf(a_real * a_real + a_imaginary * a_imaginary) +
           sqrtf(b_real * b_real + b_imaginary * b_imaginary);
  };
  const size_t center = peak;
  const float left = magnitude((center + fft_bins - 1) % fft_bins);
  const float middle = magnitude(center);
  const float right = magnitude((center + 1) % fft_bins);
  const float denominator = left - 2.0f * middle + right;
  const float fraction = fabsf(denominator) < 1.0e-6f
                             ? 0.0f
                             : std::clamp(0.5f * (left - right) / denominator, -0.5f, 0.5f);
  float signed_peak = static_cast<float>(peak) + fraction;
  if (signed_peak + 1.0f > static_cast<float>(fft_bins) * 0.5f) signed_peak -= fft_bins;
  return signed_peak * bandwidth_hz / fft_bins;
}

bool align_sync(size_t* sync, uint16_t peak, size_t fft_bins) {
  if (sync == nullptr) return false;
  const int32_t signed_peak = peak + 1 > fft_bins / 2
                                  ? static_cast<int32_t>(peak) - static_cast<int32_t>(fft_bins)
                                  : static_cast<int32_t>(peak);
  const int32_t shift = static_cast<int32_t>(lroundf(
      static_cast<float>(signed_peak) / static_cast<float>(kFftPadding)));
  if (shift < 0 && *sync < static_cast<size_t>(-shift)) return false;
  *sync = static_cast<size_t>(static_cast<int64_t>(*sync) + shift);
  return true;
}

size_t symbol_count(uint8_t payload_len, uint8_t coding_rate, bool has_crc, uint8_t sf) {
  int total = 2 * payload_len - sf + 7 + (has_crc ? 4 : 0);
  const int groups = total <= 0 ? 0 : (total + sf - 1) / sf;
  return 8u + static_cast<size_t>(groups) * (coding_rate + 4u);
}

bool decode_symbols(const uint16_t* raw_symbols, size_t raw_count, uint8_t sf, uint8_t* data,
                    size_t* data_size, bool* crc_ok) {
  if (raw_symbols == nullptr || data == nullptr || data_size == nullptr || crc_ok == nullptr ||
      raw_count < 8) return false;
  uint16_t gray[300]{};
  if (raw_count > std::size(gray)) return false;
  for (size_t i = 0; i < raw_count; ++i) gray[i] = gray_code(raw_symbols[i], i, sf);
  uint8_t nibbles[kMaxNibbles]{};
  uint16_t codewords[16]{};
  uint8_t payload_len = 0;
  uint8_t coding_rate = 0;
  bool has_crc = false;
  size_t nibble_count = 0;
  if (!parse_header(gray, sf, &payload_len, &coding_rate, &has_crc, nibbles, &nibble_count))
    return false;
  const uint8_t redundant_bits = static_cast<uint8_t>(coding_rate + 4);
  for (size_t start = 8; start + redundant_bits <= raw_count; start += redundant_bits) {
    const size_t count = deinterleave(gray + start, redundant_bits, sf, codewords,
                                      std::size(codewords));
    if (count == 0 || nibble_count + count > std::size(nibbles)) return false;
    for (size_t i = 0; i < count; ++i) nibbles[nibble_count++] = hamming_decode(codewords[i], redundant_bits);
  }
  if ((nibble_count & 1u) != 0) ++nibble_count;
  const size_t byte_count = nibble_count / 2;
  if (byte_count < static_cast<size_t>(payload_len) + (has_crc ? 2u : 0u) || byte_count > kMaxBytes)
    return false;
  for (size_t i = 0; i < byte_count; ++i)
    data[i] = static_cast<uint8_t>(nibbles[i * 2] | (nibbles[i * 2 + 1] << 4));
  dewhiten(data, has_crc ? payload_len : byte_count);
  *data_size = payload_len;
  *crc_ok = !has_crc || phy_crc_matches(data, payload_len, data + payload_len);
  return true;
}

bool decode_mesh(const uint8_t* data, size_t size, const Config& config, Packet* packet) {
  if (data == nullptr || packet == nullptr || size <= 16) return false;
  packet->destination = read_le32(data);
  packet->sender = read_le32(data + 4);
  packet->packet_id = read_le32(data + 8);
  const uint8_t* encrypted = data + 16;
  const size_t encrypted_size = size - 16;
  uint8_t plain[kMaxBytes]{};
  const uint8_t* payload = nullptr;
  size_t payload_size = 0;
  uint16_t port = 0;
  if (parse_data(encrypted, encrypted_size, &port, &payload, &payload_size)) {
    packet->encrypted = false;
  } else {
    const uint8_t* keys[] = {config.authorized_psk, kDefaultPsk};
    const size_t key_sizes[] = {config.authorized_psk_bytes, sizeof(kDefaultPsk)};
    bool decoded = false;
    for (size_t key = 0; key < std::size(keys); ++key) {
      if (keys[key] == nullptr || (key_sizes[key] != 16 && key_sizes[key] != 32) ||
          !decrypt_ctr(encrypted, encrypted_size, keys[key], key_sizes[key], packet->sender,
                       packet->packet_id, plain) ||
          !parse_data(plain, encrypted_size, &port, &payload, &payload_size)) continue;
      decoded = true;
      packet->encrypted = true;
      break;
    }
    if (!decoded) {
      packet->encrypted = true;
      copy_literal(packet->text, sizeof(packet->text), "ENCRYPTED FRAME");
      return true;
    }
  }
  packet->port = port;
  summarize_packet(port, payload, payload_size, packet);
  return true;
}

}  // namespace

bool initialize() {
  if (g_scratch.fft != nullptr && g_scratch.downchirp != nullptr)
    return rf_analysis::initialize_fft();
  g_scratch.fft = static_cast<float*>(heap_caps_malloc(sizeof(float) * kMaxFft * 2,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_scratch.downchirp = static_cast<float*>(heap_caps_malloc(sizeof(float) * kMaxFft * 2,
                                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_scratch.fft == nullptr || g_scratch.downchirp == nullptr) return false;
  return rf_analysis::initialize_fft();
}

static size_t decode_capture_pass(const uint8_t* cu8, size_t bytes, uint32_t sample_rate_sps,
                                  uint8_t spreading_factor, uint32_t bandwidth_hz,
                                  uint32_t frequency_hz, const Config& config, Packet* packets,
                                  size_t packet_capacity, Stats* stats, float phase_cfo_hz) {
  if (stats != nullptr) {
    ++stats->captures;
    stats->decode_millis = 0;
  }
  if (cu8 == nullptr || packets == nullptr || packet_capacity == 0 || (bytes & 1u) != 0 ||
      sample_rate_sps < kDecodeRate || !initialize() ||
      !configure_chirp(spreading_factor, bandwidth_hz)) return 0;
  const uint32_t started = now_millis();
  const uint8_t* decode_cu8 = cu8;
  size_t samples = bytes / 2;
  uint32_t decode_rate = sample_rate_sps;
  const uint8_t* filtered_cu8 = nullptr;
  size_t filtered_samples = 0;
  if (!filter_capture(cu8, samples, sample_rate_sps, &filtered_cu8, &filtered_samples)) return 0;
  const uint8_t* resampled_cu8 = nullptr;
  size_t resampled_samples = 0;
  if (!linear_resample_capture(filtered_cu8, filtered_samples, sample_rate_sps, &resampled_cu8,
                               &resampled_samples)) return 0;
  decode_cu8 = resampled_cu8;
  samples = resampled_samples;
  decode_rate = kDecodeRate;
  // Dechirping linearly selects the 500 kS/s CSS grid from the original source.
  const size_t n = g_scratch.symbol_samples;
  const size_t fft_bins = g_scratch.fft_size / 2;
  const size_t bins = 1u << spreading_factor;
  const uint64_t virtual_samples = static_cast<uint64_t>(samples) * kDecodeRate / decode_rate;
  const size_t preamble = 16;
  size_t start = 0;
  size_t found = 0;
  uint32_t work_units = 0;
  while (start + n * (preamble + 8) < virtual_samples && found < packet_capacity) {
    uint16_t previous_peak = 0;
    uint8_t matching = 0;
    bool preamble_found = false;
    size_t cursor = start;
    while (cursor + n * preamble < virtual_samples) {
      uint16_t peak = 0;
      float height = 0;
      if (!dechirp_peak(decode_cu8, samples, decode_rate, cursor, true, &peak, &height,
                        phase_cfo_hz)) break;
      const uint16_t delta = peak > previous_peak ? peak - previous_peak : previous_peak - peak;
      if (matching > 0 && std::min<size_t>(delta, fft_bins - delta) <= kFftPadding) {
        ++matching;
      } else {
        matching = 1;
      }
      previous_peak = peak;
      if (matching >= preamble - 1) {
        // Match the host detector: the completed 15-symbol run advances to
        // the following symbol before its coarse timing correction.
        cursor += n;
        const size_t coarse = static_cast<size_t>(lroundf(
            static_cast<float>(peak) * 2.0f / static_cast<float>(kFftPadding)));
        cursor -= std::min(cursor, coarse);
        preamble_found = true;
        break;
      }
      cursor += n;
      if ((++work_units & 0x0fu) == 0) vTaskDelay(1);
    }
    if (!preamble_found) break;
    if (stats != nullptr) ++stats->preambles;
    size_t sync = cursor;
    float up_height = 0;
    float down_height = 0;
    uint16_t unused_peak = 0;
    while (sync + n < virtual_samples) {
      if (!dechirp_peak(decode_cu8, samples, decode_rate, sync, true, &unused_peak, &up_height,
                        phase_cfo_hz) ||
          !dechirp_peak(decode_cu8, samples, decode_rate, sync, false, &unused_peak, &down_height,
                        phase_cfo_hz)) break;
      sync += n;
      if (down_height > up_height) break;
    }
    uint16_t down_peak = 0;
    if (sync + n >= virtual_samples ||
        !dechirp_peak(decode_cu8, samples, decode_rate, sync, false, &down_peak, &down_height,
                      phase_cfo_hz)) {
      start = cursor + n;
      continue;
    }
    if (!align_sync(&sync, down_peak, fft_bins) || sync < 4 * n || sync >= virtual_samples) {
      start = cursor + n;
      continue;
    }
    uint16_t preamble_peak = 0;
    float ignored = 0;
    if (!dechirp_peak(decode_cu8, samples, decode_rate, sync - 4 * n, true, &preamble_peak, &ignored,
                      phase_cfo_hz)) {
      start = cursor + n;
      continue;
    }
    float correction_cfo_hz = refined_cfo_hz(preamble_peak, fft_bins, bandwidth_hz);
    float last_up = 0;
    float last_down = 0;
    if (!dechirp_peak(decode_cu8, samples, decode_rate, sync - n, true, &unused_peak, &last_up,
                      phase_cfo_hz) ||
        !dechirp_peak(decode_cu8, samples, decode_rate, sync - n, false, &unused_peak, &last_down,
                      phase_cfo_hz)) {
      start = cursor + n;
      continue;
    }
    const size_t nominal_data_start =
        sync + static_cast<size_t>((last_up > last_down ? 2.25f : 1.25f) * n);
    uint16_t symbols[300]{};
    uint8_t payload_len = 0;
    uint8_t coding_rate = 0;
    bool has_crc = false;
    size_t data_start = 0;
    bool header_found = false;
    for (int radius = 0; radius <= kTimingSearchSamples && !header_found; ++radius) {
      const int directions = radius == 0 ? 1 : 2;
      for (int direction = 0; direction < directions && !header_found; ++direction) {
      const int adjustment = direction == 0 ? radius : -radius;
      if ((adjustment < 0 && nominal_data_start < static_cast<size_t>(-adjustment)) ||
          (adjustment > 0 && nominal_data_start + static_cast<size_t>(adjustment) >= virtual_samples))
        continue;
      const size_t candidate = static_cast<size_t>(static_cast<int64_t>(nominal_data_start) + adjustment);
      if (candidate + 8 * n >= virtual_samples) continue;
      bool candidate_ok = true;
      for (size_t i = 0; i < 8; ++i) {
        uint16_t peak = 0;
        if (!dechirp_peak(decode_cu8, samples, decode_rate, candidate + i * n, true, &peak, &ignored,
                          phase_cfo_hz)) {
          candidate_ok = false;
          break;
        }
        symbols[i] = peak_to_symbol(peak, preamble_peak, fft_bins, bins);
      }
      if (candidate_ok && parse_raw_header(symbols, spreading_factor, &payload_len, &coding_rate,
                                           &has_crc)) {
        data_start = candidate;
        header_found = true;
      }
      vTaskDelay(1);
      }
    }
    if (!header_found) {
      if (stats != nullptr) ++stats->header_failures;
      start = cursor + n;
      continue;
    }
    if (stats != nullptr) {
      stats->raw_cfo_tenths_hz =
          static_cast<int16_t>(lroundf((phase_cfo_hz + correction_cfo_hz) * 10.0f));
      stats->cfo_tenths_hz = static_cast<int16_t>(lroundf(correction_cfo_hz * 10.0f));
    }
    const size_t count = symbol_count(payload_len, coding_rate, has_crc, spreading_factor);
    if (count > std::size(symbols) || data_start + count * n > virtual_samples) {
      start = cursor + n;
      continue;
    }
    uint16_t header_symbols[8]{};
    std::memcpy(header_symbols, symbols, sizeof(header_symbols));
    bool crc_ok = false;
    for (int phase_retry = 0; phase_retry < 1 && !crc_ok; ++phase_retry) {
      const float payload_phase_cfo_hz = phase_cfo_hz + (phase_retry ? correction_cfo_hz : 0.0f);
      uint16_t payload_preamble_peak = preamble_peak;
      float payload_drift_cfo_hz = correction_cfo_hz;
      if (phase_retry &&
          (!dechirp_peak(decode_cu8, samples, decode_rate, sync - 4 * n, true,
                         &payload_preamble_peak, &ignored, payload_phase_cfo_hz))) {
        continue;
      }
      if (phase_retry) {
        payload_drift_cfo_hz = refined_cfo_hz(payload_preamble_peak, fft_bins, bandwidth_hz);
      }
      for (float payload_clock_skew : kPayloadClockSkews) {
      if (crc_ok) break;
      std::memcpy(symbols, header_symbols, sizeof(header_symbols));
      bool symbols_ok = true;
      for (size_t i = 8; i < count; ++i) {
        const int64_t symbol_start = static_cast<int64_t>(data_start + i * n) +
            static_cast<int64_t>(lroundf((static_cast<float>(i) - 8.0f) * payload_clock_skew));
        uint16_t peak = 0;
        if (symbol_start < 0 ||
            !dechirp_peak(decode_cu8, samples, decode_rate, static_cast<size_t>(symbol_start), true,
                          &peak, &ignored, payload_phase_cfo_hz)) {
          symbols_ok = false;
          break;
        }
        symbols[i] = peak_to_symbol(peak, payload_preamble_peak, fft_bins, bins);
      }
      if (symbols_ok && frequency_hz != 0) {
        const float drift_per_symbol =
            static_cast<float>(1u << spreading_factor) * payload_drift_cfo_hz / frequency_hz;
        for (size_t i = 0; i < count; ++i) {
          int32_t corrected = static_cast<int32_t>(symbols[i]) -
                              static_cast<int32_t>(lroundf((i + 2u) * drift_per_symbol));
          corrected %= static_cast<int32_t>(bins);
          symbols[i] = static_cast<uint16_t>(corrected < 0 ? corrected + bins : corrected);
        }
      }
      uint8_t decoded[kMaxBytes]{};
      size_t decoded_size = 0;
      if (symbols_ok && decode_symbols(symbols, count, spreading_factor, decoded, &decoded_size, &crc_ok) &&
          crc_ok) {
        if (stats != nullptr) ++stats->crc_ok;
        Packet packet{};
        if (decode_mesh(decoded, decoded_size, config, &packet)) {
          packets[found++] = packet;
          if (stats != nullptr && packet.encrypted) ++stats->encrypted;
        }
      }
      }
    }
    if (!crc_ok && stats != nullptr) ++stats->crc_failures;
    start = data_start + count * n;
  }
  if (stats != nullptr) {
    stats->ready = true;
    stats->decode_millis = now_millis() - started;
  }
  return found;
}

size_t decode_capture(const uint8_t* cu8, size_t bytes, uint32_t sample_rate_sps,
                      uint8_t spreading_factor, uint32_t bandwidth_hz, uint32_t frequency_hz,
                      const Config& config, Packet* packets, size_t packet_capacity, Stats* stats) {
  Stats raw{};
  Stats* first = stats != nullptr ? stats : &raw;
  const size_t found = decode_capture_pass(cu8, bytes, sample_rate_sps, spreading_factor,
                                           bandwidth_hz, frequency_hz, config, packets,
                                           packet_capacity, first, 0.0f);
  if (found != 0 || first->crc_ok != 0 || fabsf(first->raw_cfo_tenths_hz) <= 5) return found;
  Stats corrected{};
  const size_t retried = decode_capture_pass(cu8, bytes, sample_rate_sps, spreading_factor,
                                             bandwidth_hz, frequency_hz, config, packets,
                                             packet_capacity, &corrected,
                                             first->raw_cfo_tenths_hz / 10.0f);
  if (stats != nullptr) *stats = corrected;
  return retried;
}

bool self_check() {
  const uint8_t encrypted[] = {
      0x6a, 0x92, 0x18, 0x55, 0xb8, 0x20, 0x6a, 0x22, 0xd1, 0x09, 0x1b, 0xd4,
      0x70, 0x27, 0xbb, 0xef, 0xdd, 0x21, 0x0f, 0x46, 0x25, 0x82, 0xf3, 0x51,
      0xbd, 0xf0, 0xcc, 0x66, 0x48, 0x6d, 0x8c,
  };
  uint8_t plain[sizeof(encrypted)]{};
  if (!decrypt_ctr(encrypted, sizeof(encrypted), kDefaultPsk, sizeof(kDefaultPsk),
                   0xA1B2C3D4, 0x10203040, plain) ||
      std::memcmp(plain, "\x08\x01\x12\x1bOrcSDR Meshtastic self-test", sizeof(plain)) != 0)
    return false;
  const uint8_t telemetry[] = {0x12, 0x07, 0x08, 0x51, 0x15, 0x0a, 0xd7, 0x83, 0x40};
  char summary[48]{};
  summarize_telemetry(telemetry, sizeof(telemetry), summary, sizeof(summary));
  return std::strstr(summary, "81%") != nullptr && std::strstr(summary, "4.12V") != nullptr;
}

}  // namespace orcsdr::lora_native
