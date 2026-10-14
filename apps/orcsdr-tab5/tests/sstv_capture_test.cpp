// Host test for the decoder-input capture: stops at capacity, writes a well-formed 12 kHz mono WAV with the recorded samples.
#include "sstv_capture.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace cap = orcsdr::sstv_capture;
namespace {
void require(bool pass, const char* why) {
  if (!pass) {
    std::fprintf(stderr, "SSTV FAIL: %s\n", why);
    std::exit(1);
  }
}
bool collect(void* context, const void* data, size_t size) {
  auto* bytes = static_cast<std::vector<unsigned char>*>(context);
  const auto* p = static_cast<const unsigned char*>(data);
  bytes->insert(bytes->end(), p, p + size);
  return true;
}
unsigned u32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<unsigned>(p[3]) << 24); }
}  // namespace

int main() {
  require(cap::self_check(), "capture: self check");
  require(!cap::start(0) && !cap::start(cap::kMaxSeconds + 1), "capture: bad durations refused");
  require(cap::start(2), "capture: starts");
  int16_t block[777];
  for (int i = 0; i < 777; ++i) block[i] = static_cast<int16_t>(i * 7 - 2000);
  for (int i = 0; i < 40; ++i) cap::push(block, 777);
  require(cap::full() && !cap::recording() && cap::samples() == 2 * cap::kSampleRate, "capture: stops at capacity");
  std::vector<unsigned char> wav;
  require(cap::write_wav(collect, &wav), "capture: writes");
  require(wav.size() == 44 + 2 * cap::kSampleRate * 2, "capture: WAV size");
  require(std::memcmp(wav.data(), "RIFF", 4) == 0 && std::memcmp(wav.data() + 8, "WAVEfmt ", 8) == 0, "capture: RIFF/WAVE header");
  require(wav[20] == 1 && wav[22] == 1 && u32(&wav[24]) == cap::kSampleRate && wav[34] == 16, "capture: PCM, mono, 12 kHz, 16-bit");
  require(std::memcmp(wav.data() + 36, "data", 4) == 0 && u32(&wav[40]) == 2 * cap::kSampleRate * 2, "capture: data chunk length");
  const short first = static_cast<short>(wav[44] | (wav[45] << 8));
  require(first == block[0], "capture: first sample is the first one pushed");
  cap::release();
  require(!cap::write_wav(collect, &wav), "capture: nothing to write after release");
  std::puts("SSTV capture: PASS");
  return 0;
}
