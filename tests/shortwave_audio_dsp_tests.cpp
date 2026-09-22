#include "shortwave_audio_dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <vector>

namespace {
[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

double rms(const int16_t* samples, size_t count) {
  double sum = 0.0;
  for (size_t i = 0; i < count; ++i) sum += static_cast<double>(samples[i]) * samples[i];
  return std::sqrt(sum / count);
}

uint32_t le32(const unsigned char* value) {
  return static_cast<uint32_t>(value[0]) |
         static_cast<uint32_t>(value[1]) << 8u |
         static_cast<uint32_t>(value[2]) << 16u |
         static_cast<uint32_t>(value[3]) << 24u;
}

std::vector<int16_t> read_pcm16_wav(const char* path) {
  std::ifstream file(path, std::ios::binary);
  unsigned char header[44]{};
  CHECK(file.read(reinterpret_cast<char*>(header), sizeof(header)));
  CHECK(std::memcmp(header, "RIFF", 4) == 0);
  CHECK(std::memcmp(header + 8, "WAVEfmt ", 8) == 0);
  CHECK(le32(header + 24) == 48000);
  CHECK(header[22] == 1 && header[23] == 0);
  CHECK(header[34] == 16 && header[35] == 0);
  CHECK(std::memcmp(header + 36, "data", 4) == 0);
  std::vector<int16_t> samples(le32(header + 40) / sizeof(int16_t));
  CHECK(file.read(reinterpret_cast<char*>(samples.data()),
                  static_cast<std::streamsize>(samples.size() * sizeof(int16_t))));
  return samples;
}
}  // namespace

int main(int argc, char** argv) {
  using namespace orcsdr::shortwave::audio_dsp;
  CHECK(settings().noise_reduction == NoiseReduction::off);
  apply_clean_preset();
  CHECK(settings().noise_reduction == NoiseReduction::low);
  CHECK(settings().auto_notch);
  CHECK(settings().squelch == SquelchMode::off);

  cycle_noise_reduction();
  reset();
  std::vector<int16_t> noise(96000);
  uint32_t random = 1;
  for (auto& sample : noise) {
    random = random * 1664525u + 1013904223u;
    sample = static_cast<int16_t>((static_cast<int32_t>(random >> 16) - 32768) / 12);
  }
  const double noise_before = rms(noise.data() + 72000, 24000);
  for (size_t offset = 0; offset < noise.size(); offset += 480)
    process(noise.data() + offset, 480, -30.0f);
  CHECK(rms(noise.data() + 72000, 24000) < noise_before * 0.75);

  cycle_noise_reduction();
  reset();
  std::vector<int16_t> tone(96000);
  for (size_t offset = 0; offset < tone.size(); offset += 480) {
    for (size_t i = 0; i < 480; ++i) {
      const size_t n = offset + i;
      tone[n] = static_cast<int16_t>(8000.0 * std::sin(2.0 * 3.141592653589793 * 1000.0 * n / 48000.0) +
                                     1200.0 * std::sin(2.0 * 3.141592653589793 * 300.0 * n / 48000.0));
    }
    process(tone.data() + offset, 480, -30.0f);
  }
  CHECK(metrics().notch_active);
  CHECK(metrics().notch_hz >= 875 && metrics().notch_hz <= 1125);
  CHECK(rms(tone.data() + 72000, 24000) < 2500.0);

  cycle_squelch();
  cycle_squelch();
  reset();
  std::vector<int16_t> quiet(4800, 2000);
  for (size_t offset = 0; offset < quiet.size(); offset += 480)
    process(quiet.data() + offset, 480, -90.0f);
  CHECK(!metrics().squelch_open);
  CHECK(rms(quiet.data() + 2400, 2400) < 50.0);
  process(quiet.data(), 480, -30.0f);
  CHECK(metrics().squelch_open);

  std::vector<int16_t> concurrent(480, 1000);
  std::thread controls([] {
    for (int i = 0; i < 2000; ++i) {
      reset();
      toggle_auto_notch();
      cycle_squelch();
    }
  });
  for (int i = 0; i < 2000; ++i)
    process(concurrent.data(), concurrent.size(), -45.0f);
  controls.join();

  if (argc == 2) {
    std::vector<int16_t> capture = read_pcm16_wav(argv[1]);
    const double before = rms(capture.data(), capture.size());
    reset();
    apply_clean_preset();
    for (size_t offset = 0; offset < capture.size(); offset += 480)
      process(capture.data() + offset, std::min<size_t>(480, capture.size() - offset),
              -30.0f);
    const double after = rms(capture.data(), capture.size());
    CHECK(std::fabs(after - before) > 1.0);
    std::printf("capture_ab rms_before=%.1f rms_after=%.1f ratio=%.3f\n",
                before, after, after / before);
  }

  std::puts("shortwave_audio_dsp_tests: PASS");
  return 0;
}
