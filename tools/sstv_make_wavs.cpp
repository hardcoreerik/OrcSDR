// Writes the colour ORC test pattern as a 48 kHz mono PCM16 SSTV WAV for each of the 14 supported modes.
//
//   g++ -std=c++17 -O2 -Iapps/orcsdr-tab5/ui -Iapps/orcsdr-tab5/tests tools/sstv_make_wavs.cpp -o sstv_make_wavs
//   ./sstv_make_wavs apps/orcsdr-tab5/main/sstv_testpattern_320x240.rgb out_dir [mode-vis-code ...]
//
// It reuses the host tests' independent transmitter model (tests/sstv_test_colour.hpp). The receiver itself is not
// linked: the two entry points the Signal helper calls are replaced by a recorder, so the audio goes to the file.
// The waveform is for cabled lab transmission only (see docs/sstv/README.md); peak amplitude 0.8 of full scale,
// which with 2.5 kHz FM deviation is the level the existing Robot 36 lab frames used.
#include "sstv_receiver.hpp"
#include "sstv_test_colour.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace orcsdr::sstv {
static std::vector<int16_t> g_audio;
void offer_audio_48k(const int16_t* samples, size_t count) { g_audio.insert(g_audio.end(), samples, samples + count); }
void service(size_t) {}
}  // namespace orcsdr::sstv

static void put32(std::FILE* f, uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc((v >> (8 * i)) & 255, f); }
static void put16(std::FILE* f, uint16_t v) { for (int i = 0; i < 2; ++i) std::fputc((v >> (8 * i)) & 255, f); }

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s pattern.rgb out_dir [vis ...]\n", argv[0]);
    return 2;
  }
  std::vector<uint8_t> rgb(320u * 240u * 3u);
  std::FILE* in = std::fopen(argv[1], "rb");
  if (!in || std::fread(rgb.data(), 1, rgb.size(), in) != rgb.size()) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }
  std::fclose(in);
  size_t count = 0;
  const sstv_test::ColourMode* modes = sstv_test::colour_modes(&count);
  for (size_t i = 0; i < count; ++i) {
    const auto& m = modes[i];
    if (argc > 3) {
      bool wanted = false;
      for (int a = 3; a < argc; ++a) wanted = wanted || std::atoi(argv[a]) == m.vis;
      if (!wanted) continue;
    }
    orcsdr::sstv::g_audio.clear();
    sstv_test::Signal sig;
    sig.amplitude = 0.8 * 32767.0;
    sstv_test::send_colour(sig, m, rgb.data());
    sig.silence(500);
    sig.flush();
    std::string name = m.name;
    for (auto& c : name) c = (c == ' ') ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string path = std::string(argv[2]) + "/" + name + ".wav";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
    const uint32_t bytes = static_cast<uint32_t>(orcsdr::sstv::g_audio.size() * 2);
    std::fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, 48000); put32(f, 96000); put16(f, 2); put16(f, 16);
    std::fwrite("data", 1, 4, f); put32(f, bytes);
    std::fwrite(orcsdr::sstv::g_audio.data(), 2, orcsdr::sstv::g_audio.size(), f);
    std::fclose(f);
    std::printf("%-12s vis=%u %ux%u %.1f s -> %s\n", m.name, m.vis, m.width, m.height,
                static_cast<double>(orcsdr::sstv::g_audio.size()) / 48000.0, path.c_str());
  }
  return 0;
}
