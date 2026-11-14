// Host tests for the RDS multiplex capture module (apps/orcsdr-tab5/ui/rds_capture.cpp) and its save through the
// background SD writer. Expected replies are typed from the code this replaced in main.cpp.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/rds_capture_tests.cpp apps/orcsdr-tab5/ui/rds_capture.cpp apps/orcsdr-tab5/ui/sd_writer.cpp
//   apps/orcsdr-tab5/ui/memory_reclaim.cpp
#include "rds_capture.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "memory_reclaim.hpp"

using namespace orcsdr;
using namespace orcsdr::rds_capture;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// ---- fake device ----
std::vector<std::string> g_writes;  // every write, as the serial port would see it
bool g_fm = true;
uint32_t g_freq = 96100000;
uint32_t g_now = 4242;
bool g_alloc_fails = false;
int g_allocs = 0, g_frees = 0;
bool g_sd = true;
std::set<std::string> g_existing;
std::vector<std::string> g_mkdirs, g_removed;

struct CardFile { std::string bytes; };
std::map<std::string, CardFile> g_card;
std::string g_open_path;
bool g_open_fails_for_json = false, g_open_fails_for_raw = false;
size_t g_card_room = SIZE_MAX;

void w(const char* t) { g_writes.emplace_back(t); }
void wl(const char* t) { g_writes.emplace_back(t); g_writes.emplace_back("\n"); }
int16_t* alloc(size_t bytes) {
  if (g_alloc_fails) return nullptr;
  ++g_allocs;
  return static_cast<int16_t*>(std::malloc(bytes));
}
void release(int16_t* p) { ++g_frees; std::free(p); }

int g_token = 0;
void* card_open(const char* path) {
  const std::string p(path);
  const bool json = p.size() > 5 && p.compare(p.size() - 5, 5, ".json") == 0;
  if ((json && g_open_fails_for_json) || (!json && g_open_fails_for_raw)) return nullptr;
  g_open_path = p;
  g_card[p].bytes.clear();
  return &g_token;
}
size_t card_write(void*, const uint8_t* d, size_t n) {
  size_t used = 0;
  for (auto& f : g_card) used += f.second.bytes.size();
  const size_t room = g_card_room > used ? g_card_room - used : 0;
  const size_t k = n < room ? n : room;
  g_card[g_open_path].bytes.append(reinterpret_cast<const char*>(d), k);
  return k;
}
bool card_close(void*) { return true; }

sd_writer::Platform make_platform() {
  sd_writer::Platform p;
  p.open = card_open;
  p.write = card_write;
  p.close = card_close;
  p.chunk_bytes = 64 * 1024;
  return p;  // no wake: the test plays the worker by calling run_once()
}
const sd_writer::Platform kPlatform = make_platform();
sd_writer::Writer g_writer(kPlatform);

Hooks make_hooks() {
  Hooks h;
  h.write = w;
  h.write_line = wl;
  h.fm_selected = [] { return g_fm; };
  h.frequency_hz = [] { return g_freq; };
  h.now_ms = [] { return g_now; };
  h.alloc = alloc;
  h.release = release;
  h.sd_ready = [] { return g_sd; };
  h.sd_state = [] { return g_sd ? "ready" : "missing"; };
  h.exists = [](const char* p) { return g_existing.count(p) > 0 || g_card.count(p) > 0; };
  h.mkdir = [](const char* p) { g_mkdirs.emplace_back(p); return true; };
  h.remove = [](const char* p) { g_removed.emplace_back(p); g_card.erase(p); return true; };
  h.wait = [] {};
  h.writer = []() -> sd_writer::Writer& { return g_writer; };
  h.scale_radians_per_lsb = 1.0 / (32767.0 / 3.14159265358979323846);
  return h;
}
const Hooks kHooks = make_hooks();

void reset() {
  if (buffer()) std::free(const_cast<void*>(buffer()));
  reset_for_tests();
  memory_reclaim::clear_for_tests();
  if (g_writer.result().state != sd_writer::State::idle) {
    (void)g_writer.run_once();
    g_writer.acknowledge();
  }
  g_writes.clear();
  g_fm = true;
  g_freq = 96100000;
  g_now = 4242;
  g_alloc_fails = false;
  g_allocs = g_frees = 0;
  g_sd = true;
  g_existing.clear();
  g_mkdirs.clear();
  g_removed.clear();
  g_card.clear();
  g_open_fails_for_json = g_open_fails_for_raw = false;
  g_card_room = SIZE_MAX;
}

std::string joined() {
  std::string s;
  for (const auto& x : g_writes) s += x;
  return s;
}

// The DSP block: fills `n` samples (values 0,1,2,...) through the tap, as main.cpp's demodulator does.
void dsp_block(size_t n) {
  Tap tap;
  if (!tap_begin(&tap)) return;
  for (size_t i = 0; i < n && tap.write < tap.capacity; ++i) {
    tap.data[tap.write] = static_cast<int16_t>(tap.write & 0x7fff);
    ++tap.write;
  }
  tap_end(tap, kHooks);
}

void finish_save() {  // the worker and the main loop, until the save is over
  for (int i = 0; i < 10 && saving(); ++i) {
    (void)g_writer.run_once();
    service(kHooks);
  }
}

void test_start_refusals() {
  reset();
  g_fm = false;
  CHECK(!start(kHooks));
  CHECK(joined() == "RTL_RDS_CAPTURE_ERROR fm_mode_required\n");
  CHECK(g_writes.size() == 2);  // println: text then newline
  CHECK(g_allocs == 0);
  reset();
  g_alloc_fails = true;
  CHECK(!start(kHooks));
  CHECK(joined() == "RTL_RDS_CAPTURE_ERROR no_psram_buffer\n");
}

void test_capture_and_save() {
  reset();
  CHECK(start(kHooks));
  CHECK(joined() == "RTL_RDS_CAPTURE_START samples=1920000 seconds=8 rate=240000 frequency_hz=96100000\n");
  CHECK(g_writes.size() == 1);  // printf: one write
  g_writes.clear();
  CHECK(start(kHooks));
  CHECK(joined() == "RTL_RDS_CAPTURE_ALREADY\n");
  CHECK(g_allocs == 1);
  CHECK(held_bytes() == 0 && release_buffer() == 0);  // running: never released
  g_writes.clear();
  dsp_block(1000);
  dsp_block(500);
  status_print(kHooks);
  CHECK(joined() ==
        "RTL_RDS_CAPTURE_STATUS active=true ready=false samples=1500 seconds=0.006 max_seconds=8 rate=240000 "
        "frequency_hz=96100000 last_path=\"none\" sd=ready\n");
  g_writes.clear();

  CHECK(stop_and_export(kHooks));
  CHECK(joined() == "RTL_RDS_CAPTURE_STOP samples=1500 seconds=0.006\n");  // answers at once
  CHECK(saving());
  CHECK(held_bytes() == 0 && release_buffer() == 0);  // being saved: never released
  g_writes.clear();
  dsp_block(100);  // the capture is stopped: the tap stays closed
  CHECK(g_writes.empty());
  CHECK(stop_and_export(kHooks));
  CHECK(joined() == "RTL_RDS_CAPTURE_STOP busy=saving\n");
  g_writes.clear();
  CHECK(!start(kHooks));
  CHECK(joined() == "RTL_RDS_CAPTURE_ERROR saving\n");
  g_writes.clear();

  service(kHooks);  // the writer has not run: nothing yet
  CHECK(g_writes.empty());
  finish_save();
  CHECK(!saving());
  CHECK(joined() ==
        "RTL_RDS_CAPTURE_SAVED path=\"/orcsdr/rds_debug/001_96100000_mpx.s16\" "
        "metadata=\"/orcsdr/rds_debug/001_96100000_mpx.json\" samples=1500 rate=240000\n");
  CHECK(g_mkdirs.size() == 2 && g_mkdirs[0] == "/orcsdr" && g_mkdirs[1] == "/orcsdr/rds_debug");
  const std::string& raw = g_card["/orcsdr/rds_debug/001_96100000_mpx.s16"].bytes;
  CHECK(raw.size() == 3000);
  CHECK(raw.size() == 3000 && raw[2] == 1 && raw[3] == 0 && raw[2998] == static_cast<char>(1499 & 0xff));
  char expected_json[256];
  std::snprintf(expected_json, sizeof(expected_json),
                "{\"format\":\"s16le_mpx\",\"sample_rate_hz\":240000,\"frequency_hz\":96100000,"
                "\"samples\":1500,\"scale_radians_per_lsb\":%.10g,\"start_uptime_ms\":4242}\n",
                kHooks.scale_radians_per_lsb);
  CHECK(g_card["/orcsdr/rds_debug/001_96100000_mpx.json"].bytes == expected_json);

  g_writes.clear();
  status_print(kHooks);
  CHECK(joined() ==
        "RTL_RDS_CAPTURE_STATUS active=false ready=true samples=1500 seconds=0.006 max_seconds=8 rate=240000 "
        "frequency_hz=96100000 last_path=\"/orcsdr/rds_debug/001_96100000_mpx.s16\" sd=ready\n");

  // saved: the buffer may go, through the registry like RTL_MEM RECLAIM
  CHECK(held_bytes() == kBufferBytes);
  size_t n = 0;
  memory_reclaim::Report out[memory_reclaim::kMaxHolders];
  CHECK(memory_reclaim::reclaim_all(out, memory_reclaim::kMaxHolders, &n) == kBufferBytes);
  CHECK(n == 1 && std::strcmp(out[0].name, "RDS capture") == 0);
  CHECK(buffer() == nullptr && g_frees == 1);
  g_writes.clear();
  status_print(kHooks);
  CHECK(joined() ==
        "RTL_RDS_CAPTURE_STATUS active=false ready=false samples=0 seconds=0.000 max_seconds=8 rate=240000 "
        "frequency_hz=96100000 last_path=\"/orcsdr/rds_debug/001_96100000_mpx.s16\" sd=ready\n");
  g_writes.clear();
  CHECK(!stop_and_export(kHooks));
  CHECK(joined() == "RTL_RDS_CAPTURE_STOP empty\n");

  // the next capture allocates again and numbers the next file
  g_writes.clear();
  CHECK(start(kHooks) && g_allocs == 2 && buffer() != nullptr);
  dsp_block(10);
  CHECK(stop_and_export(kHooks));
  finish_save();
  CHECK(g_card.count("/orcsdr/rds_debug/002_96100000_mpx.s16") == 1);
}

void test_full_buffer() {
  reset();
  CHECK(start(kHooks));
  g_writes.clear();
  for (int i = 0; i < 200 && g_writes.empty(); ++i) dsp_block(10000);
  CHECK(joined() == "RTL_RDS_CAPTURE_FULL samples=1920000 seconds=8 note=send_stop_to_export\n");
  g_writes.clear();
  dsp_block(10000);  // printed once
  CHECK(g_writes.empty());
  status_print(kHooks);
  CHECK(joined().find("active=false ready=true samples=1920000 seconds=8.000") != std::string::npos);
  CHECK(held_bytes() == 0);  // full but not saved: kept
  g_writes.clear();
  CHECK(stop_and_export(kHooks));
  CHECK(joined() == "RTL_RDS_CAPTURE_STOP samples=1920000 seconds=8.000\n");
  finish_save();
  CHECK(g_card["/orcsdr/rds_debug/001_96100000_mpx.s16"].bytes.size() == kBufferBytes);
}

void test_skips_existing_names() {
  reset();
  g_existing = {"/orcsdr/rds_debug/001_96100000_mpx.s16", "/orcsdr/rds_debug/002_96100000_mpx.s16"};
  CHECK(start(kHooks));
  dsp_block(4);
  CHECK(stop_and_export(kHooks));
  finish_save();
  CHECK(g_card.count("/orcsdr/rds_debug/003_96100000_mpx.s16") == 1);
  CHECK(g_card.count("/orcsdr/rds_debug/003_96100000_mpx.json") == 1);
}

void test_no_card() {
  reset();
  g_sd = false;
  CHECK(start(kHooks));
  dsp_block(800);
  g_writes.clear();
  CHECK(!stop_and_export(kHooks));
  CHECK(joined() ==
        "RTL_RDS_CAPTURE_STOP samples=800 seconds=0.003\n"
        "RTL_RDS_CAPTURE_PSRAM_HOLD samples=800 note=insert_sd_then_stop_again\n");
  CHECK(!saving() && g_mkdirs.empty());
  CHECK(held_bytes() == 0 && release_buffer() == 0);  // unsaved: kept for the next STOP
  g_sd = true;  // card inserted: STOP again saves it
  g_writes.clear();
  CHECK(stop_and_export(kHooks));
  finish_save();
  CHECK(joined().find("RTL_RDS_CAPTURE_SAVED path=\"/orcsdr/rds_debug/001_96100000_mpx.s16\"") != std::string::npos);
  CHECK(held_bytes() == kBufferBytes);
}

void test_card_errors() {
  reset();
  g_open_fails_for_raw = true;
  CHECK(start(kHooks));
  dsp_block(100);
  CHECK(stop_and_export(kHooks));
  g_writes.clear();
  finish_save();
  CHECK(joined() ==
        "RTL_RDS_CAPTURE_ERROR raw_open_failed\n"
        "RTL_RDS_CAPTURE_PSRAM_HOLD samples=100 note=insert_sd_then_stop_again\n");
  CHECK(held_bytes() == 0);

  reset();
  g_card_room = 150;  // the card fills after 150 bytes of 200
  CHECK(start(kHooks));
  dsp_block(100);
  CHECK(stop_and_export(kHooks));
  g_writes.clear();
  finish_save();
  CHECK(joined() ==
        "RTL_RDS_CAPTURE_ERROR short_write got=150 want=200\n"
        "RTL_RDS_CAPTURE_PSRAM_HOLD samples=100 note=insert_sd_then_stop_again\n");
  CHECK(g_removed.size() == 1 && g_removed[0] == "/orcsdr/rds_debug/001_96100000_mpx.s16");

  reset();
  g_open_fails_for_json = true;
  CHECK(start(kHooks));
  dsp_block(100);
  CHECK(stop_and_export(kHooks));
  g_writes.clear();
  finish_save();
  CHECK(joined() ==
        "RTL_RDS_CAPTURE_ERROR metadata_open_failed raw=\"/orcsdr/rds_debug/001_96100000_mpx.s16\"\n"
        "RTL_RDS_CAPTURE_PSRAM_HOLD samples=100 note=insert_sd_then_stop_again\n");
}

// Another feature holds the writer: the save waits its turn and then runs.
void test_writer_busy() {
  reset();
  static const char other[] = "x";
  CHECK(g_writer.submit("/other.bin", nullptr, 0, other, 1));
  CHECK(start(kHooks));
  dsp_block(10);
  CHECK(stop_and_export(kHooks));
  g_writes.clear();
  service(kHooks);
  CHECK(saving() && g_writes.empty());
  CHECK(g_writer.run_once());  // the other job
  g_writer.acknowledge();
  finish_save();
  CHECK(joined().find("RTL_RDS_CAPTURE_SAVED") == 0);
  CHECK(g_card.count("/other.bin") == 1);
}

}  // namespace

int main() {
  test_start_refusals();
  test_capture_and_save();
  test_full_buffer();
  test_skips_existing_names();
  test_no_card();
  test_card_errors();
  test_writer_busy();
  reset();
  if (failures) {
    std::printf("rds_capture_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("rds_capture_tests: ok\n");
  return 0;
}
