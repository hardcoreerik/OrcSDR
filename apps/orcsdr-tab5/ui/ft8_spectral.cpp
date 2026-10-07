#include "ft8_spectral.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orcsdr::ftx::spectral {
namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

bool config_valid(const ModeProfile& profile, const Config& config,
                  const OutputGrid& output) {
  if (!profile_valid(profile) || profile.sample_rate_hz == 0 ||
      profile.symbol_samples == 0 ||
      profile.symbol_samples > kMaxSymbolSamples)
    return false;
  if (config.bin_count == 0 || config.bin_spacing_millihz == 0 ||
      config.rows_per_symbol == 0)
    return false;
  if (profile.symbol_samples % config.rows_per_symbol != 0) return false;
  if (output.cells == nullptr || output.row_capacity == 0 ||
      output.stride < config.bin_count)
    return false;

  const uint64_t highest_millihz =
      static_cast<uint64_t>(config.first_bin_millihz) +
      static_cast<uint64_t>(config.bin_count - 1) *
          config.bin_spacing_millihz;
  const uint64_t nyquist_millihz =
      static_cast<uint64_t>(profile.sample_rate_hz) * 500ull;
  return highest_millihz < nyquist_millihz;
}

bool compute_row(ReferenceAccumulator* state) {
  if (state == nullptr || state->profile == nullptr ||
      state->rows_written >= state->output.row_capacity)
    return false;

  const auto& profile = *state->profile;
  const std::size_t n = profile.symbol_samples;
  float* row =
      state->output.cells + state->rows_written * state->output.stride;

  for (std::size_t bin = 0; bin < state->config.bin_count; ++bin) {
    const double frequency = bin_frequency_hz(state->config, bin);
    const double step =
        kTwoPi * frequency / static_cast<double>(profile.sample_rate_hz);
    const double step_cos = std::cos(step);
    const double step_sin = std::sin(step);
    double osc_cos = 1.0;
    double osc_sin = 0.0;
    double re = 0.0;
    double im = 0.0;

    for (std::size_t i = 0; i < n; ++i) {
      const double sample = static_cast<double>(state->window[i]);
      re += sample * osc_cos;
      im -= sample * osc_sin;
      const double next_cos = osc_cos * step_cos - osc_sin * step_sin;
      osc_sin = osc_sin * step_cos + osc_cos * step_sin;
      osc_cos = next_cos;
    }

    const double scale = 1.0 / static_cast<double>(n);
    const double re_norm = re * scale;
    const double im_norm = im * scale;
    const double power = re_norm * re_norm + im_norm * im_norm;
    if (!std::isfinite(power) || power < 0.0) return false;
    row[bin] = static_cast<float>(power);
  }

  ++state->rows_written;
  return true;
}

}  // namespace

bool begin(ReferenceAccumulator* state, const ModeProfile& profile,
           const Config& config, const OutputGrid& output) {
  if (state == nullptr || !config_valid(profile, config, output)) return false;
  *state = ReferenceAccumulator{};
  state->profile = &profile;
  state->config = config;
  state->output = output;
  state->hop_samples =
      profile.symbol_samples / static_cast<std::size_t>(config.rows_per_symbol);
  state->active = true;
  return true;
}

bool offer(ReferenceAccumulator* state, const int16_t* samples,
           std::size_t count) {
  if (state == nullptr || !state->active || state->profile == nullptr ||
      (count != 0 && samples == nullptr))
    return false;

  const std::size_t n = state->profile->symbol_samples;
  std::size_t offset = 0;
  while (offset < count) {
    const std::size_t needed = n - state->window_count;
    const std::size_t take = std::min(needed, count - offset);
    std::memcpy(state->window.data() + state->window_count, samples + offset,
                take * sizeof(int16_t));
    state->window_count += take;
    state->samples_offered += take;
    offset += take;

    if (state->window_count != n) continue;
    if (!compute_row(state)) return false;

    if (state->hop_samples < n) {
      const std::size_t keep = n - state->hop_samples;
      std::memmove(state->window.data(),
                   state->window.data() + state->hop_samples,
                   keep * sizeof(int16_t));
      state->window_count = keep;
    } else {
      state->window_count = 0;
    }
  }
  return true;
}

void reset(ReferenceAccumulator* state) {
  if (state == nullptr) return;
  const auto* profile = state->profile;
  const Config config = state->config;
  const OutputGrid output = state->output;
  const std::size_t hop = state->hop_samples;
  *state = ReferenceAccumulator{};
  state->profile = profile;
  state->config = config;
  state->output = output;
  state->hop_samples = hop;
  state->active = profile != nullptr;
}

std::size_t rows_written(const ReferenceAccumulator& state) {
  return state.rows_written;
}

double bin_frequency_hz(const Config& config, std::size_t bin) {
  const uint64_t millihz =
      static_cast<uint64_t>(config.first_bin_millihz) +
      static_cast<uint64_t>(bin) * config.bin_spacing_millihz;
  return static_cast<double>(millihz) / 1000.0;
}

bool self_check() {
  float grid[8]{};
  ReferenceAccumulator state{};
  Config config{};
  config.first_bin_millihz = 1000000;
  config.bin_spacing_millihz = 6250;
  config.bin_count = 8;
  config.rows_per_symbol = 1;
  OutputGrid output{grid, 1, 8};
  return begin(&state, profile(Mode::ft8), config, output) &&
         state.hop_samples == profile(Mode::ft8).symbol_samples &&
         bin_frequency_hz(config, 4) == 1025.0;
}

}  // namespace orcsdr::ftx::spectral
