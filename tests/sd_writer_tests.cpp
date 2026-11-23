// Host tests for the background SD writer core (apps/orcsdr-tab5/ui/sd_writer.cpp).
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/sd_writer_tests.cpp apps/orcsdr-tab5/ui/sd_writer.cpp
#include "sd_writer.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace orcsdr::sd_writer;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

struct FakeCard {
  std::string path;
  std::vector<uint8_t> bytes;
  std::vector<size_t> write_sizes;
  std::vector<const uint8_t*> write_sources;
  bool open_fails = false;
  size_t accept_limit = SIZE_MAX;  // total bytes the card takes before it is "full"
  int opens = 0, closes = 0, pauses = 0, wakes = 0, stagings = 0, staging_frees = 0;
  void* staging = nullptr;
  bool give_staging = true;
} card;

int g_file_token = 0;
void* fake_open(const char* path) {
  ++card.opens;
  if (card.open_fails) return nullptr;
  card.path = path;
  card.bytes.clear();
  return &g_file_token;
}
size_t fake_write(void* file, const uint8_t* data, size_t size) {
  if (file != &g_file_token) return 0;
  card.write_sizes.push_back(size);
  card.write_sources.push_back(data);
  const size_t room = card.accept_limit > card.bytes.size() ? card.accept_limit - card.bytes.size() : 0;
  const size_t n = size < room ? size : room;
  card.bytes.insert(card.bytes.end(), data, data + n);
  return n;
}
bool fake_close(void*) { ++card.closes; return true; }
void* fake_alloc(size_t size) {
  ++card.stagings;
  if (!card.give_staging) return nullptr;
  card.staging = std::malloc(size);
  return card.staging;
}
void fake_free(void* p) { ++card.staging_frees; std::free(p); }
void fake_pause() { ++card.pauses; }
void fake_wake() { ++card.wakes; }

Platform make_platform(size_t chunk) {
  Platform p;
  p.open = fake_open;
  p.write = fake_write;
  p.close = fake_close;
  p.alloc_staging = fake_alloc;
  p.free_staging = fake_free;
  p.pause = fake_pause;
  p.wake = fake_wake;
  p.chunk_bytes = chunk;
  return p;
}

void reset_card() { card = FakeCard{}; }

std::vector<uint8_t> pattern(size_t n) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 7 + 3);
  return v;
}

void test_header_and_chunks() {
  reset_card();
  const Platform p = make_platform(1000);
  Writer w(p);
  const auto data = pattern(2500);
  const char header[] = "HDR";
  CHECK(w.result().state == State::idle && !w.busy());
  CHECK(w.submit("/orcsdr/x.bin", header, 3, data.data(), data.size()));
  CHECK(card.wakes == 1);
  CHECK(w.busy() && w.result().state == State::queued);
  CHECK(!w.submit("/orcsdr/y.bin", header, 3, data.data(), data.size()));  // one job at a time
  CHECK(w.run_once());
  CHECK(!w.run_once());  // nothing more to do
  const Result r = w.result();
  CHECK(r.state == State::done && r.failure == Failure::none);
  CHECK(r.written == 2503 && r.wanted == 2503);
  CHECK(card.path == "/orcsdr/x.bin" && card.opens == 1 && card.closes == 1);
  CHECK(card.bytes.size() == 2503);
  CHECK(std::memcmp(card.bytes.data(), "HDR", 3) == 0);
  CHECK(std::memcmp(card.bytes.data() + 3, data.data(), data.size()) == 0);
  // header, then 1000 + 1000 + 500, pausing between pieces but not after the last
  CHECK(card.write_sizes.size() == 4 && card.write_sizes[0] == 3 && card.write_sizes[1] == 1000 &&
        card.write_sizes[2] == 1000 && card.write_sizes[3] == 500);
  CHECK(card.pauses == 2);
  // the pieces came from the staging buffer, not the caller's data, and staging was given back
  CHECK(card.stagings == 1 && card.staging_frees == 1);
  CHECK(card.write_sources[1] != data.data() + 0 && card.write_sources[2] != data.data() + 1000);
  CHECK(!w.busy());
  CHECK(!w.submit("/orcsdr/y.bin", header, 3, data.data(), data.size()));  // not acknowledged yet
  w.acknowledge();
  CHECK(w.result().state == State::idle);
  CHECK(w.submit("/orcsdr/y.bin", header, 3, data.data(), data.size()));
}

void test_without_staging() {
  reset_card();
  card.give_staging = false;
  const Platform p = make_platform(1024);
  Writer w(p);
  const auto data = pattern(3000);
  CHECK(w.submit("/a", nullptr, 0, data.data(), data.size()));
  CHECK(w.run_once());
  CHECK(w.result().state == State::done && card.bytes == data);
  CHECK(card.write_sources[0] == data.data() && card.write_sources[1] == data.data() + 1024);
  CHECK(card.staging_frees == 0);
}

void test_header_only() {
  reset_card();
  const Platform p = make_platform(1024);
  Writer w(p);
  const char json[] = "{\"a\":1}\n";
  CHECK(w.submit("/m.json", json, std::strlen(json), nullptr, 0));
  CHECK(w.run_once());
  CHECK(w.result().state == State::done && w.result().written == std::strlen(json));
  CHECK(std::string(card.bytes.begin(), card.bytes.end()) == json);
  CHECK(card.stagings == 0 && card.pauses == 0);
}

void test_failures() {
  reset_card();
  const Platform p = make_platform(100);
  Writer w(p);
  const auto data = pattern(450);
  card.open_fails = true;
  CHECK(w.submit("/f", "h", 1, data.data(), data.size()));
  CHECK(w.run_once());
  Result r = w.result();
  CHECK(r.state == State::failed && r.failure == Failure::open && r.written == 0 && r.wanted == 451);
  CHECK(card.closes == 0);
  w.acknowledge();

  reset_card();
  card.accept_limit = 251;  // header plus 250 data bytes, then the card is full
  CHECK(w.submit("/f", "h", 1, data.data(), data.size()));
  CHECK(w.run_once());
  r = w.result();
  CHECK(r.state == State::failed && r.failure == Failure::write && r.written == 251 && r.wanted == 451);
  CHECK(card.closes == 1 && card.staging_frees == 1);  // closed and cleaned up even when short
  w.acknowledge();
  CHECK(w.result().state == State::idle);
}

void test_rejects_bad_jobs() {
  reset_card();
  const Platform p = make_platform(100);
  Writer w(p);
  const auto data = pattern(10);
  std::string long_path(kPathMax, 'p');
  CHECK(!w.submit(long_path.c_str(), nullptr, 0, data.data(), data.size()));
  CHECK(!w.submit("", nullptr, 0, data.data(), data.size()));
  CHECK(!w.submit(nullptr, nullptr, 0, data.data(), data.size()));
  CHECK(!w.submit("/x", nullptr, 0, nullptr, 0));        // nothing to write
  CHECK(!w.submit("/x", nullptr, 5, data.data(), 10));   // header length without a header
  CHECK(!w.submit("/x", data.data(), kHeaderMax + 1, nullptr, 0));
  CHECK(!w.submit("/x", nullptr, 0, nullptr, 10));       // data length without data
  CHECK(card.wakes == 0 && w.result().state == State::idle);
  std::string fits(kPathMax - 1, 'p');
  CHECK(w.submit(fits.c_str(), nullptr, 0, data.data(), data.size()));
}

// The caller and the worker on different threads, many jobs: the result is always complete when it is reported.
void test_threads() {
  reset_card();
  card.give_staging = false;
  Platform p = make_platform(64);
  p.pause = [] { std::this_thread::yield(); };
  p.wake = nullptr;
  Writer w(p);
  std::atomic<bool> stop{false};
  std::thread worker([&] {
    while (!stop.load()) {
      if (!w.run_once()) std::this_thread::yield();
    }
  });
  const auto data = pattern(1000);
  int ok = 0;
  for (int i = 0; i < 300; ++i) {
    while (!w.submit("/t", "H", 1, data.data(), data.size())) std::this_thread::yield();
    Result r;
    do { r = w.result(); } while (r.state != State::done && r.state != State::failed);
    if (r.state == State::done && r.written == 1001) ++ok;
    w.acknowledge();
  }
  stop.store(true);
  worker.join();
  CHECK(ok == 300);
}

}  // namespace

int main() {
  test_header_and_chunks();
  test_without_staging();
  test_header_only();
  test_failures();
  test_rejects_bad_jobs();
  test_threads();
  if (failures) {
    std::printf("sd_writer_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("sd_writer_tests: ok\n");
  return 0;
}
