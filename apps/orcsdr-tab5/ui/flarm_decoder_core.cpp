/*
 * Receive-only AIR V6/V7 packet decoding adapted from SoftRF Legacy.cpp/.h.
 * Copyright (C) 2014-2015 Stanislaw Pusep
 * Copyright (C) 2016-2026 Linar Yusupov
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Reference revision: a4c21fc45b251ab2e3b3a543ab69771e63328ef6
 * See docs/FLARM.md for provenance. SDR front end is specific to OrcSDR.
 */
#include "flarm_decoder_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orcsdr::flarm_rx {
namespace {
constexpr double kPi = 3.14159265358979323846;
uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void write32(uint8_t* p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
uint32_t bits(const uint8_t* p, unsigned start, unsigned count) {
  uint32_t value = 0;
  for (unsigned i = 0; i < count; ++i)
    value |= uint32_t((p[(start + i) / 8] >> ((start + i) % 8)) & 1) << i;
  return value;
}
int32_t signed_bits(uint32_t value, unsigned width) {
  return int32_t(value) - ((value & (1u << (width - 1))) ? int32_t(1u << width) : 0);
}
void decrypt(uint32_t* v, unsigned n, const uint32_t key[4]) {
  uint32_t sum = 6u * 0x9e3779b9u, y = v[0];
  for (unsigned round = 0; round < 6; ++round) {
    const unsigned e = (sum >> 2) & 3;
    for (unsigned p = n - 1; ; --p) {
      const uint32_t z = v[p ? p - 1 : n - 1];
      const uint32_t mix = (((z >> 5 ^ y << 2) + (y >> 3 ^ z << 4)) ^
                            ((sum ^ y) + (key[(p & 3) ^ e] ^ z)));
      y = v[p] -= mix;
      if (!p) break;
    }
    sum -= 0x9e3779b9u;
  }
}
void v6_key(uint32_t* key, uint32_t epoch, uint32_t address) {
  constexpr uint32_t table[] = {0xe43276df,0xdca83759,0x9802b8ac,0x4675a56b,
                                0xfc78ea65,0x804b90ea,0xb76542cd,0x329dfa32};
  for (unsigned i = 0; i < 4; ++i) {
    uint32_t x = table[i + ((epoch >> 23 & 1) ? 4 : 0)] ^ (epoch >> 6) ^ ((address << 8) & 0xffffff);
    x = 0x045d9f3bu * (x ^ (x >> 16));
    x = 0x045d9f3bu * (x ^ (x >> 16));
    key[i] = x ^ (x >> 16) ^ 0x87b562f4u;
  }
}
void v7_mask(uint8_t* mask, const uint8_t* packet, uint32_t epoch) {
  std::memcpy(mask, packet, 8);
  write32(mask + 8, epoch >> 4);
  write32(mask + 12, 0x956f6c77);
  uint32_t sum = 0, x = mask[15];
  for (unsigned round = 0; round < 2; ++round) {
    sum += 0x9e3779b9u;
    for (unsigned p = 0; p < 16; ++p) {
      const uint32_t z = x & 255, y = mask[(p + 1) % 16];
      x = mask[p] + ((((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ (sum ^ y));
      mask[p] = uint8_t(x);
    }
  }
}
int descale(uint32_t v, unsigned m, unsigned e, bool is_signed = false) {
  const uint32_t offset = 1u << m, sign = offset << e;
  const bool negative = is_signed && (v & sign);
  v &= sign - 1;
  if (v >= offset) v = ((offset + (v & (offset - 1))) << (v >> m)) - offset;
  return negative ? -int(v) : int(v);
}
// Use wide signed arithmetic: southern latitudes and the dateline must not
// depend on signed shifts, unsigned promotion or 32-bit overflow.
int64_t unwrap(uint32_t encoded, int64_t reference, unsigned width) {
  const int64_t mod = int64_t(1) << width;
  int64_t delta = (int64_t(encoded) - reference) % mod;
  if (delta < -mod / 2) delta += mod;
  if (delta >= mod / 2) delta -= mod;
  return reference + delta;
}
int longitude_divisor(double latitude) {
  constexpr uint16_t table[] = {
    53,53,54,54,55,55,56,56,57,57,58,58,59,59,60,60,61,61,62,62,63,63,64,64,65,65,
    67,68,70,71,73,74,76,77,79,80,82,83,85,86,88,89,91,94,98,101,105,108,112,115,
    119,122,126,129,137,144,152,159,167,174,190,205,221,236,252,267,299,330,362,425,
    489,552,616,679,743,806,806};
  const int lat = std::min(89, int(std::fabs(latitude)));
  return lat < 14 ? 52 : table[lat - 14];
}
bool unpack(const uint8_t* input, uint32_t epoch, const Context& ctx, Frame& f) {
  uint8_t p[24];
  std::memcpy(p, input, sizeof(p));
  uint32_t words[6];
  for (unsigned i = 0; i < 6; ++i) words[i] = read32(p + 4 * i);
  const bool old = (p[3] & 15) == 0;
  f.address = words[0] & 0xffffff;
  f.address_type = (p[3] >> 4) & (old ? 7 : 3);
  f.generation = old ? 6 : 7;
  if (old) {
    uint32_t key[4];
    v6_key(key, epoch, f.address);
    decrypt(words + 1, 5, key);
    for (unsigned i = 1; i < 6; ++i) write32(p + 4 * i, words[i]);
    unsigned parity = 0;
    for (uint8_t b : p) parity ^= unsigned(__builtin_parity(unsigned(b)));
    if (parity || f.address_type > 3) return false;
    f.aircraft_type = uint8_t(bits(p, 60, 4));
    f.stealth = bits(p, 45, 1); f.no_track = bits(p, 46, 1);
    // Floor division matches the V6 arithmetic right shift at negative positions.
    const int64_t rlat = int64_t(std::floor(ctx.latitude_e7 / 128.0));
    const int64_t rlon = int64_t(std::floor(ctx.longitude_e7 / 128.0));
    f.latitude = unwrap(bits(p, 64, 19), rlat, 19) * 128.0 / 1e7;
    f.longitude = unwrap(bits(p, 96, 20), rlon, 20) * 128.0 / 1e7;
    f.altitude_m = float(bits(p, 83, 13));
    const int scale = 1 << bits(p, 126, 2);
    int ns = 0, ew = 0;
    for (unsigned i = 0; i < 4; ++i) {
      ns += signed_bits(p[16 + i], 8); ew += signed_bits(p[20 + i], 8);
    }
    const float north = (ns / 4) * scale / 4.0f, east = (ew / 4) * scale / 4.0f;
    f.speed_mps = std::hypot(north, east);
    f.course_deg = float(std::fmod(std::atan2(east, north) * 180 / kPi + 360, 360));
    f.climb_mps = signed_bits(bits(p, 32, 10), 10) * scale / 10.0f;
  } else {
    constexpr uint32_t key[] = {0xa5f9b21c,0xab3f9d12,0xc6f34e34,0xd72fa378};
    decrypt(words + 2, 4, key);
    uint8_t mask[16];
    v7_mask(mask, input, epoch);
    for (unsigned i = 2; i < 6; ++i) write32(p + 4 * i, words[i] ^ read32(mask + 4 * (i - 2)));
    const unsigned version = bits(p, 56, 4), max_version = bits(p, 60, 4);
    if (version < 1 || version > 3 || max_version < version ||
        bits(p, 48, 6) != 0 || bits(p, 66, 4) != (epoch & 15) ||
        bits(p, 156, 10) >= 720) return false;
    f.stealth = bits(p, 54, 1); f.no_track = bits(p, 55, 1);
    f.aircraft_type = uint8_t(bits(p, 70, 5));
    f.altitude_m = float(descale(bits(p, 75, 13), 12, 1) - 1000);
    f.latitude = unwrap(bits(p, 88, 20), int64_t(ctx.latitude_e7) / 52, 20) * 52.0 / 1e7;
    const int divisor = longitude_divisor(f.latitude);
    f.longitude = unwrap(bits(p, 108, 20), int64_t(ctx.longitude_e7) / divisor, 20) * double(divisor) / 1e7;
    f.speed_mps = descale(bits(p, 137, 10), 8, 2) / 10.0f;
    f.climb_mps = descale(bits(p, 147, 9), 6, 2, true) / 10.0f;
    f.course_deg = bits(p, 156, 10) / 2.0f;
  }
  f.longitude = std::fmod(f.longitude + 540, 360) - 180;
  return f.address != 0 && std::fabs(f.latitude) <= 90 &&
         f.course_deg < 360 && f.speed_mps <= 400 && std::fabs(f.climb_mps) <= 100;
}
uint16_t crc_byte(uint16_t crc, uint8_t b) {
  crc ^= uint16_t(b) << 8;
  for (unsigned i = 0; i < 8; ++i) crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
  return crc;
}
} // namespace

uint16_t packet_crc(const uint8_t* p) {
  uint16_t crc = 0xffff;
  for (uint8_t b : {0x31, 0xfa, 0xb6}) crc = crc_byte(crc, b);
  for (unsigned i = 0; i < 24; ++i) crc = crc_byte(crc, p[i]);
  return crc;
}
Result decode_packet(const uint8_t* p, size_t n, const Context& ctx, Frame* out) {
  if (!p || !out || n != kPacketBytes) return Result::invalid;
  if (packet_crc(p) != (uint16_t(p[24]) << 8 | p[25])) return Result::crc_error;
  if ((p[3] & 15) != 0 && (p[3] & 15) != 2) return Result::unsupported;
  if (ctx.unix_ms < 946684800000ULL || ctx.unix_ms / 1000 > UINT32_MAX - 2) return Result::need_time;
  if (!ctx.location_valid || ctx.latitude_e7 < -900000000 || ctx.latitude_e7 > 900000000 ||
      ctx.longitude_e7 < -1800000000 || ctx.longitude_e7 > 1800000000) return Result::need_location;
  const uint32_t now = uint32_t(ctx.unix_ms / 1000);
  // Try the exact second first; adjacent seconds cover receive latency and key
  // rollover. Never search arbitrary times until random bytes look plausible.
  for (int offset : {0, -1, 1}) {
    Frame candidate;
    if (unpack(p, uint32_t(int64_t(now) + offset), ctx, candidate)) {
      *out = candidate;
      return Result::ok;
    }
  }
  return Result::invalid;
}
const char* aircraft_type_name(uint8_t type) {
  constexpr const char* names[] = {"Unknown", "Glider", "Towplane", "Helicopter", "Parachute",
    "Drop plane", "Hang glider", "Paraglider", "Powered aircraft", "Jet", "Gyrocopter",
    "Balloon", "Airship", "UAV", "Airfield", "Static", "eVTOL", "UAV open", "UAV specific", "UAV certified"};
  return type < sizeof(names) / sizeof(names[0]) ? names[type] : "Unknown";
}

Decoder::Decoder() {
  float sum = 0;
  for (size_t i = 0; i < kTaps; ++i) {
    const double x = double(i) - double(kTaps - 1) / 2;
    const double cutoff = 90000.0 / kSampleRate;
    taps_[i] = float((x == 0 ? 2 * cutoff : std::sin(2 * kPi * cutoff * x) / (kPi * x)) *
                     (0.54 - 0.46 * std::cos(2 * kPi * i / (kTaps - 1))));
    sum += taps_[i];
  }
  for (auto& t : taps_) t /= sum;
  for (size_t i = 0; i < 48; ++i) {
    osc_i_[i] = float(std::cos(2 * kPi * 5 * i / 48));
    osc_q_[i] = float(std::sin(2 * kPi * 5 * i / 48));
  }
  reset();
}
void Decoder::discontinuity() {
  for (auto& ch : channels_) {
    ch = Channel{};
    for (size_t i = 0; i < kLanes; ++i) ch.lanes[i].phase = uint32_t(i * 480000 / kLanes);
  }
  ring_ = oscillator_ = 0; samples_ = 0; pending_i_ = false;
}
void Decoder::reset() { stats_ = {}; discontinuity(); }
void Decoder::chip(Channel& ch, Lane& lane, bool bit, uint8_t channel,
                   const Context& context, Callback callback, void* user) {
  constexpr uint64_t sync = 0x5599a5a955666596ULL;
  if (!lane.collecting) {
    lane.sync = (lane.sync << 1) | uint64_t(bit);
    const int errors = __builtin_popcountll(lane.sync ^ sync);
    if (errors <= 1 || errors >= 63) {
      lane.collecting = true; lane.inverted = errors >= 63;
      lane.chips = 0; std::memset(lane.packet, 0, sizeof(lane.packet));
      ++stats_.syncs;
    }
    return;
  }
  bit ^= lane.inverted;
  if (!(lane.chips & 1)) lane.first = uint8_t(bit);
  else {
    if (lane.first == uint8_t(bit)) { lane.collecting = false; lane.sync = 0; return; }
    const unsigned index = lane.chips / 2;
    // The sync word uses IEEE Manchester; FLARM's payload is inverted.
    lane.packet[index / 8] = uint8_t((lane.packet[index / 8] << 1) | lane.first);
  }
  if (++lane.chips != kPacketBytes * 16) return;
  lane.collecting = false; lane.sync = 0;
  if (ch.has_frame && samples_ - ch.last_frame_sample < 960) return;
  Frame frame;
  const Result result = decode_packet(lane.packet, sizeof(lane.packet), context, &frame);
  if (result == Result::crc_error) { ++stats_.crc_errors; return; }
  ++stats_.crc_ok;
  ch.has_frame = true; ch.last_frame_sample = samples_;
  if (result == Result::unsupported) ++stats_.unsupported;
  if (result == Result::invalid) ++stats_.invalid;
  if (result != Result::ok) return;
  frame.channel = channel;
  frame.signal_dbfs = 10 * std::log10(std::max(ch.power, 1e-12f));
  if (frame.generation == 6) ++stats_.v6; else ++stats_.v7;
  if (callback) callback(frame, user);
}
void Decoder::process_cu8(const uint8_t* iq, size_t bytes, const Context& ctx,
                          Callback callback, void* user) {
  if (!iq) return;
  const uint64_t first_sample = samples_;
  for (size_t b = 0; b < bytes; ++b) {
    if (!pending_i_) { i_byte_ = iq[b]; pending_i_ = true; continue; }
    pending_i_ = false;
    const float i = (float(i_byte_) - 127.5f) / 128, q = (float(iq[b]) - 127.5f) / 128;
    const float c = osc_i_[oscillator_], s = osc_q_[oscillator_];
    for (uint8_t n = 0; n < 2; ++n) {
      auto& ch = channels_[n];
      const float sn = n == 0 ? s : -s;
      ch.i[ring_] = i * c - q * sn; ch.q[ring_] = i * sn + q * c;
      if (samples_ & 1) continue;
      float fi = 0, fq = 0;
      size_t index = ring_;
      for (size_t t = 0; t < kTaps; ++t) {
        fi += taps_[t] * ch.i[index]; fq += taps_[t] * ch.q[index];
        index = index == 0 ? kTaps - 1 : index - 1;
      }
      const float discriminator = fq * ch.prev_i - fi * ch.prev_q;
      ch.prev_i = fi; ch.prev_q = fq;
      ch.power += 0.02f * (fi * fi + fq * fq - ch.power);
      Context at_sample = ctx;
      if (ctx.unix_ms) at_sample.unix_ms += (samples_ - first_sample) * 1000 / kSampleRate;
      for (auto& lane : ch.lanes) {
        lane.sum += discriminator;
        lane.phase += 100000;
        if (lane.phase >= 480000) {
          lane.phase -= 480000;
          chip(ch, lane, lane.sum >= 0, n, at_sample, callback, user);
          lane.sum = 0;
        }
      }
    }
    ring_ = (ring_ + 1) % kTaps; oscillator_ = (oscillator_ + 1) % 48; ++samples_;
  }
}
} // namespace orcsdr::flarm_rx
