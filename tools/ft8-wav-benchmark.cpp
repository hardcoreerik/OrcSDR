#include "ft8_mode.hpp"
#include "ft8_pipeline.hpp"
#include "ft8_spectral.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         static_cast<uint16_t>(p[1] << 8u);
}

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8u) |
         (static_cast<uint32_t>(p[2]) << 16u) |
         (static_cast<uint32_t>(p[3]) << 24u);
}

bool read_exact(std::ifstream* file, void* out, std::size_t bytes) {
  file->read(static_cast<char*>(out), static_cast<std::streamsize>(bytes));
  return file->good() || static_cast<std::size_t>(file->gcount()) == bytes;
}

struct Wav {
  uint32_t sample_rate = 0;
  std::vector<int16_t> samples;
};

bool read_wav(const char* path, Wav* wav) {
  if (path == nullptr || wav == nullptr) return false;
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  std::array<uint8_t, 12> header{};
  if (!read_exact(&file, header.data(), header.size()) ||
      std::memcmp(header.data(), "RIFF", 4) != 0 ||
      std::memcmp(header.data() + 8, "WAVE", 4) != 0)
    return false;

  bool have_fmt = false;
  bool have_data = false;
  uint16_t format = 0, channels = 0, bits = 0, block_align = 0;
  uint32_t sample_rate = 0;
  std::vector<uint8_t> data;

  while (file && !(have_fmt && have_data)) {
    std::array<uint8_t, 8> chunk{};
    if (!read_exact(&file, chunk.data(), chunk.size())) break;
    const uint32_t size = le32(chunk.data() + 4);
    if (size > 16u * 1024u * 1024u) return false;

    if (std::memcmp(chunk.data(), "fmt ", 4) == 0) {
      if (size < 16) return false;
      std::vector<uint8_t> fmt(size);
      if (!read_exact(&file, fmt.data(), fmt.size())) return false;
      format = le16(fmt.data());
      channels = le16(fmt.data() + 2);
      sample_rate = le32(fmt.data() + 4);
      block_align = le16(fmt.data() + 12);
      bits = le16(fmt.data() + 14);
      have_fmt = true;
    } else if (std::memcmp(chunk.data(), "data", 4) == 0) {
      data.resize(size);
      if (!read_exact(&file, data.data(), data.size())) return false;
      have_data = true;
    } else {
      file.seekg(size, std::ios::cur);
      if (!file) return false;
    }
    if ((size & 1u) != 0) file.seekg(1, std::ios::cur);
  }

  if (!have_fmt || !have_data || format != 1 || channels != 1 ||
      sample_rate != 12000 || bits != 16 || block_align != 2 ||
      (data.size() & 1u) != 0)
    return false;

  wav->sample_rate = sample_rate;
  wav->samples.resize(data.size() / 2);
  for (std::size_t i = 0; i < wav->samples.size(); ++i)
    wav->samples[i] = static_cast<int16_t>(le16(data.data() + i * 2));
  return true;
}

bool parse_u8(const char* text, uint8_t* value) {
  if (text == nullptr || value == nullptr || *text == '\0') return false;
  unsigned long v = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + static_cast<unsigned long>(*p - '0');
    if (v > 255) return false;
  }
  *value = static_cast<uint8_t>(v);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 4) {
    std::fprintf(stderr,
                 "usage: %s <12k-mono-i16.wav> [rows_per_symbol=2] "
                 "[bins_per_tone=1]\n",
                 argv[0]);
    return 2;
  }

  uint8_t rows_per_symbol = 2;
  uint8_t bins_per_tone = 1;
  if (argc >= 3 && !parse_u8(argv[2], &rows_per_symbol)) return 2;
  if (argc >= 4 && !parse_u8(argv[3], &bins_per_tone)) return 2;
  if ((rows_per_symbol != 1 && rows_per_symbol != 2 &&
       rows_per_symbol != 4) ||
      (bins_per_tone != 1 && bins_per_tone != 2)) {
    std::fprintf(stderr,
                 "rows_per_symbol must be 1, 2, or 4; "
                 "bins_per_tone must be 1 or 2\n");
    return 2;
  }

  Wav wav{};
  if (!read_wav(argv[1], &wav)) {
    std::fprintf(stderr,
                 "unsupported WAV: require RIFF PCM, mono, 12000 Hz, 16-bit\n");
    return 2;
  }

  const auto& p = orcsdr::ftx::profile(orcsdr::ftx::Mode::ft8);
  const std::size_t hop =
      p.symbol_samples / static_cast<std::size_t>(rows_per_symbol);
  if (wav.samples.size() < p.symbol_samples) {
    std::fprintf(stderr, "WAV is shorter than one FT8 symbol\n");
    return 2;
  }
  const std::size_t rows =
      1 + (wav.samples.size() - p.symbol_samples) / hop;

  constexpr uint32_t kFirstMillihz = 200000;
  constexpr uint32_t kLastMillihz = 3000000;
  const uint32_t spacing =
      p.tone_spacing_millihz / bins_per_tone;
  const std::size_t bins =
      1 + (kLastMillihz - kFirstMillihz) / spacing;
  if (bins > std::numeric_limits<uint16_t>::max()) return 2;

  std::vector<float> energy(rows * bins, 0.0f);
  orcsdr::ftx::spectral::Config spectral_config{};
  spectral_config.first_bin_millihz = kFirstMillihz;
  spectral_config.bin_spacing_millihz = spacing;
  spectral_config.bin_count = static_cast<uint16_t>(bins);
  spectral_config.rows_per_symbol = rows_per_symbol;

  orcsdr::ftx::spectral::ReferenceAccumulator spectral{};
  if (!orcsdr::ftx::spectral::begin(
          &spectral, p, spectral_config,
          orcsdr::ftx::spectral::OutputGrid{
              energy.data(), rows, bins}) ||
      !orcsdr::ftx::spectral::offer(
          &spectral, wav.samples.data(), wav.samples.size())) {
    std::fprintf(stderr, "spectral analysis failed\n");
    return 1;
  }

  orcsdr::ftx::sync::EnergyGrid grid{
      energy.data(), orcsdr::ftx::spectral::rows_written(spectral),
      bins, bins};
  orcsdr::ftx::sync::Geometry geometry{
      rows_per_symbol, bins_per_tone};

  orcsdr::ftx::pipeline::Config config{};
  config.candidate_limit = 16;
  config.search.min_score = 0.10f;
  config.search.suppress_time_rows = rows_per_symbol;
  config.search.suppress_frequency_bins = bins_per_tone;

  orcsdr::ftx::pipeline::Workspace workspace{};
  std::array<orcsdr::ftx::pipeline::FrameResult, 16> results{};
  const std::size_t count = orcsdr::ftx::pipeline::decode_grid(
      p, grid, geometry, config, &workspace, results.data(), results.size());

  std::printf(
      "FT8_WAV_BENCH file=%s samples=%zu rows=%zu bins=%zu "
      "rows_per_symbol=%u bins_per_tone=%u decoded=%zu\n",
      argv[1], wav.samples.size(), grid.rows, grid.bins,
      rows_per_symbol, bins_per_tone, count);

  for (std::size_t i = 0; i < count; ++i) {
    const auto& r = results[i];
    const double audio_hz =
        orcsdr::ftx::spectral::bin_frequency_hz(
            spectral_config, r.candidate.base_bin);
    const double start_s =
        static_cast<double>(r.candidate.start_row * hop) /
        static_cast<double>(p.sample_rate_hz);
    std::printf(
        "DECODE index=%zu start_s=%.3f audio_hz=%.3f "
        "sync=%.4f contrast=%.4f ldpc_iter=%u text=%s\n",
        i, start_s, audio_hz, r.candidate.score,
        r.mean_symbol_contrast,
        static_cast<unsigned>(r.ldpc_iterations),
        r.standard.text);
  }

  return 0;
}
