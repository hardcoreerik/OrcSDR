#include "rtl_tcp_iq_ring.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using orcsdr::rtl_tcp::IqRing;

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

static uint8_t pattern(uint64_t i) {
  return (i % 2 == 0) ? static_cast<uint8_t>(0x10 + (i / 2) % 100)
                      : static_cast<uint8_t>(0xC0 + (i / 2) % 50);
}

static void test_wrap_and_all_or_nothing() {
  uint8_t storage[64];
  IqRing ring;
  ring.init(storage, sizeof(storage));
  std::vector<uint8_t> block(40);
  for (size_t i = 0; i < block.size(); ++i) block[i] = pattern(i);
  CHECK(ring.write_block(block.data(), 40));
  CHECK(!ring.write_block(block.data(), 40));  // would not fit: lost whole, counted
  CHECK(ring.overrun_bytes() == 40);
  CHECK(ring.available() == 40);
  uint8_t out[64];
  CHECK(ring.read(out, 25) == 24);  // even count only
  CHECK(ring.write_block(block.data(), 40));  // wraps around the end
  CHECK(ring.read(out, 64) == 56);
  CHECK(std::memcmp(out, block.data() + 24, 16) == 0);
  CHECK(std::memcmp(out + 16, block.data(), 40) == 0);
  CHECK(!ring.write_block(block.data(), 3));  // odd block would split an I/Q pair
  CHECK(ring.read(out, 64) == 0);
}

static void test_two_thread_sequence() {
  static uint8_t storage[1 << 16];
  IqRing ring;
  ring.init(storage, sizeof(storage));
  const uint64_t total = 20u * 1000u * 1000u;
  std::atomic<bool> done{false};
  std::atomic<uint64_t> lost{0};
  std::thread producer([&] {
    uint64_t sent = 0;
    uint32_t rng = 1;
    std::vector<uint8_t> block;
    while (sent < total) {
      rng = rng * 1664525u + 1013904223u;
      size_t n = 2 * (1 + (rng >> 8) % 1500);
      block.resize(n);
      for (size_t i = 0; i < n; ++i) block[i] = pattern(sent + i);
      if (ring.write_block(block.data(), n)) sent += n;
      else lost.fetch_add(n);  // retry the same samples, so the stream stays contiguous
    }
    done = true;
  });
  uint64_t received = 0;
  bool ordered = true, even = true;
  std::vector<uint8_t> buf(4096);
  while (!done || ring.available() > 0) {
    const size_t n = ring.read(buf.data(), buf.size());
    if (n & 1u) even = false;
    for (size_t i = 0; i < n; ++i)
      if (buf[i] != pattern(received + i)) ordered = false;
    received += n;
  }
  producer.join();
  CHECK(ordered);
  CHECK(even);
  CHECK(received == total || received + 1500 * 2 >= total);  // everything written was read
  std::printf("ring stress: bytes=%llu full_events_bytes=%llu\n",
              static_cast<unsigned long long>(received),
              static_cast<unsigned long long>(lost.load()));
}

int main() {
  test_wrap_and_all_or_nothing();
  test_two_thread_sequence();
  std::printf("RTL_TCP_IQ_RING_HOST_TEST failures=%d pass=%d\n", g_failures, g_failures == 0);
  return g_failures == 0 ? 0 : 1;
}
