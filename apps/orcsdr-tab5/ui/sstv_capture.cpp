#include "sstv_capture.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace orcsdr::sstv_capture {
namespace {
int16_t* g_buffer = nullptr;
size_t g_capacity = 0, g_count = 0;
bool g_recording = false;

void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i)); }

void* allocate(size_t bytes) {
#ifdef ESP_PLATFORM
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}
void deallocate(void* p) {
#ifdef ESP_PLATFORM
  heap_caps_free(p);
#else
  std::free(p);
#endif
}
}  // namespace

bool start(uint32_t seconds) {
  if (seconds == 0 || seconds > kMaxSeconds) return false;
  const size_t wanted = static_cast<size_t>(seconds) * kSampleRate;
  if (!g_buffer || g_capacity != wanted) {
    // Allocate the replacement first: a failed request must not destroy the capture that is already held.
    int16_t* fresh = static_cast<int16_t*>(allocate(wanted * sizeof(int16_t)));
    if (!fresh) return false;
    release();
    g_buffer = fresh;
    g_capacity = wanted;
  }
  g_count = 0;
  g_recording = true;
  return true;
}
bool recording() { return g_recording; }
void push(const int16_t* samples, size_t count) {
  if (!g_recording || !g_buffer || !samples) return;
  const size_t room = g_capacity - g_count;
  const size_t n = std::min(room, count);
  std::memcpy(g_buffer + g_count, samples, n * sizeof(int16_t));
  g_count += n;
  if (g_count >= g_capacity) g_recording = false;
}
void stop() { g_recording = false; }
size_t samples() { return g_count; }
bool full() { return g_buffer && g_count >= g_capacity; }

void header(uint8_t out[44]) {
  const uint32_t bytes = static_cast<uint32_t>(g_count * sizeof(int16_t));
  const uint8_t base[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0};
  std::memset(out, 0, 44);
  std::memcpy(out, base, 24);
  put32(out + 4, 36 + bytes);
  put32(out + 24, kSampleRate);
  put32(out + 28, kSampleRate * 2);
  put16(out + 32, 2);
  put16(out + 34, 16);
  std::memcpy(out + 36, "data", 4);
  put32(out + 40, bytes);
}
const uint8_t* pcm() { return reinterpret_cast<const uint8_t*>(g_buffer); }
size_t pcm_bytes() { return g_count * sizeof(int16_t); }
bool write_wav(Writer writer, void* context) {
  if (!writer || !g_buffer || g_count == 0) return false;
  const uint32_t bytes = static_cast<uint32_t>(g_count * sizeof(int16_t));
  uint8_t header_bytes[44];
  header(header_bytes);
  if (!writer(context, header_bytes, sizeof(header_bytes))) return false;
  // The device is little-endian, so the samples go out as they are in memory.
  size_t done = 0;
  while (done < bytes) {
    const size_t chunk = std::min<size_t>(4096, bytes - done);
    if (!writer(context, reinterpret_cast<const uint8_t*>(g_buffer) + done, chunk)) return false;
    done += chunk;
  }
  return true;
}
void release() {
  g_recording = false;
  if (g_buffer) deallocate(g_buffer);
  g_buffer = nullptr;
  g_capacity = g_count = 0;
}

size_t held_bytes() { return g_buffer ? g_capacity * sizeof(int16_t) : 0; }

bool self_check() {
  release();
  if (start(0) || start(kMaxSeconds + 1)) return false;
  if (!start(1)) return false;
  int16_t block[1000];
  for (int i = 0; i < 1000; ++i) block[i] = static_cast<int16_t>(i);
  for (int i = 0; i < 20; ++i) push(block, 1000);   // more than a second: it must stop at the capacity
  const bool ok = full() && !recording() && samples() == kSampleRate;
  release();
  return ok;
}

}  // namespace orcsdr::sstv_capture
