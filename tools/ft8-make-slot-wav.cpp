// Host tool: writes a mono 12 kHz PCM16 WAV of whole 15 s FT8 slots holding several synthetic stations, for replaying a known,
// decodable signal through a receive path (for example as IQ over rtl_tcp with tools/rtl_tcp_replay_server.py --usb).
//
//   g++ -std=c++17 -O2 -Iapps/orcsdr-tab5/ui tools/ft8-make-slot-wav.cpp apps/orcsdr-tab5/ui/ft8_mode.cpp
//       apps/orcsdr-tab5/ui/ft8_codec.cpp apps/orcsdr-tab5/ui/ft8_ldpc.cpp -o ft8-make-slot-wav   (one command)
//   ./ft8-make-slot-wav out.wav [--slots N] [--stations K] [--snr-top DB] [--snr-bottom DB] [--noise SIGMA]
//
// Each station sends "CQ <call> <grid>" encoded with the production codec and shaped as Gaussian CPFSK (BT 2.0), starting about
// 0.5 s into the slot (dt within +-0.2 s), at its own audio frequency (400 Hz upward, 300 Hz apart, so none overlap). SNRs spread
// evenly from --snr-top to --snr-bottom in the usual convention (signal power over noise power in 2500 Hz); the noise is white
// Gaussian. Slot n shifts which callsigns are heard so consecutive slots differ. Test fixture only: the encoder never ships.
#include "ft8_codec.hpp"
#include "ft8_ldpc.hpp"
#include "ft8_mode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kPi = 3.14159265358979323846;

void put_bits(orcsdr::ft8::codec::PayloadBits* payload, std::size_t offset, std::size_t count, uint32_t value) {
  for (std::size_t i = 0; i < count; ++i) (*payload)[offset + i] = static_cast<uint8_t>((value >> (count - 1 - i)) & 1u);
}

uint16_t grid15(const char* t) { return static_cast<uint16_t>((t[0] - 'A') * 1800 + (t[1] - 'A') * 100 + (t[2] - '0') * 10 + (t[3] - '0')); }

bool cq_payload(const char* call, const char* grid, orcsdr::ft8::codec::PayloadBits* out) {
  uint32_t c28 = 0;
  if (!orcsdr::ft8::codec::encode_standard_callsign(call, &c28)) return false;
  *out = {};
  put_bits(out, 0, 28, 2);
  put_bits(out, 29, 28, c28);
  put_bits(out, 59, 15, grid15(grid));
  put_bits(out, 74, 3, 1);
  return true;
}

std::vector<int> build_sequence(const orcsdr::ftx::ModeProfile& p, const orcsdr::ft8::codec::CodewordBits& codeword) {
  std::vector<int> seq(p.channel_symbols, -1);
  const std::size_t sync_len = 7;
  for (std::size_t b = 0; b < p.sync_block_count; ++b)
    for (std::size_t i = 0; i < sync_len; ++i) seq[p.sync[b].first_symbol + i] = p.sync[b].tones[i];
  std::size_t bit = 0;
  for (std::size_t b = 0; b < p.data_block_count; ++b)
    for (std::size_t i = 0; i < p.data[b].length; ++i) {
      uint8_t pattern = 0;
      for (uint8_t k = 0; k < p.bits_per_tone; ++k) pattern = static_cast<uint8_t>((pattern << 1) | (codeword[bit++] & 1u));
      for (uint8_t tone = 0; tone < p.tone_count; ++tone)
        if (p.tone_bits[tone] == pattern) seq[p.data[b].first_symbol + i] = tone;
    }
  return seq;
}

void put32(std::FILE* f, uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc((v >> (8 * i)) & 255, f); }
void put16(std::FILE* f, uint16_t v) { for (int i = 0; i < 2; ++i) std::fputc((v >> (8 * i)) & 255, f); }
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s out.wav [--slots N] [--stations K] [--snr-top DB] [--snr-bottom DB] [--noise SIGMA]\n", argv[0]);
    return 2;
  }
  int slots = 4, stations = 8;
  double snr_top = 4.0, snr_bottom = -12.0, sigma = 1000.0;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--slots" && i + 1 < argc) slots = std::atoi(argv[++i]);
    else if (a == "--stations" && i + 1 < argc) stations = std::atoi(argv[++i]);
    else if (a == "--snr-top" && i + 1 < argc) snr_top = std::atof(argv[++i]);
    else if (a == "--snr-bottom" && i + 1 < argc) snr_bottom = std::atof(argv[++i]);
    else if (a == "--noise" && i + 1 < argc) sigma = std::atof(argv[++i]);
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
  }
  if (slots < 1 || stations < 1 || stations > 8) { std::fprintf(stderr, "need 1+ slots and 1-8 stations\n"); return 2; }
  const char* calls[] = {"K1ABC", "W9XYZ", "JA1XYZ", "DL1ABC", "VK2RF", "N7QQ", "EA6VQ", "PY2APK"};
  const char* grids[] = {"FN42", "EN61", "PM95", "JO62", "QF56", "CN87", "JM19", "GG66"};

  const auto& prof = orcsdr::ftx::profile(orcsdr::ftx::Mode::ft8);
  const double spacing_hz = prof.tone_spacing_millihz / 1000.0;
  const std::size_t sym = prof.symbol_samples;
  const std::size_t slot_samples = static_cast<std::size_t>(prof.slot_ms) * 12u;   // 12 kS/s
  std::mt19937 rng(20261109);
  std::normal_distribution<double> gauss(0.0, 1.0);
  std::uniform_real_distribution<double> uni(0.0, 1.0);

  std::vector<int16_t> pcm;
  pcm.reserve(slot_samples * static_cast<std::size_t>(slots));
  for (int s = 0; s < slots; ++s) {
    std::vector<double> x(slot_samples, 0.0);
    for (int k = 0; k < stations; ++k) {
      const int who = (k + s) % 8;
      orcsdr::ft8::codec::PayloadBits payload{};
      if (!cq_payload(calls[who], grids[who], &payload)) return 1;
      const auto message = orcsdr::ft8::codec::append_crc(payload);
      const auto codeword = orcsdr::ft8::ldpc::encode(message);
      const auto seq = build_sequence(prof, codeword);
      const double snr = stations == 1 ? snr_top : snr_top + (snr_bottom - snr_top) * k / (stations - 1);
      const double noise_2500 = sigma * sigma * 2500.0 / 6000.0;
      const double amplitude = std::sqrt(2.0 * noise_2500 * std::pow(10.0, snr / 10.0));
      const double base_hz = 400.0 + 300.0 * k;
      const double start_s = 0.5 + (uni(rng) - 0.5) * 0.4;
      double phase = uni(rng) * kTwoPi;
      const std::size_t first = static_cast<std::size_t>(start_s * 12000.0);
      const double kk = kPi * 2.0 * std::sqrt(2.0 / std::log(2.0));
      auto pulse = [&](double u) { return 0.5 * (std::erf(kk * (u + 0.5)) - std::erf(kk * (u - 0.5))); };
      const long symbols = static_cast<long>(seq.size());
      for (std::size_t n = 0; n < static_cast<std::size_t>(symbols) * sym + sym && first + n < slot_samples; ++n) {
        const double u = static_cast<double>(n) / static_cast<double>(sym) - 0.5;
        const long centre = static_cast<long>(std::floor(u + 0.5));
        double tone_sum = 0.0;
        for (long q = centre - 2; q <= centre + 2; ++q) {
          if (q < 0 || q >= symbols) continue;
          const int tone = seq[static_cast<std::size_t>(q)] < 0 ? 0 : seq[static_cast<std::size_t>(q)];
          tone_sum += tone * pulse(u - static_cast<double>(q));
        }
        x[first + n] += amplitude * std::sin(phase);
        phase += kTwoPi * (base_hz + spacing_hz * tone_sum) / 12000.0;
        if (phase >= kTwoPi) phase -= kTwoPi;
      }
    }
    for (std::size_t i = 0; i < slot_samples; ++i) {
      const double v = x[i] + sigma * gauss(rng);
      pcm.push_back(static_cast<int16_t>(std::max(-32767.0, std::min(32767.0, std::round(v)))));
    }
  }
  std::FILE* f = std::fopen(argv[1], "wb");
  if (!f) { std::fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
  const uint32_t bytes = static_cast<uint32_t>(pcm.size() * 2u);
  std::fwrite("RIFF", 1, 4, f); put32(f, 36u + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
  put32(f, 16); put16(f, 1); put16(f, 1); put32(f, 12000); put32(f, 24000); put16(f, 2); put16(f, 16);
  std::fwrite("data", 1, 4, f); put32(f, bytes);
  for (int16_t v : pcm) put16(f, static_cast<uint16_t>(v));
  std::fclose(f);
  std::printf("wrote %s: %d slots of %u s, %d stations per slot, SNR %+.0f to %+.0f dB, 12 kS/s\n", argv[1], slots,
              static_cast<unsigned>(prof.slot_ms / 1000u), stations, snr_top, snr_bottom);
  return 0;
}
