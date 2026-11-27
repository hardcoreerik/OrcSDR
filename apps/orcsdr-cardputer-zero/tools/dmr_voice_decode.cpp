// Decodes DMR voice from a captured CU8 file to a WAV, exercising the whole chain:
// channeliser -> d1 receiver -> voice superframe -> de-interleave -> mbelib -> PCM.
//
// This is the end-to-end proof that the derived schedule is right: if the audio
// has speech-like energy and duration, every stage agrees.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <string>
#include <vector>

#include "dv_ambe.hpp"
#include "dv_channelizer.hpp"
#include "dv_voice.hpp"
#include "linux_audio.hpp"
#include "orcsdr_dmr/d1_bridge.hpp"

extern "C" {
#include "mbelib.h"
}

namespace {

constexpr int kFrameSamples = 160;
constexpr int kSampleRate = 8000;

struct DecoderState {
  orc::zero::dv::VoicePlayer player;
  unsigned groups = 0;
};

void on_voice(const orcsdr::dmr::d1::VoiceGroup& group, void* user) noexcept {
  auto* state = static_cast<DecoderState*>(user);
  if (!state) return;
  // 162 bytes = 18 frames of 9 bytes, three per burst, bursts A-F.
  state->player.decode_superframe(group.payload.data(), group.payload.size());
  ++state->groups;
}

void write_wav(const char* path, const std::vector<short>& pcm) {
  FILE* file = std::fopen(path, "wb");
  if (!file) return;
  const std::uint32_t data_bytes = std::uint32_t(pcm.size() * 2);
  const std::uint32_t rate = kSampleRate;
  const std::uint16_t channels = 1, bits = 16;
  const std::uint32_t byte_rate = rate * channels * bits / 8;
  const std::uint32_t riff = 36 + data_bytes;
  const std::uint32_t fmt_size = 16, fmt = 1;
  const std::uint16_t block_align = channels * bits / 8;
  std::fwrite("RIFF", 1, 4, file);
  std::fwrite(&riff, 4, 1, file);
  std::fwrite("WAVEfmt ", 1, 8, file);
  std::fwrite(&fmt_size, 4, 1, file);
  std::fwrite(&fmt, 2, 1, file);
  std::fwrite(&channels, 2, 1, file);
  std::fwrite(&rate, 4, 1, file);
  std::fwrite(&byte_rate, 4, 1, file);
  std::fwrite(&block_align, 2, 1, file);
  std::fwrite(&bits, 2, 1, file);
  std::fwrite("data", 1, 4, file);
  std::fwrite(&data_bytes, 4, 1, file);
  std::fwrite(pcm.data(), 2, pcm.size(), file);
  std::fclose(file);
}

// Round-trip check of the schedule: build a 72-bit frame from a payload with the
// inverse mapping, extract it, expect the same payload. No third-party data used.
int selftest() {
  std::uint32_t seed = 12345;
  int failures = 0;
  for (int trial = 0; trial < 1000; ++trial) {
    std::uint8_t payload[49];
    for (int i = 0; i < 49; ++i) {
      seed = seed * 1103515245u + 12345u;
      payload[i] = std::uint8_t((seed >> 16) & 1u);
    }
    std::uint8_t onair[72] = {0};
    for (int k = 0; k < 12; ++k) onair[4 * k] = payload[k];
    for (int k = 0; k < 7; ++k) onair[46 + 4 * k] = payload[24 + k];
    for (int k = 0; k < 18; ++k) onair[3 + 4 * k] = payload[31 + k];
    // C1 needs the keystream, which needs C0 in place - it is, above.
    for (int k = 1; k <= 12; ++k)
      onair[21 + 4 * k] = payload[11 + k] ^ orc::zero::dv::keystream_bit(payload, k);

    std::uint8_t back[49];
    orc::zero::dv::extract_ambe(onair, back);
    if (std::memcmp(back, payload, 49) != 0) ++failures;
  }
  std::printf("selftest: %d/1000 round-trips exact\n", 1000 - failures);
  return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) return selftest();
  if (argc < 2) {
    std::printf("usage: %s <capture.cu8> [out.wav]\n       %s --selftest\n", argv[0], argv[0]);
    return 2;
  }

  FILE* in = std::fopen(argv[1], "rb");
  if (!in) {
    std::printf("cannot open %s\n", argv[1]);
    return 2;
  }
  std::fseek(in, 0, SEEK_END);
  const long bytes = std::ftell(in);
  std::fseek(in, 0, SEEK_SET);
  const size_t count = size_t(bytes);
  std::vector<std::uint8_t> cu8(count);
  if (std::fread(cu8.data(), 1, count, in) != count) {
    std::fclose(in);
    std::printf("short read\n");
    return 2;
  }
  std::fclose(in);
  std::printf("cu8 %s: %zu pairs (%.1f s)\n", argv[1], count / 2, double(count / 2) / 240000.0);

  static orcsdr::dmr::d1::integration::Receiver receiver;
  receiver.reset();
  DecoderState state;
  receiver.set_voice_sink(on_voice, &state);

  orcsdr::dv::Channelizer channelizer;
  std::vector<orcsdr::dv::Iq48> baseband(2400 / 5 + 16);
  for (size_t offset = 0; offset + 2 <= cu8.size(); offset += 2400 * 2) {
    const size_t chunk = std::min<size_t>(2400, (cu8.size() - offset) / 2);
    const size_t out_pairs =
        channelizer.process_cu8(cu8.data() + offset, chunk, baseband.data(), baseband.size());
    if (!out_pairs) continue;
    const auto* samples =
        reinterpret_cast<const orcsdr::dmr::d1::ComplexSample*>(baseband.data());
    receiver.process(samples, out_pairs, nullptr, nullptr);
  }

  // Why did superframes qualify or not? These counters separate "the call was
  // short" from "qualification dropped it", which decides whether per-frame error
  // correction would change anything at all.
  {
    const auto& v = receiver.voice_stats();
    std::printf(
        "voice: anchors %llu  groups %llu  abandoned_sync %llu  abandoned_emb %llu  lc ok %llu bad %llu\n",
        (unsigned long long)v.anchors, (unsigned long long)v.groups,
        (unsigned long long)v.abandoned_sync, (unsigned long long)v.abandoned_emb,
        (unsigned long long)v.embedded_lc_ok, (unsigned long long)v.embedded_lc_bad);
  }
  if (!state.player.frames_decoded()) {
    std::printf("no voice frames decoded\n");
    return 1;
  }

  std::vector<short> pcm;
  std::vector<std::int16_t> upsampled(4800);
  for (;;) {
    const size_t got = state.player.take_upsampled(upsampled.data(), upsampled.size());
    if (!got) break;
    pcm.insert(pcm.end(), upsampled.begin(), upsampled.begin() + std::ptrdiff_t(got));
  }

  double sum_sq = 0, peak = 0;
  for (short value : pcm) {
    const double v = double(value);
    sum_sq += v * v;
    if (std::fabs(v) > peak) peak = std::fabs(v);
  }
  const double rms = pcm.empty() ? 0.0 : std::sqrt(sum_sq / double(pcm.size()));
  std::printf("groups %u  vocoder frames %llu  %.2f s at 48 kHz  rms %.1f  peak %.0f\n",
              state.groups, (unsigned long long)state.player.frames_decoded(),
              double(pcm.size()) / 48000.0, rms, peak);
  if (argc > 2) {
    write_wav(argv[2], pcm);
    std::printf("wrote %s\n", argv[2]);
  }

  // --play sends the same PCM to the codec, which is how the dashboard will do it.
  if (argc > 3 && std::strcmp(argv[3], "--play") == 0) {
    orc::zero::AudioSink sink;
    if (!sink.open("plughw:ES8389Audio,0", false)) {
      std::printf("audio open failed\n");
      return 1;
    }
    size_t offset = 0;
    while (offset < pcm.size()) {
      // AudioSink's scratch buffer is 4096 frames; asking for more is refused.
      const size_t chunk = std::min<size_t>(4000, pcm.size() - offset);
      if (!sink.write_mono(pcm.data() + offset, chunk, 100, false)) break;
      offset += chunk;
      ::usleep(100000);
    }
    sink.close();
    std::printf("played %zu samples through the codec\n", offset);
  }
  return rms > 1.0 ? 0 : 1;

}
