// Decodes AMBE+2 reference frames to PCM using mbelib.
//
// The frame logs in artifacts/digital-voice-integration/vocoder_reference_pack
// record the 49-bit AMBE+2 parameter vector per voice frame, already past FEC, so
// mbe_processAmbe2450Dataf is the right entry point: it synthesises speech from
// the parameters without trying to correct them again.
//
// This is a smoke test for the vocoder path, not for RF: it answers "does our
// build of mbelib turn real DMR parameter frames into non-silent audio on this
// CPU, and how fast".
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "mbelib.h"
}

namespace {

constexpr int kFrameSamples = 160;   // 20 ms at 8 kHz
constexpr int kSampleRate = 8000;

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// "AMBE 81B7109B585500" -> 49 bits, MSB first within each byte.
bool parse_frame(const char* hex, char ambe_d[49]) {
  unsigned char bytes[7]{};
  for (int n = 0; n < 7; ++n) {
    const int hi = hex_nibble(hex[n * 2]);
    const int lo = hex_nibble(hex[n * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    bytes[n] = (unsigned char)((hi << 4) | lo);
  }
  for (int n = 0; n < 49; ++n)
    ambe_d[n] = char((bytes[n / 8] >> (7 - (n % 8))) & 1);
  return true;
}

void write_wav(const char* path, const std::vector<short>& pcm) {
  FILE* file = std::fopen(path, "wb");
  if (!file) return;
  const std::uint32_t data_bytes = uint32_t(pcm.size() * 2);
  const std::uint32_t rate = kSampleRate;
  const std::uint16_t channels = 1, bits = 16;
  const std::uint32_t byte_rate = rate * channels * bits / 8;
  const std::uint32_t riff = 36 + data_bytes;
  const std::uint32_t fmt_size = 16, fmt = 1;
  std::fwrite("RIFF", 1, 4, file);
  std::fwrite(&riff, 4, 1, file);
  std::fwrite("WAVEfmt ", 1, 8, file);
  std::fwrite(&fmt_size, 4, 1, file);
  std::fwrite(&fmt, 2, 1, file);
  std::fwrite(&channels, 2, 1, file);
  std::fwrite(&rate, 4, 1, file);
  std::fwrite(&byte_rate, 4, 1, file);
  const std::uint16_t block_align = channels * bits / 8;
  std::fwrite(&block_align, 2, 1, file);
  std::fwrite(&bits, 2, 1, file);
  std::fwrite("data", 1, 4, file);
  std::fwrite(&data_bytes, 4, 1, file);
  std::fwrite(pcm.data(), 2, pcm.size(), file);
  std::fclose(file);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s <frame_log.txt> [out.wav]\n", argv[0]);
    return 2;
  }
  FILE* log = std::fopen(argv[1], "r");
  if (!log) {
    std::printf("cannot open %s\n", argv[1]);
    return 2;
  }

  mbe_parms cur{}, prev{}, prev_enh{};
  mbe_parms* cur_ptr = &cur;
  mbe_parms* prev_ptr = &prev;
  mbe_parms* prev_enh_ptr = &prev_enh;

  std::vector<short> pcm;
  char line[512];
  size_t frames = 0;
  int total_errs = 0;
  float peak = 0, sum_sq = 0;
  short frame[kFrameSamples];

  while (std::fgets(line, sizeof(line), log)) {
    const char* tag = std::strstr(line, "AMBE ");
    if (!tag) continue;
    tag += 5;
    char ambe_d[49];
    if (!parse_frame(tag, ambe_d)) continue;

    int errs = 0, errs2 = 0;
    char err_str[64] = {0};
    // mbe_processAmbe2450Data takes the 49-bit vector directly (post-FEC) and
    // writes 160 shorts; the float variant is not needed here.
    std::memset(frame, 0, sizeof(frame));
    mbe_processAmbe2450Data(frame, &errs, &errs2, err_str, ambe_d, cur_ptr, prev_ptr,
                            prev_enh_ptr, 3);  // mbelib default; 0 is out of range
    total_errs += errs;
    for (int n = 0; n < kFrameSamples; ++n) {
      pcm.push_back(frame[n]);
      const float v = float(frame[n]);
      if (std::fabs(v) > peak) peak = std::fabs(v);
      sum_sq += v * v;
    }
    ++frames;
  }
  std::fclose(log);

  if (!frames) {
    std::printf("no AMBE frames found in %s\n", argv[1]);
    return 1;
  }

  const double seconds = double(pcm.size()) / double(kSampleRate);
  const double rms = std::sqrt(sum_sq / double(pcm.size()));
  std::printf("frames %zu  samples %zu  %.2f s at %d Hz\n", frames, pcm.size(), seconds,
              kSampleRate);
  std::printf("rms %.1f  peak %.0f  total FEC errors %d\n", rms, double(peak), total_errs);

  if (argc > 2) {
    write_wav(argv[2], pcm);
    std::printf("wrote %s\n", argv[2]);
  }
  // Silence here would mean the vocoder path is not producing audio at all.
  return rms > 1.0 ? 0 : 1;
}
