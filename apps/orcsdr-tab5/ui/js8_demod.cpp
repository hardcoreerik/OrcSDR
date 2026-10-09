#include "js8_demod.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace orcsdr::js8 {
namespace {
constexpr float kPi = 3.14159265358979323846f;

float tone_power(const int16_t* samples, size_t n, float hz, float sample_rate) {
  if (samples == nullptr || n == 0 || hz < 0.0f || hz >= sample_rate * 0.5f) return 0.0f;
  const float phase = -2.0f * kPi * hz / sample_rate;
  const float wr = std::cos(phase);
  const float wi = std::sin(phase);
  float zr = 1.0f, zi = 0.0f;
  float re = 0.0f, im = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    const float x = static_cast<float>(samples[i]);
    re += x * zr;
    im += x * zi;
    const float nr = zr * wr - zi * wi;
    zi = zr * wi + zi * wr;
    zr = nr;
    if ((i & 255u) == 255u) {
      const float mag2 = zr * zr + zi * zi;
      if (mag2 > 0.0f) {
        const float inv = 1.0f / std::sqrt(mag2);
        zr *= inv;
        zi *= inv;
      }
    }
  }
  const float scale = 1.0f / static_cast<float>(n);
  re *= scale;
  im *= scale;
  return re * re + im * im;
}

bool expected_sync_tone(const Profile& p, size_t symbol, uint8_t* tone) {
  if (!p.sync_pattern_verified || tone == nullptr) return false;
  for (const SyncBlock& block : p.sync) {
    if (symbol >= block.first_symbol && symbol < block.first_symbol + block.tones.size()) {
      *tone = block.tones[symbol - block.first_symbol];
      return true;
    }
  }
  return false;
}
}  // namespace

bool demodulate_tones(const int16_t* samples, size_t count, Submode submode,
                      const DemodConfig& config, RawFrame* frame, DemodStats* stats) {
  if (samples == nullptr || frame == nullptr || stats == nullptr || config.base_hz < 0.0f) return false;
  const Profile& p = profile(submode);
  if (!physical_layer_ready(submode)) return false;
  const size_t needed = static_cast<size_t>(p.channel_symbols) * p.symbol_samples;
  if (config.start_sample > count || needed > count - config.start_sample) return false;

  RawFrame decoded{};
  decoded.submode = submode;
  DemodStats measured{};
  float sync_sum = 0.0f;
  float margin_sum = 0.0f;
  size_t sync_count = 0;

  for (size_t symbol = 0; symbol < p.channel_symbols; ++symbol) {
    const int16_t* window = samples + config.start_sample + symbol * p.symbol_samples;
    std::array<float, 8> energy{};
    for (uint8_t tone = 0; tone < p.tone_count; ++tone) {
      const float hz = config.base_hz + tone * (static_cast<float>(p.tone_spacing_millihz) / 1000.0f);
      energy[tone] = tone_power(window, p.symbol_samples, hz, static_cast<float>(p.sample_rate_hz));
    }
    uint8_t best = 0;
    uint8_t second = 1;
    if (energy[second] > energy[best]) std::swap(best, second);
    for (uint8_t tone = 2; tone < p.tone_count; ++tone) {
      if (energy[tone] > energy[best]) {
        second = best;
        best = tone;
      } else if (energy[tone] > energy[second]) {
        second = tone;
      }
    }
    decoded.tones[symbol] = best;
    const float denom = energy[best] + energy[second] + 1.0e-20f;
    margin_sum += (energy[best] - energy[second]) / denom;

    uint8_t expected = 0;
    if (expected_sync_tone(p, symbol, &expected)) {
      ++sync_count;
      if (best == expected) ++measured.sync_hits;
      float competitor = 0.0f;
      for (uint8_t tone = 0; tone < p.tone_count; ++tone)
        if (tone != expected) competitor = std::max(competitor, energy[tone]);
      const float sync_denom = energy[expected] + competitor + 1.0e-20f;
      sync_sum += (energy[expected] - competitor) / sync_denom;
    }
  }

  measured.sync_score = sync_count ? sync_sum / static_cast<float>(sync_count) : 0.0f;
  measured.mean_margin = margin_sum / static_cast<float>(p.channel_symbols);
  *frame = decoded;
  *stats = measured;
  return measured.sync_hits >= config.min_sync_hits && measured.sync_score >= config.min_sync_score;
}

bool probe_sync(const int16_t* samples, size_t count, Submode submode, size_t start_sample, float base_hz, SyncProbe* out) {
  if (samples == nullptr || out == nullptr || base_hz < 0.0f) return false;
  const Profile& p = profile(submode);
  if (!physical_layer_ready(submode)) return false;
  const size_t needed = static_cast<size_t>(p.channel_symbols) * p.symbol_samples;
  if (start_sample > count || needed > count - start_sample) return false;
  SyncProbe probe{};
  float sum = 0.0f;
  size_t symbols = 0;
  for (const SyncBlock& block : p.sync) {
    for (size_t i = 0; i < block.tones.size(); ++i) {
      const int16_t* window = samples + start_sample + (block.first_symbol + i) * p.symbol_samples;
      float expected = 0.0f, competitor = 0.0f;
      uint8_t strongest = 0;
      float strongest_energy = -1.0f;
      for (uint8_t tone = 0; tone < p.tone_count; ++tone) {
        const float hz = base_hz + tone * (static_cast<float>(p.tone_spacing_millihz) / 1000.0f);
        const float e = tone_power(window, p.symbol_samples, hz, static_cast<float>(p.sample_rate_hz));
        if (tone == block.tones[i]) expected = e;
        else competitor = std::max(competitor, e);
        if (e > strongest_energy) {
          strongest_energy = e;
          strongest = tone;
        }
      }
      if (strongest == block.tones[i]) ++probe.hits;
      sum += (expected - competitor) / (expected + competitor + 1.0e-20f);
      ++symbols;
    }
  }
  probe.score = symbols ? sum / static_cast<float>(symbols) : 0.0f;
  *out = probe;
  return true;
}

bool demodulate_energies(const int16_t* samples, size_t count, Submode submode, size_t start_sample, float base_hz,
                         float energy[kChannelSymbols][8], DemodStats* stats) {
  if (samples == nullptr || energy == nullptr || stats == nullptr || base_hz < 0.0f) return false;
  const Profile& p = profile(submode);
  if (!physical_layer_ready(submode) || p.channel_symbols != kChannelSymbols || p.tone_count != 8) return false;
  const size_t needed = static_cast<size_t>(p.channel_symbols) * p.symbol_samples;
  if (start_sample > count || needed > count - start_sample) return false;
  DemodStats measured{};
  float sync_sum = 0.0f, margin_sum = 0.0f;
  size_t sync_count = 0;
  for (size_t symbol = 0; symbol < p.channel_symbols; ++symbol) {
    const int16_t* window = samples + start_sample + symbol * p.symbol_samples;
    uint8_t best = 0;
    for (uint8_t tone = 0; tone < 8; ++tone) {
      const float hz = base_hz + tone * (static_cast<float>(p.tone_spacing_millihz) / 1000.0f);
      energy[symbol][tone] = tone_power(window, p.symbol_samples, hz, static_cast<float>(p.sample_rate_hz));
      if (energy[symbol][tone] > energy[symbol][best]) best = tone;
    }
    float second = 0.0f;
    for (uint8_t tone = 0; tone < 8; ++tone)
      if (tone != best) second = std::max(second, energy[symbol][tone]);
    margin_sum += (energy[symbol][best] - second) / (energy[symbol][best] + second + 1.0e-20f);
    uint8_t expected = 0;
    if (expected_sync_tone(p, symbol, &expected)) {
      ++sync_count;
      if (best == expected) ++measured.sync_hits;
      float competitor = 0.0f;
      for (uint8_t tone = 0; tone < 8; ++tone)
        if (tone != expected) competitor = std::max(competitor, energy[symbol][tone]);
      sync_sum += (energy[symbol][expected] - competitor) / (energy[symbol][expected] + competitor + 1.0e-20f);
    }
  }
  measured.sync_score = sync_count ? sync_sum / static_cast<float>(sync_count) : 0.0f;
  measured.mean_margin = margin_sum / static_cast<float>(p.channel_symbols);
  *stats = measured;
  return true;
}

bool self_check_demod() {
  return profile(Submode::normal).symbol_samples == 1920 && physical_layer_ready(Submode::normal);
}

}  // namespace orcsdr::js8
