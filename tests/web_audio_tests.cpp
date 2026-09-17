#include "web_audio_buffer.hpp"
#include "web_audio_protocol.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using orcsdr::web_console::AudioBuffer;

int main() {
  AudioBuffer history;
  const int16_t first[] = {11, 12, 13, 14};
  int16_t output[4]{};
  history.append(first, 4);
  auto fast = history.subscribe();
  auto slow = history.subscribe();
  assert(history.read(fast, output, 4).count == 0); // No stale subscription audio.
  const int16_t next[] = {21, 22, 23, 24};
  history.append(next, 4);
  auto result = history.read(fast, output, 2);
  assert(result.count == 2 && result.position == 4 && result.dropped == 0 &&
         result.remaining == 2);
  assert(output[0] == 21 && output[1] == 22);
  result = history.read(slow, output, 4);
  assert(result.count == 4 && output[0] == 21 && output[3] == 24);
  result = history.read(fast, output, 4);
  assert(result.count == 2 && output[0] == 23 && output[1] == 24);

  // Overflow affects only a lagging reader, with exact loss accounting.
  std::array<int16_t, AudioBuffer::capacity> full{};
  for (size_t i = 0; i < full.size(); ++i) full[i] = static_cast<int16_t>(i);
  history.append(full.data(), full.size());
  fast = history.subscribe();
  history.append(first, 4);
  result = history.read(slow, output, 4);
  assert(result.count == 4 && result.dropped == 4 && result.discontinuity);
  assert(output[0] == 4 && output[3] == 7);
  result = history.read(fast, output, 4);
  assert(result.dropped == 0 && !result.discontinuity && output[0] == 11);

  // Invalid calls neither dereference null nor consume samples.
  history.append(next, 4);
  assert(history.read(fast, nullptr, 4).count == 0);
  assert(history.read(fast, output, 0).count == 0);
  history.append(nullptr, 100);
  result = history.read(fast, output, 4);
  assert(result.count == 4 && output[0] == 21);

  // A reset invalidates every old station sample, including an idle reader.
  const auto old_generation = result.generation;
  history.reset();
  history.append(first, 4);
  result = history.read(slow, output, 4);
  assert(result.count == 4 && result.discontinuity);
  assert(result.generation != old_generation && output[0] == 11);
  assert(history.read(slow, output, 4).count == 0);

  // Oversized producer batches preserve the newest history and count all loss.
  history.reset();
  auto oversized = history.subscribe();
  std::array<int16_t, AudioBuffer::capacity + 3> large{};
  large[3] = 31; large[4] = 32; large[5] = 33; large[6] = 34;
  history.append(large.data(), large.size());
  result = history.read(oversized, output, 4);
  assert(result.count == 4 && result.dropped == 3 && result.discontinuity);
  assert(output[0] == 31 && output[3] == 34);
  // Wire bytes must not depend on native struct packing or host byte order.
  using namespace orcsdr::web_console;
  std::array<uint8_t, kAudioPacketBytes> packet{};
  const int16_t pcm[] = {-32768, -1, 0, 32767};
  assert(encode_audio_packet(packet.data(), packet.size(), 0x12345678,
                             0x0102030405060708ULL, true, pcm, 4) == 40);
  const uint8_t header[] = {
      'O', 'R', 'C', 'A', 1, 0, 32, 0, 0x78, 0x56, 0x34, 0x12,
      8, 7, 6, 5, 4, 3, 2, 1, 0x80, 0xbb, 0, 0, 4, 0, 1, 1,
      1, 0, 0, 0};
  assert(std::memcmp(packet.data(), header, sizeof(header)) == 0);
  const uint8_t payload[] = {0, 0x80, 0xff, 0xff, 0, 0, 0xff, 0x7f};
  assert(std::memcmp(packet.data() + 32, payload, sizeof(payload)) == 0);
  assert(encode_audio_packet(nullptr, 40, 1, 0, false, pcm, 4) == 0);
  assert(encode_audio_packet(packet.data(), 39, 1, 0, false, pcm, 4) == 0);
  assert(encode_audio_packet(packet.data(), packet.size(), 1, 0, false, nullptr, 4) == 0);
  assert(encode_audio_packet(packet.data(), packet.size(), 1, 0, false, pcm, 0) == 0);
  assert(encode_audio_packet(packet.data(), packet.size(), 1, 0, false, pcm, 961) == 0);
  std::puts("WEB_AUDIO_BUFFER_PROTOCOL_OK");
}
