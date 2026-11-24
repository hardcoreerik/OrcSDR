#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "iq_transfer_engine.hpp"
#include "mbedtls/sha256.h"
#include "sd_transfer.hpp"

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::string g_text_out;
std::vector<uint8_t> g_bin_out;
std::vector<uint8_t> g_capture;
bool g_capture_ok = true;
bool g_null_data = false;
uint32_t g_generation = 1;

void reset_state() {
  g_text_out.clear();
  g_bin_out.clear();
  g_capture.clear();
  g_capture_ok = true;
  g_null_data = false;
  g_generation = 1;
  orcsdr::test_sha::fail_start = false;
  orcsdr::test_sha::fail_update = false;
  orcsdr::test_sha::fail_finish = false;
}

const orcsdr::iq_transfer_engine::Hooks kHooks{
    [](const char* t) { g_text_out += t; },
    [](const char* t) { g_text_out += t; g_text_out += '\n'; },
    [](const uint8_t* v, size_t n) {
      static const char hex[] = "0123456789abcdef";
      for (size_t i = 0; i < n; ++i) {
        g_text_out.push_back(hex[v[i] >> 4]);
        g_text_out.push_back(hex[v[i] & 0xf]);
      }
    },
    [](const uint8_t* d, size_t n) -> size_t {
      g_bin_out.insert(g_bin_out.end(), d, d + n);
      return n;
    },
    [](uint8_t*, size_t, uint32_t) -> size_t { return 0; },
    [](const char*, uint8_t*, size_t) -> bool { return true; },
    [](size_t* bytes) -> bool {
      *bytes = g_capture.size();
      return g_capture_ok && !g_capture.empty();
    },
    []() -> size_t { return g_capture.size(); },
    []() -> const uint8_t* { return g_null_data ? nullptr : g_capture.data(); },
    []() -> uint32_t { return g_generation; },
    []() -> size_t { return 4 * 1024 * 1024; },
    []() -> uint32_t { return 250000; },
    []() -> bool { return false; },
    []() -> bool { return true; },
    []() -> uint8_t* { return g_capture.data(); },
    [](size_t, uint8_t, uint32_t, uint32_t) -> bool { return true; },
};

}  // namespace

int main() {
  using namespace orcsdr::iq_transfer_engine;

  // 1. Multi-chunk normal IQ download completes cleanly.
  reset_state();
  const size_t full_len = orcsdr::sd_transfer::kGetChunkBytes * 2 + 17;
  g_capture.resize(full_len);
  for (size_t i = 0; i < full_len; ++i) g_capture[i] = static_cast<uint8_t>(i & 0xff);
  get_begin(kHooks);
  CHECK(get_active());
  CHECK(g_text_out.find("RTL_IQ_GET_READY") == 0);
  while (get_active()) get_chunk(kHooks);
  CHECK(!get_active());
  CHECK(g_bin_out == g_capture);
  CHECK(g_text_out.find("RTL_IQ_GET_DONE bytes=" + std::to_string(full_len)) != std::string::npos);

  // 2. Capture shrinks mid-download below sent: must abort cleanly without underflow/OOB read.
  reset_state();
  g_capture.assign(orcsdr::sd_transfer::kGetChunkBytes + 512, 0x5a);
  get_begin(kHooks);
  CHECK(get_active());
  get_chunk(kHooks);  // sends first 2048 bytes
  CHECK(get_active());
  g_capture.resize(128);  // shrinks far below sent (2048) and size (2560)
  g_text_out.clear();
  get_chunk(kHooks);
  CHECK(!get_active());
  CHECK(g_text_out == "RTL_IQ_GET_ERROR capture_changed\n");

  // 3. Capture shrinks below initial size while still > sent: must also abort with capture_changed.
  reset_state();
  g_capture.assign(orcsdr::sd_transfer::kGetChunkBytes * 2, 0x33);
  get_begin(kHooks);
  get_chunk(kHooks);
  g_capture.resize(orcsdr::sd_transfer::kGetChunkBytes + 10);
  g_text_out.clear();
  get_chunk(kHooks);
  CHECK(!get_active());
  CHECK(g_text_out == "RTL_IQ_GET_ERROR capture_changed\n");

  // 4. Null capture pointer aborts cleanly.
  reset_state();
  g_capture.assign(1024, 0x77);
  get_begin(kHooks);
  g_null_data = true;
  g_text_out.clear();
  get_chunk(kHooks);
  CHECK(!get_active());
  CHECK(g_text_out == "RTL_IQ_GET_ERROR capture_changed\n");

  // 5. Zero-byte capture_ready refused in get_begin.
  reset_state();
  g_capture.clear();
  get_begin(kHooks);
  CHECK(!get_active());
  CHECK(g_text_out == "RTL_IQ_GET_ERROR capture_not_ready\n");

  // 6. SHA update failure during get_chunk aborts cleanly with sha_update.
  reset_state();
  g_capture.assign(1024, 0x42);
  get_begin(kHooks);
  orcsdr::test_sha::fail_update = true;
  g_text_out.clear();
  get_chunk(kHooks);
  CHECK(!get_active());
  CHECK(g_text_out == "RTL_IQ_GET_ERROR sha_update\n");

  // 7. Capture replaced with same byte count mid-download aborts cleanly on generation mismatch.
  reset_state();
  g_capture.assign(orcsdr::sd_transfer::kGetChunkBytes * 2, 0x24);
  get_begin(kHooks);
  CHECK(get_active());
  get_chunk(kHooks);
  CHECK(get_active());
  ++g_generation;
  std::fill(g_capture.begin(), g_capture.end(), 0x99);
  g_text_out.clear();
  get_chunk(kHooks);
  CHECK(!get_active());
  CHECK(g_text_out == "RTL_IQ_GET_ERROR capture_changed\n");

  if (failures == 0) std::printf("iq_transfer_engine: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
