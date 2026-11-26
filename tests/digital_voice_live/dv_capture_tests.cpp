// Host tests for the development IQ capture: segment pipeline, ordering, gap accounting and the sidecar.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "dv_capture.hpp"

using namespace orcsdr::dv_capture;
namespace {
int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

// A deterministic stream: byte i of the stream is (i * 7 + 3) & 0xFF, so any part of a written file can be checked.
uint8_t stream_byte(uint64_t i) { return static_cast<uint8_t>((i * 7u + 3u) & 0xFFu); }

struct Rig {
  std::vector<uint8_t> mem = std::vector<uint8_t>(kMemBytes);
  Capture cap;
  std::vector<std::vector<uint8_t>> files;      // what the "card" received, in order
  std::vector<std::string> names;
  uint64_t sent = 0;
  uint32_t now = 1000;
  Rig() { cap.attach(mem.data()); }
  void feed(size_t bytes, uint32_t rate) {
    std::vector<uint8_t> blk(bytes);
    for (size_t i = 0; i < bytes; ++i) blk[i] = stream_byte(sent + i);
    cap.offer(blk.data(), blk.size(), rate);
    sent += bytes;
  }
  // The card: takes one segment per call (instantly) when `card_ready`.
  void card(bool card_ready) {
    if (!card_ready) return;
    const uint8_t* d = nullptr; size_t n = 0; char path[96];
    if (cap.next(&d, &n, path, sizeof(path))) {
      cap.write_started();
      files.emplace_back(d, d + n);
      names.emplace_back(path);
      cap.write_done(true);
    }
  }
};

void test_clean_capture_is_byte_exact() {
  Rig r;
  Config c; c.center_hz = 463250000; c.rate = 1536000; c.seconds = 5; c.utc = 1795000000;
  CHECK(r.cap.start(c, r.now));
  const size_t blk = 16384;
  const size_t total = 2 * kSegBytes + 4096;             // two full segments plus a partial
  for (size_t sent = 0; sent < total; sent += blk) {
    r.feed(std::min(blk, total - sent), 1536000);
    r.card(true);
    r.cap.service(r.now);
  }
  r.cap.stop();
  for (int i = 0; i < 10; ++i) { r.cap.service(r.now); r.card(true); }
  CHECK(r.cap.state() == State::done);
  CHECK(r.cap.bytes_dropped() == 0);
  CHECK(r.cap.bytes_captured() == total);
  CHECK(r.files.size() == 3);
  CHECK(r.files[0].size() == kSegBytes && r.files[1].size() == kSegBytes && r.files[2].size() == total - 2 * kSegBytes);
  uint64_t pos = 0; bool exact = true;
  for (auto& f : r.files) for (uint8_t b : f) { exact = exact && b == stream_byte(pos); ++pos; }
  CHECK(exact);
  CHECK(r.names[0].find("cap_1795000000_463250kHz_1536kSps_0000.cu8") != std::string::npos);
  CHECK(r.names[2].find("_0002.cu8") != std::string::npos);
}

void test_stalled_card_drops_are_recorded() {
  Rig r;
  Config c; c.center_hz = 152157500; c.rate = 2560000; c.seconds = 60;
  CHECK(r.cap.start(c, r.now));
  const size_t blk = 65536;
  size_t fed = 0;
  while (fed < 3 * kMemBytes) { r.feed(blk, 2560000); fed += blk; r.card(false); }     // the card never answers
  CHECK(r.cap.bytes_captured() == kMemBytes);                                               // exactly the memory's worth is kept
  CHECK(r.cap.bytes_dropped() == fed - kMemBytes);
  CHECK(r.cap.drop_events() == 1);                                                          // one long contiguous gap
  r.cap.stop();
  for (int i = 0; i < 40; ++i) { r.cap.service(r.now); r.card(true); }
  CHECK(r.cap.state() == State::done);
  CHECK(r.files.size() == kSegs);
  char json[4096];
  const size_t n = r.cap.sidecar(json, sizeof(json));
  CHECK(n > 0);
  std::string j(json, n);
  CHECK(j.find("\"bytes_dropped\":" + std::to_string(fed - kMemBytes)) != std::string::npos);
  CHECK(j.find("\"stream_offset\":" + std::to_string(kMemBytes)) != std::string::npos);
  CHECK(j.find("\"sample_rate_sps\":2560000") != std::string::npos);
}

void test_gaps_keep_stream_offsets() {
  Rig r;
  Config c; c.center_hz = 456000000; c.rate = 2048000; c.seconds = 60;
  CHECK(r.cap.start(c, r.now));
  // Fill every segment with the card stalled, then free one and send more: two separate gaps must be listed.
  const size_t half = kSegBytes / 2;
  for (size_t i = 0; i < kSegs; ++i) r.feed(kSegBytes, 2048000);
  r.feed(half, 2048000);                                      // lost (gap 1)
  r.card(true);                                               // one segment goes out, one frees up
  r.feed(half, 2048000);                                      // kept
  r.feed(kSegBytes, 2048000);                                 // half fits in the freed segment, the other half is lost (gap 2)
  CHECK(r.cap.drop_events() == 2);
  CHECK(r.cap.bytes_dropped() == kSegBytes);
}

void test_other_rates_ignored_and_idle_is_safe() {
  Rig r;
  uint8_t b[64]{};
  r.cap.offer(b, sizeof(b), 240000);                          // not capturing: nothing happens
  CHECK(r.cap.bytes_captured() == 0);
  Config c; c.center_hz = 100000000; c.rate = 2400000; c.seconds = 1;
  CHECK(r.cap.start(c, r.now));
  r.cap.offer(b, sizeof(b), 240000);                          // wrong rate: counted, not stored
  CHECK(r.cap.bytes_captured() == 0 && r.cap.wrong_rate_blocks() == 1);
  r.cap.offer(b, sizeof(b), 2400000);
  CHECK(r.cap.bytes_captured() == 64);
  CHECK(!r.cap.start(c, r.now));                              // already running
  Capture other; CHECK(!other.start(c, 0));                   // no memory attached
  Config bad = c; bad.seconds = 0; Capture third; third.attach(r.mem.data()); CHECK(!third.start(bad, 0));
  bad.seconds = kMaxSeconds + 1; CHECK(!third.start(bad, 0));
}

void test_time_limit_and_failed_write() {
  Rig r;
  Config c; c.center_hz = 463250000; c.rate = 1024000; c.seconds = 2;
  CHECK(r.cap.start(c, r.now));
  r.feed(100000, 1024000);
  r.now += 1999; r.cap.service(r.now); CHECK(r.cap.state() == State::capturing);
  r.now += 2;    r.cap.service(r.now); CHECK(r.cap.state() == State::draining);
  const uint8_t* d = nullptr; size_t n = 0; char path[96];
  CHECK(r.cap.next(&d, &n, path, sizeof(path)) && n == 100000);
  r.cap.write_started();
  r.cap.service(r.now); CHECK(r.cap.state() == State::draining);   // still writing
  r.cap.write_done(false);
  r.cap.service(r.now); CHECK(r.cap.state() == State::done);
  CHECK(r.cap.segments_failed() == 1 && r.cap.segments_written() == 0);
  char json[4096]; CHECK(r.cap.sidecar(json, sizeof(json)) > 0);
  CHECK(std::string(json).find("\"segments_failed\":1") != std::string::npos);
}
}  // namespace

int main() {
  test_clean_capture_is_byte_exact();
  test_stalled_card_drops_are_recorded();
  test_gaps_keep_stream_offsets();
  test_other_rates_ignored_and_idle_is_safe();
  test_time_limit_and_failed_write();
  if (g_fail) { std::printf("FAILED %d checks\n", g_fail); return 1; }
  std::puts("PASS dv_capture");
  return 0;
}
