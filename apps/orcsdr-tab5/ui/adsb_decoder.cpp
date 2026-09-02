#include "adsb_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orcsdr::adsb_rx {
namespace {

constexpr uint32_t kCrcPolynomial = 0xfff409u;

uint32_t bits(const uint8_t* frame, int first, int length) {
  uint32_t value = 0;
  for (int i = 0; i < length; ++i)
    value = (value << 1) | ((frame[(first + i) / 8] >> (7 - (first + i) % 8)) & 1u);
  return value;
}

uint32_t crc_payload(const uint8_t* frame, int data_bits) {
  uint32_t crc = 0;
  for (int i = 0; i < data_bits; ++i) {
    const bool feedback = ((crc >> 23) & 1u) != ((frame[i / 8] >> (7 - i % 8)) & 1u);
    crc = (crc << 1) & 0xffffffu;
    if (feedback) crc ^= kCrcPolynomial;
  }
  return crc;
}

uint32_t interpolate(const uint16_t* samples, size_t milli_index) {
  const size_t index = milli_index / 1000u;
  const uint32_t fraction = static_cast<uint32_t>(milli_index % 1000u);
  return static_cast<uint32_t>(samples[index]) * (1000u - fraction) +
         static_cast<uint32_t>(samples[index + 1]) * fraction;
}

bool parse(const uint8_t* bytes, uint8_t bit_length, uint16_t signal, Frame* out) {
  const uint8_t df = bytes[0] >> 3;
  const int data_bits = bit_length - 24;
  const uint32_t syndrome = crc_payload(bytes, data_bits) ^ bits(bytes, data_bits, 24);
  if (!((df == 17 && bit_length == 112 && syndrome == 0) ||
        (df == 11 && bit_length == 56 && syndrome <= 0x7f)))
    return false;
  std::memcpy(out->bytes, bytes, bit_length / 8);
  out->bit_length = bit_length;
  out->icao = bits(bytes, 8, 24);
  if (df != 17) {
    out->signal = signal;
    return true;
  }
  out->type_code = static_cast<uint8_t>(bits(bytes, 32, 5));
  out->signal = signal;

  if (out->type_code >= 1 && out->type_code <= 4) {
    size_t length = 8;
    for (size_t i = 0; i < 8; ++i) {
      const uint8_t code = static_cast<uint8_t>(bits(bytes, 40 + static_cast<int>(i) * 6, 6));
      out->callsign[i] = code >= 1 && code <= 26 ? static_cast<char>('A' + code - 1)
                         : code >= 48 && code <= 57 ? static_cast<char>('0' + code - 48)
                                                    : ' ';
    }
    while (length && out->callsign[length - 1] == ' ') --length;
    out->callsign[length] = '\0';
    out->has_callsign = length != 0;
  } else if (out->type_code >= 9 && out->type_code <= 18) {
    const uint16_t encoded = static_cast<uint16_t>(bits(bytes, 40, 12));
    if ((encoded & 0x10u) != 0) {
      const int n = ((encoded & 0xfe0u) >> 1) | (encoded & 0x0fu);
      out->altitude_ft = n * 25 - 1000;
      out->has_altitude = true;
    }
    out->cpr_odd = bits(bytes, 53, 1) != 0;
    out->cpr_latitude = bits(bytes, 54, 17);
    out->cpr_longitude = bits(bytes, 71, 17);
    out->has_cpr = true;
  } else if (out->type_code == 19) {
    const uint8_t subtype = static_cast<uint8_t>(bits(bytes, 37, 3));
    const int scale = (subtype == 2 || subtype == 4) ? 4 : 1;
    if (subtype == 1 || subtype == 2) {
      const int ew_raw = static_cast<int>(bits(bytes, 46, 10));
      const int ns_raw = static_cast<int>(bits(bytes, 57, 10));
      if (ew_raw && ns_raw) {
        const int ew = (bits(bytes, 45, 1) ? -1 : 1) * (ew_raw - 1) * scale;
        const int ns = (bits(bytes, 56, 1) ? -1 : 1) * (ns_raw - 1) * scale;
        out->speed_kts = static_cast<int>(std::lround(std::sqrt(ew * ew + ns * ns)));
        out->heading_deg = static_cast<int>(std::lround(
            std::fmod(std::atan2(static_cast<double>(ew), static_cast<double>(ns)) *
                              180.0 / 3.14159265358979323846 + 360.0,
                      360.0)));
        out->has_speed = out->has_heading = true;
      }
    }
    const int vr = static_cast<int>(bits(bytes, 69, 9));
    if (vr) {
      out->vertical_rate_fpm = (bits(bytes, 68, 1) ? -1 : 1) * (vr - 1) * 64;
      out->has_vertical_rate = true;
    }
  }
  return true;
}

}  // namespace

bool decode_global_cpr(const Frame& first, const Frame& second, bool use_odd,
                       double* latitude, double* longitude) {
  if (!latitude || !longitude || !first.has_cpr || !second.has_cpr ||
      first.cpr_odd == second.cpr_odd || first.icao != second.icao)
    return false;
  const Frame& even = first.cpr_odd ? second : first;
  const Frame& odd = first.cpr_odd ? first : second;
  const double yz_even = even.cpr_latitude / 131072.0;
  const double yz_odd = odd.cpr_latitude / 131072.0;
  const int j = static_cast<int>(std::floor(59.0 * yz_even - 60.0 * yz_odd + 0.5));
  auto positive_mod = [](int value, int divisor) {
    const int result = value % divisor;
    return result < 0 ? result + divisor : result;
  };
  double lat_even = 6.0 * (positive_mod(j, 60) + yz_even);
  double lat_odd = (360.0 / 59.0) * (positive_mod(j, 59) + yz_odd);
  if (lat_even >= 270.0) lat_even -= 360.0;
  if (lat_odd >= 270.0) lat_odd -= 360.0;
  auto longitude_zones = [](double latitude_value) {
    constexpr double kPi = 3.14159265358979323846;
    const double latitude_radians = std::abs(latitude_value) * kPi / 180.0;
    if (latitude_radians >= 87.0 * kPi / 180.0) return 1;
    const double numerator = 1.0 - std::cos(kPi / 30.0);
    const double denominator = std::cos(latitude_radians) * std::cos(latitude_radians);
    return static_cast<int>(std::floor(2.0 * kPi / std::acos(1.0 - numerator / denominator)));
  };
  const int nl_even = longitude_zones(lat_even);
  const int nl_odd = longitude_zones(lat_odd);
  if (nl_even != nl_odd) return false;
  const double xz_even = even.cpr_longitude / 131072.0;
  const double xz_odd = odd.cpr_longitude / 131072.0;
  const int m = static_cast<int>(
      std::floor(xz_even * (nl_even - 1) - xz_odd * nl_even + 0.5));
  const int ni = std::max(nl_even - (use_odd ? 1 : 0), 1);
  double lon = (360.0 / ni) *
               (positive_mod(m, ni) + (use_odd ? xz_odd : xz_even));
  if (lon > 180.0) lon -= 360.0;
  *latitude = use_odd ? lat_odd : lat_even;
  *longitude = lon;
  return true;
}

void Decoder::reset() {
  overlap_ = 0;
  stats_ = {};
}

void Decoder::process_cu8(const uint8_t* data, size_t bytes, FrameCallback callback,
                          void* context) {
  if (!data) return;
  bytes &= ~size_t{1};
  size_t count = overlap_;
  for (size_t i = 0; i < bytes; i += 2) {
    const int iv = static_cast<int>(data[i]) - 127;
    const int qv = static_cast<int>(data[i + 1]) - 127;
    if (count < kMagnitudeCapacity) {
      const uint16_t magnitude = static_cast<uint16_t>(std::abs(iv) + std::abs(qv));
      stats_.magnitude_min = std::min(stats_.magnitude_min, magnitude);
      stats_.magnitude_max = std::max(stats_.magnitude_max, magnitude);
      magnitudes_[count++] = magnitude;
    }
  }
  constexpr size_t kFrameSamples = 246;
  // Bit interpolation for the last data bit reads one sample past the
  // kFrameSamples preamble window (see interpolate()), so require that
  // extra sample to be part of this call's freshly-filled data.
  for (size_t pos = 0; pos + kFrameSamples + 1 <= count; ++pos) {
    const uint16_t* sample = magnitudes_ + pos;
    if (sample[0] <= sample[1] || sample[2] <= sample[1] || sample[2] <= sample[3] ||
        sample[7] <= sample[6] || sample[7] <= sample[8] || sample[9] <= sample[8] ||
        sample[9] <= sample[10])
      continue;
    const uint16_t pulse = static_cast<uint16_t>(
        (sample[0] + sample[2] + sample[7] + sample[9]) / 4u);
    const uint16_t quiet = static_cast<uint16_t>(
        (sample[11] + sample[12] + sample[13] + sample[14] + sample[15]) / 5u);
    if (pulse <= static_cast<uint32_t>(quiet) * 2u + 8u) continue;
    ++stats_.preambles;

    ++stats_.frames;
    Frame decoded{};
    bool valid = false;
    bool saw_df17 = false;
    uint8_t valid_bits = 0;
    // Preamble peak alignment constrains the data-center phase to this narrow
    // range. Interpolation avoids the five hard-decision errors measured in a
    // live 2.048 MS/s frame without weakening CRC acceptance.
    for (size_t first_center = 15946; first_center <= 16846; first_center += 100) {
      uint8_t frame[14]{};
      for (int bit = 0; bit < 112; ++bit) {
        const size_t center = first_center + static_cast<size_t>(bit) * 2048u;
        if (interpolate(sample, center) > interpolate(sample, center + 1024u))
          frame[bit / 8] |= static_cast<uint8_t>(0x80u >> (bit % 8));
      }
      const uint8_t df = frame[0] >> 3;
      saw_df17 |= df == 17;
      const uint8_t frame_bits = df == 11 ? 56 : 112;
      if (parse(frame, frame_bits, pulse - quiet, &decoded)) {
        valid = true;
        valid_bits = frame_bits;
        break;
      }
    }
    if (saw_df17) ++stats_.df17;
    if (!valid) continue;
    ++stats_.crc_ok;
    if (callback) callback(decoded, context);
    pos += (valid_bits == 56 ? 131 : kFrameSamples) - 1;
  }
  // Retain kFrameSamples samples (not kFrameSamples - 1) so the extra
  // lookahead sample the interpolator needs is available on the next call.
  overlap_ = std::min(count, kFrameSamples);
  std::memmove(magnitudes_, magnitudes_ + count - overlap_, overlap_ * sizeof(uint16_t));
}

bool Decoder::self_check() {
  const uint8_t identity[] = {0x8d, 0x48, 0x40, 0xd6, 0x20, 0x2c, 0xc3,
                              0x71, 0xc3, 0x2c, 0xe0, 0x57, 0x60, 0x98};
  const uint8_t position[] = {0x8d, 0x40, 0x62, 0x1d, 0x58, 0xc3, 0x82,
                              0xd6, 0x90, 0xc8, 0xac, 0x28, 0x63, 0xa7};
  const uint8_t odd_position[] = {0x8d, 0x40, 0x62, 0x1d, 0x58, 0xc3, 0x86,
                                  0x43, 0x5c, 0xc4, 0x12, 0x69, 0x2a, 0xd6};
  const uint8_t velocity[] = {0x8d, 0x45, 0x1d, 0xbd, 0x99, 0x05, 0xb5,
                              0x01, 0x80, 0x04, 0x00, 0x59, 0x79, 0xc5};
  // Live OrcSDR RF capture, 2026-08-11: ASA1310 / ICAO A29551 at 35,000 ft.
  const uint8_t live_position[] = {0x8d, 0xa2, 0x95, 0x51, 0x58, 0xb5, 0x05,
                                   0x03, 0x6b, 0xfb, 0x54, 0xbc, 0x90, 0xac};
  const uint8_t live_magnitudes[] = {
      111, 32,  98,  9,   15,  14,  22,  101, 24,  97,  37,  9,   12,  10,  5,
      17,  114, 42,  33,  114, 62,  73,  61,  50,  148, 31,  82,  95,  21,  60,
      125, 60,  51,  70,  29,  43,  115, 69,  25,  44,  83,  27,  110, 27,  140,
      105, 14,  18,  125, 123, 8,   18,  112, 38,  106, 145, 37,  16,  96,  127,
      52,  9,   71,  109, 38,  9,   60,  129, 54,  2,   52,  139, 59,  19,  57,
      61,  51,  103, 18,  160, 88,  10,  28,  144, 104, 19,  48,  119, 91,  30,
      82,  13,  18,  82,  20,  82,  40,  92,  111, 16,  6,   81,  138, 41,  78,
      51,  27,  72,  138, 47,  37,  80,  146, 51,  19,  84,  71,  47,  78,  28,
      75,  43,  72,  14,  106, 78,  4,   26,  114, 106, 11,  38,  101, 22,  102,
      24,  92,  26,  104, 34,  88,  46,  102, 154, 40,  91,  50,  15,  87,  138,
      33,  58,  52,  29,  64,  104, 53,  3,   56,  135, 63,  62,  76,  31,  75,
      34,  97,  39,  102, 27,  102, 16,  113, 8,   12,  120, 107, 30,  84,  22,
      9,   94,  99,  18,  11,  102, 99,  48,  19,  101, 129, 35,  24,  86,  56,
      53,  163, 68,  28,  39,  152, 52,  46,  80,  40,  87,  39,  98,  18,  35,
      73,  21,  110, 80,  9,   37,  96,  24,  94,  105, 2,   27,  111, 19,  80,
      19,  102, 45,  83,  134, 23,  6,   102, 128, 37,  14,  79,  123, 46,  67,
      69,  24,  77,  54,  34,  73};
  uint8_t all_call[] = {0x5d, 0x48, 0x40, 0xd6, 0, 0, 0};
  const uint32_t all_call_crc = crc_payload(all_call, 32);
  all_call[4] = static_cast<uint8_t>(all_call_crc >> 16);
  all_call[5] = static_cast<uint8_t>(all_call_crc >> 8);
  all_call[6] = static_cast<uint8_t>(all_call_crc);
  Frame a{}, b{}, c{}, odd{}, live{}, short_reply{};
  double latitude = 0.0, longitude = 0.0;
  if (!(parse(identity, 112, 1, &a) && a.icao == 0x4840d6 && a.has_callsign &&
         std::strcmp(a.callsign, "KLM1023") == 0 && parse(position, 112, 1, &b) &&
         b.has_altitude && b.altitude_ft == 38000 && parse(velocity, 112, 1, &c) &&
         c.icao == 0x451dbd && c.has_speed && c.speed_kts == 436 &&
         c.has_heading && c.heading_deg == 271 && parse(odd_position, 112, 1, &odd) &&
         decode_global_cpr(b, odd, true, &latitude, &longitude) &&
         std::abs(latitude - 52.2658) < 0.001 && std::abs(longitude - 3.9389) < 0.001 &&
         parse(live_position, 112, 1, &live) &&
         live.icao == 0xa29551 && live.has_altitude && live.altitude_ft == 35000 &&
         parse(all_call, 56, 1, &short_reply) && short_reply.bit_length == 56 &&
         short_reply.icao == 0x4840d6))
    return false;

  // process_cu8's frame-search loop needs kFrameSamples + 1 (247) samples
  // to run a single pass; 246 would leave the buffer one sample short.
  uint8_t cu8[247 * 2]{};
  for (size_t i = 0; i < sizeof(cu8); i += 2) cu8[i] = cu8[i + 1] = 127;
  for (const size_t index : {size_t{0}, size_t{2}, size_t{7}, size_t{9}})
    cu8[index * 2] = 220;
  for (int bit = 0; bit < 112; ++bit) {
    const bool one = (identity[bit / 8] & (0x80u >> (bit % 8))) != 0;
    const size_t first = (16246u + static_cast<size_t>(bit) * 2048u + 500u) / 1000u;
    const size_t second =
        (16246u + static_cast<size_t>(bit) * 2048u + 1024u + 500u) / 1000u;
    cu8[(one ? first : second) * 2] = 220;
  }
  Decoder* decoder = new Decoder;
  if (!decoder) return false;
  Frame replay{};
  decoder->process_cu8(
      cu8, sizeof(cu8),
      [](const Frame& frame, void* context) { *static_cast<Frame*>(context) = frame; },
      &replay);
  bool ok = decoder->stats().crc_ok == 1 && replay.icao == 0x4840d6;
  if (ok) {
    for (size_t i = 0; i < sizeof(live_magnitudes); ++i) {
      const uint8_t i_part = std::min<uint8_t>(live_magnitudes[i], 127);
      cu8[i * 2] = static_cast<uint8_t>(127 + i_part);
      cu8[i * 2 + 1] = static_cast<uint8_t>(127 + live_magnitudes[i] - i_part);
    }
    decoder->reset();
    replay = {};
    decoder->process_cu8(
        cu8, sizeof(cu8),
        [](const Frame& frame, void* context) { *static_cast<Frame*>(context) = frame; },
        &replay);
    ok = decoder->stats().crc_ok == 1 && replay.icao == 0xa29551 &&
         replay.has_altitude && replay.altitude_ft == 35000;
  }
  if (ok) {
    std::fill(cu8, cu8 + sizeof(cu8), uint8_t{127});
    for (const size_t index : {size_t{0}, size_t{2}, size_t{7}, size_t{9}})
      cu8[index * 2] = 220;
    for (int bit = 0; bit < 56; ++bit) {
      const bool one = (all_call[bit / 8] & (0x80u >> (bit % 8))) != 0;
      const size_t first = (16246u + static_cast<size_t>(bit) * 2048u + 500u) / 1000u;
      const size_t second =
          (16246u + static_cast<size_t>(bit) * 2048u + 1024u + 500u) / 1000u;
      cu8[(one ? first : second) * 2] = 220;
    }
    decoder->reset();
    replay = {};
    decoder->process_cu8(
        cu8, sizeof(cu8),
        [](const Frame& frame, void* context) { *static_cast<Frame*>(context) = frame; },
        &replay);
    ok = decoder->stats().crc_ok == 1 && replay.bit_length == 56 &&
         replay.icao == 0x4840d6;
  }
  delete decoder;
  return ok;
}

}  // namespace orcsdr::adsb_rx
