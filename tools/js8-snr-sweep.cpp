#include "js8_demod.hpp"
#include "js8_snr.hpp"
#include "js8_spectral.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr std::array<uint8_t, 79> kApiFrame{{
    4,2,5,6,1,3,0, 1,0,2,6,6,3,1,6,6,4,0,1,7,0,7,2,6,2,6,0,4,3,4,5,2,3,5,2,0,
    4,2,5,6,1,3,0, 3,4,2,5,4,5,7,0,1,6,3,6,7,0,2,3,5,6,4,5,7,4,0,0,1,7,3,6,4,
    4,2,5,6,1,3,0}};

}

int main(int argc, char** argv) {
  int trials = 12;
  float from = 4.0f, to = -20.0f, step = 4.0f;
  bool check = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--trials" && i + 1 < argc)
      trials = std::atoi(argv[++i]);
    else if (arg == "--from" && i + 1 < argc)
      from = std::strtof(argv[++i], nullptr);
    else if (arg == "--to" && i + 1 < argc)
      to = std::strtof(argv[++i], nullptr);
    else if (arg == "--step" && i + 1 < argc)
      step = std::strtof(argv[++i], nullptr);
    else if (arg == "--check") {
      check = true;
      trials = 6;
      from = 4.0f;
      to = -12.0f;
      step = 8.0f;
    } else {
      std::fprintf(stderr,
                   "usage: %s [--trials N] [--from DB] [--to DB] "
                   "[--step DB] [--check]\n", argv[0]);
      return 2;
    }
  }
  if (trials <= 0 || step <= 0.0f || from < to) return 2;

  const auto& p = orcsdr::js8::profile(orcsdr::js8::Submode::normal);
  const float spacing =
      static_cast<float>(p.tone_spacing_millihz) / 1000.0f;
  constexpr float kBaseHz = 1000.0f;
  const float first_hz = kBaseHz - 14.0f * spacing;
  constexpr uint16_t kBaseBin = 14;
  constexpr uint16_t kBins = 36;
  const size_t total =
      static_cast<size_t>(p.channel_symbols) * p.symbol_samples;

  orcsdr::js8::RawFrame expected{};
  expected.submode = orcsdr::js8::Submode::normal;
  expected.tones = kApiFrame;

  std::mt19937 rng(20261008u);
  std::normal_distribution<float> gauss(0.0f, 1.0f);

  bool failed = false;
  std::printf(
      "JS8_SYNTH_SNR true_db trials exact_tones rate mean_est mean_err\n");

  for (float truth = from; truth >= to - 1.0e-5f; truth -= step) {
    int recovered = 0;
    float estimate_sum = 0.0f;
    float error_sum = 0.0f;

    for (int trial = 0; trial < trials; ++trial) {
      constexpr float amplitude = 2000.0f;
      const float signal_power = amplitude * amplitude * 0.5f;
      const float snr_linear = std::pow(10.0f, truth / 10.0f);
      const float sigma =
          std::sqrt(signal_power * (6000.0f / 2500.0f) / snr_linear);

      std::vector<int16_t> pcm(total);
      float phase = 0.17f * static_cast<float>(trial + 1);
      for (size_t symbol = 0; symbol < kApiFrame.size(); ++symbol) {
        const float hz = kBaseHz + kApiFrame[symbol] * spacing;
        const float phase_step = kTwoPi * hz / 12000.0f;
        for (size_t n = 0; n < p.symbol_samples; ++n) {
          const size_t index = symbol * p.symbol_samples + n;
          const float value =
              amplitude * std::sin(phase) + sigma * gauss(rng);
          pcm[index] = static_cast<int16_t>(
              std::clamp(value, -32767.0f, 32767.0f));
          phase += phase_step;
          if (phase >= kTwoPi) phase -= kTwoPi;
        }
      }

      std::vector<float> energy(
          static_cast<size_t>(p.channel_symbols) * kBins, 0.0f);
      orcsdr::js8::spectral::Config spectral{};
      spectral.first_hz = first_hz;
      spectral.bin_spacing_hz = spacing;
      spectral.bin_count = kBins;
      spectral.rows_per_symbol = 1;
      const size_t rows = orcsdr::js8::spectral::power_grid(
          pcm.data(), pcm.size(), orcsdr::js8::Submode::normal,
          spectral,
          orcsdr::js8::spectral::OutputGrid{
              energy.data(), p.channel_symbols, kBins});
      if (rows != p.channel_symbols) return 1;

      orcsdr::js8::DemodConfig demod{};
      demod.base_hz = kBaseHz;
      demod.min_sync_score = -1.0f;
      demod.min_sync_hits = 0;
      orcsdr::js8::RawFrame observed{};
      orcsdr::js8::DemodStats demod_stats{};
      if (!orcsdr::js8::demodulate_tones(
              pcm.data(), pcm.size(), orcsdr::js8::Submode::normal,
              demod, &observed, &demod_stats))
        continue;
      if (observed.tones != expected.tones) continue;

      orcsdr::js8::sync::EnergyGrid grid{
          energy.data(), rows, kBins, kBins};
      const orcsdr::js8::sync::Candidate candidate{
          0, kBaseBin, 1.0f};
      float estimate_db = 0.0f;
      if (!orcsdr::js8::snr::estimate(
              p, grid, orcsdr::js8::sync::Geometry{1,1},
              candidate, observed, orcsdr::js8::snr::Calibration{},
              &estimate_db))
        continue;

      ++recovered;
      estimate_sum += estimate_db;
      error_sum += estimate_db - truth;
    }

    if (recovered != 0) {
      const float mean_est = estimate_sum / recovered;
      const float mean_err = error_sum / recovered;
      std::printf("%+6.1f %6d %11d %4.0f%% %+8.2f %+8.2f\n",
                  static_cast<double>(truth), trials, recovered,
                  100.0 * recovered / trials,
                  static_cast<double>(mean_est),
                  static_cast<double>(mean_err));
      if (check &&
          (recovered < trials * 5 / 6 || std::fabs(mean_err) > 2.0f))
        failed = true;
    } else {
      std::printf("%+6.1f %6d %11d    0%%       --       --\n",
                  static_cast<double>(truth), trials, 0);
      if (check) failed = true;
    }
  }

  if (check) {
    std::puts(failed ? "js8-snr-sweep --check: FAIL"
                     : "js8-snr-sweep --check: PASS");
    return failed ? 1 : 0;
  }
  return 0;
}
