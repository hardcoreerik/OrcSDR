// Host tool: checks the SNR estimator against synthetic FT8/FT4 signals of known strength.
//
//   ft8-snr-sweep [--ft4] [--trials N] [--from DB] [--to DB] [--step DB] [--plain-fsk] [--check]
//
// A CQ message is encoded with the production codec, synthesised as Gaussian-shaped continuous-phase FSK (BT 2.0 for FT8, 1.0 for
// FT4) and mixed with Gaussian noise so the SNR is exactly the requested value in the usual convention (signal power over noise
// power in 2500 Hz). Each trial uses a random audio frequency and start time. The signal goes through the same native backend as
// the Tab5 (spectral grid, sync search, LDPC, CRC) and the backend's SNR is compared with the truth. Output: decode rate and
// error statistics per SNR step, from which the correction offset in ft8_snr.hpp is fitted. Test fixture only: the encoder never
// ships. --check runs a few fixed points and exits non-zero if the estimate drifts (used by tools/test-ft8.sh).
#include "ft8_codec.hpp"
#include "ft8_ldpc.hpp"
#include "ft8_mode.hpp"
#include "ft8_native_backend.hpp"

#include <algorithm>
#include <cmath>
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

// Channel tones for one frame, from the mode profile: sync blocks, then data symbols from the codeword bits. -1 marks a ramp symbol.
std::vector<int> build_sequence(const orcsdr::ftx::ModeProfile& p, const orcsdr::ft8::codec::CodewordBits& codeword) {
  std::vector<int> seq(p.channel_symbols, -1);
  const std::size_t sync_len = p.mode == orcsdr::ftx::Mode::ft8 ? 7 : 4;
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
}  // namespace

int main(int argc, char** argv) {
  int trials = 60;
  double from = 4.0, to = -24.0, step = 2.0;
  bool gfsk = true, ft4 = false;
  bool check = false;   // regression mode: fixed points, non-zero exit if the estimate drifts
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--trials" && i + 1 < argc) trials = std::atoi(argv[++i]);
    else if (a == "--from" && i + 1 < argc) from = std::atof(argv[++i]);
    else if (a == "--to" && i + 1 < argc) to = std::atof(argv[++i]);
    else if (a == "--step" && i + 1 < argc) step = std::atof(argv[++i]);
    else if (a == "--plain-fsk") gfsk = false;
    else if (a == "--ft4") ft4 = true;
    else if (a == "--check") { check = true; trials = 16; from = 8.0; to = -16.0; step = 8.0; }
    else { std::fprintf(stderr, "usage: %s [--ft4] [--trials N] [--from DB] [--to DB] [--step DB] [--plain-fsk] [--check]\n", argv[0]); return 2; }
  }
  const char* calls[] = {"K1ABC", "W9XYZ", "JA1XYZ", "DL1ABC", "VK2RF", "N7QQ", "EA6VQ", "PY2APK"};
  const char* grids[] = {"FN42", "EN61", "PM95", "JO62", "QF56", "CN87", "JM19", "GG66"};

  const orcsdr::ftx::Mode mode = ft4 ? orcsdr::ftx::Mode::ft4 : orcsdr::ftx::Mode::ft8;
  const auto& prof = orcsdr::ftx::profile(mode);
  const double spacing_hz = prof.tone_spacing_millihz / 1000.0;
  const std::size_t sym = prof.symbol_samples;
  const std::size_t total = static_cast<std::size_t>(prof.slot_ms) * 12u;   // 12 kS/s
  orcsdr::ftx::native::Backend backend;
  if (!backend.begin(12000, mode, orcsdr::ftx::native::Config{})) {
    std::fprintf(stderr, "backend begin failed\n");
    return 1;
  }
  std::mt19937 rng(20261008);
  std::normal_distribution<double> gauss(0.0, 1.0);
  std::uniform_real_distribution<double> uni(0.0, 1.0);

  std::printf("%s  true_snr  trials  decoded  rate   mean_est  mean_err  sd_err   (error = estimate - truth, dB)\n", prof.name);
  double fit_sum = 0.0;
  int fit_n = 0;
  bool check_failed = false;
  for (double snr = from; snr >= to - 1e-9; snr -= step) {
    int decoded = 0;
    double err_sum = 0.0, err_sq = 0.0, est_sum = 0.0;
    for (int t = 0; t < trials; ++t) {
      orcsdr::ft8::codec::PayloadBits payload{};
      const int k = t % 8;
      if (!cq_payload(calls[k], grids[k], &payload)) return 1;
      if (ft4) orcsdr::ft8::codec::restore_ft4_payload(&payload);   // XOR is symmetric: this is the transmit-side scramble
      const auto message = orcsdr::ft8::codec::append_crc(payload);
      const auto codeword = orcsdr::ft8::ldpc::encode(message);
      const auto seq = build_sequence(prof, codeword);

      const double base_hz = 500.0 + (2000.0 - 120.0) * uni(rng);
      const double start_s = 0.5 + (uni(rng) - 0.5) * 0.4;   // dt within +-0.2 s
      std::vector<double> x(total, 0.0);
      const double amplitude = 2000.0;
      double phase = uni(rng) * kTwoPi;
      const std::size_t first = static_cast<std::size_t>(start_s * 12000.0);
      const double bt = ft4 ? 1.0 : 2.0, kk = kPi * bt * std::sqrt(2.0 / std::log(2.0));
      auto pulse = [&](double u) { return 0.5 * (std::erf(kk * (u + 0.5)) - std::erf(kk * (u - 0.5))); };   // u in symbols
      const long symbols = static_cast<long>(seq.size());
      for (std::size_t n = 0; n < static_cast<std::size_t>(symbols) * sym + sym && first + n < total; ++n) {
        const double u = static_cast<double>(n) / static_cast<double>(sym) - 0.5;
        const long centre = static_cast<long>(std::floor(u + 0.5));
        double tone_sum = 0.0, env = 1.0;
        for (long q = centre - 2; q <= centre + 2; ++q) {
          if (q < 0 || q >= symbols) continue;
          const int tone = seq[static_cast<std::size_t>(q)] < 0 ? 0 : seq[static_cast<std::size_t>(q)];
          tone_sum += tone * (gfsk ? pulse(u - static_cast<double>(q)) : (q == centre ? 1.0 : 0.0));
        }
        if (ft4) {   // the first and last symbols are power ramps
          const double pos = static_cast<double>(n) / static_cast<double>(sym);
          if (pos < 1.0) env = std::sin(0.5 * kPi * pos);
          else if (pos > static_cast<double>(symbols - 1)) env = std::max(0.0, std::cos(0.5 * kPi * (pos - static_cast<double>(symbols - 1))));
        }
        x[first + n] = env * amplitude * std::sin(phase);
        phase += kTwoPi * (base_hz + spacing_hz * tone_sum) / 12000.0;
        if (phase >= kTwoPi) phase -= kTwoPi;
      }
      const double signal_power = amplitude * amplitude / 2.0;
      const double snr_lin = std::pow(10.0, snr / 10.0);
      const double sigma = std::sqrt(signal_power * (6000.0 / 2500.0) / snr_lin);   // white noise over 0-6 kHz; 2500 Hz holds 2500/6000 of it
      std::vector<int16_t> pcm(total);
      for (std::size_t i = 0; i < total; ++i) {
        const double v = x[i] + sigma * gauss(rng);
        pcm[i] = static_cast<int16_t>(std::max(-32767.0, std::min(32767.0, std::round(v))));
      }
      backend.begin_slot(1791440160000ull + static_cast<uint64_t>(t) * 15000ull);
      backend.offer_audio(pcm.data(), pcm.size());
      orcsdr::ft8::Decode out[8];
      const std::size_t n = backend.finish_slot(out, 8);
      for (std::size_t i = 0; i < n; ++i) {
        char want[48];
        std::snprintf(want, sizeof(want), "CQ %s %s", calls[k], grids[k]);
        if (std::strcmp(out[i].message, want) != 0) continue;
        if (out[i].flags & orcsdr::ft8::decode_flag_snr_unavailable) continue;
        ++decoded;
        const double err = out[i].snr_db - snr;
        est_sum += out[i].snr_db;
        err_sum += err;
        err_sq += err * err;
        if (snr >= -20.0 && snr <= 0.0) { fit_sum += err; ++fit_n; }
        break;
      }
    }
    if (check && (decoded < trials * 9 / 10 || (decoded > 0 && std::fabs(err_sum / decoded) > 1.5))) check_failed = true;
    if (decoded > 0) {
      const double mean_err = err_sum / decoded;
      std::printf("%s  %+7.1f   %5d   %5d  %4.0f%%  %+8.1f  %+8.2f  %6.2f\n", prof.name, snr, trials, decoded, 100.0 * decoded / trials,
                  est_sum / decoded, mean_err, std::sqrt(std::max(0.0, err_sq / decoded - mean_err * mean_err)));
    } else {
      std::printf("%s  %+7.1f   %5d   %5d  %4.0f%%        --        --      --\n", prof.name, snr, trials, 0, 0.0);
    }
  }
  if (check) { std::puts(check_failed ? "ft8-snr-sweep --check: FAIL" : "ft8-snr-sweep --check: PASS"); return check_failed ? 1 : 0; }
  if (fit_n > 0) std::printf("\nmean error over -20..0 dB (%d decodes): %+.2f dB  -> offset_db to apply: %+.2f\n", fit_n, fit_sum / fit_n, -fit_sum / fit_n);
  return 0;
}
