// Host tests for the audio recording module (apps/orcsdr-tab5/ui/audio_rec.cpp) and its save through the background SD
// writer. Expected replies are typed from the code this replaced in main.cpp.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/audio_rec_tests.cpp apps/orcsdr-tab5/ui/audio_rec.cpp apps/orcsdr-tab5/ui/sd_writer.cpp
//   apps/orcsdr-tab5/ui/memory_reclaim.cpp
#include "audio_rec.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "memory_reclaim.hpp"

using namespace orcsdr;
using namespace orcsdr::audio_rec;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_writes;
uint8_t g_band_now = 0;  // 0 = FM, 1 = AM, 9 = LoRa in this fake
uint32_t g_freq = 96100000;
bool g_psram_fails = false, g_internal_fails = false;
int g_psram_allocs = 0, g_internal_allocs = 0, g_frees = 0, g_refreshes = 0;
bool g_sd = true;
std::set<std::string> g_existing;
bool g_remove_fails = false;
std::vector<std::string> g_removed;
std::map<std::string, std::string> g_card;
std::string g_open_path;
bool g_open_fails = false;
size_t g_card_room = SIZE_MAX;

const char* band_name(uint8_t b) { return b == 0 ? "FM" : b == 1 ? "AM" : b == 9 ? "LORA" : "OTHER"; }
int16_t* alloc_psram(size_t n) {
  if (g_psram_fails) return nullptr;
  ++g_psram_allocs;
  return static_cast<int16_t*>(std::malloc(n));
}
int16_t* alloc_internal(size_t n) {
  if (g_internal_fails) return nullptr;
  ++g_internal_allocs;
  return static_cast<int16_t*>(std::malloc(n));
}
int g_token = 0;
void* card_open(const char* path) {
  if (g_open_fails) return nullptr;
  g_open_path = path;
  g_card[g_open_path].clear();
  return &g_token;
}
size_t card_write(void*, const uint8_t* d, size_t n) {
  size_t used = 0;
  for (auto& f : g_card) used += f.second.size();
  const size_t room = g_card_room > used ? g_card_room - used : 0;
  const size_t k = n < room ? n : room;
  g_card[g_open_path].append(reinterpret_cast<const char*>(d), k);
  return k;
}
sd_writer::Platform make_platform() {
  sd_writer::Platform p;
  p.open = card_open;
  p.write = card_write;
  p.close = [](void*) { return true; };
  p.chunk_bytes = 64 * 1024;
  return p;
}
const sd_writer::Platform kPlatform = make_platform();
sd_writer::Writer g_writer(kPlatform);

Hooks make_hooks() {
  Hooks h;
  h.write = [](const char* t) { g_writes.emplace_back(t); };
  h.write_line = [](const char* t) { g_writes.emplace_back(t); g_writes.emplace_back("\n"); };
  h.band = [] { return g_band_now; };
  h.band_name = band_name;
  h.band_is_lora = [] { return g_band_now == 9; };
  h.frequency_hz = [] { return g_freq; };
  h.alloc_psram = alloc_psram;
  h.alloc_internal = alloc_internal;
  h.release = [](int16_t* p) { ++g_frees; std::free(p); };
  h.psram_has_room = [] { return true; };
  h.sd_ready = [] { return g_sd; };
  h.sd_state = [] { return g_sd ? "ready" : "missing"; };
  h.exists = [](const char* p) { return g_existing.count(p) > 0 || g_card.count(p) > 0; };
  h.mkdir = [](const char*) { return true; };
  h.remove = [](const char* p) {
    if (g_remove_fails) return false;
    g_removed.emplace_back(p);
    g_existing.erase(p);
    g_card.erase(p);
    return true;
  };
  h.wait = [] {};
  h.ui_refresh = [] { ++g_refreshes; };
  h.writer = []() -> sd_writer::Writer& { return g_writer; };
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
  g_band_now = 0;
  g_freq = 96100000;
  g_psram_fails = g_internal_fails = false;
  g_psram_allocs = g_internal_allocs = g_frees = g_refreshes = 0;
  g_sd = true;
  g_existing.clear();
  g_remove_fails = false;
  g_removed.clear();
  g_card.clear();
  g_open_fails = false;
  g_card_room = SIZE_MAX;
}

std::string joined() {
  std::string s;
  for (const auto& x : g_writes) s += x;
  return s;
}

void feed(size_t n) {  // the DSP: n samples of a ramp, in blocks of 480
  static std::vector<int16_t> block(480);
  size_t done = 0;
  while (done < n) {
    const size_t k = n - done < block.size() ? n - done : block.size();
    for (size_t i = 0; i < k; ++i) block[i] = static_cast<int16_t>((done + i) & 0x7fff);
    append(block.data(), k, kHooks);
    done += k;
  }
}

void finish_save() {
  for (int i = 0; i < 10 && (saving() || g_writer.busy()); ++i) {
    (void)g_writer.run_once();
    service(kHooks);
  }
}

void test_wav_header() {
  uint8_t h[kWavHeaderBytes];
  wav_header(h, 576000);
  const uint8_t expected[kWavHeaderBytes] = {
      'R', 'I', 'F', 'F', 0x24, 0x94, 0x11, 0x00, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
      16, 0, 0, 0, 1, 0, 1, 0, 0x80, 0xbb, 0, 0, 0x00, 0x77, 0x01, 0x00,
      2, 0, 16, 0, 'd', 'a', 't', 'a', 0x00, 0x94, 0x11, 0x00};
  CHECK(std::memcmp(h, expected, kWavHeaderBytes) == 0);  // 1,152,000 data bytes; 36 + that in the RIFF size
}

void test_refusals() {
  reset();
  g_band_now = 9;
  CHECK(!start(kHooks));
  CHECK(joined() == "RTL_REC_ERROR data_mode_use_RTL_IQ_START\n");
  reset();
  g_psram_fails = g_internal_fails = true;
  CHECK(!start(kHooks));
  CHECK(joined() == "RTL_REC_ERR no_buffer\n");
  reset();
  CHECK(!stop_and_export(kHooks));
  CHECK(joined() == "RTL_REC_STOP empty\n");
}

void test_internal_fallback_kept() {
  reset();
  g_psram_fails = true;
  CHECK(start(kHooks));
  CHECK(g_internal_allocs == 1 && g_psram_allocs == 0);
}

void test_record_and_save() {
  reset();
  CHECK(start(kHooks));
  CHECK(joined() ==
        "RTL_REC_BUF samples=576000 bytes=1152000 psram=yes\n"
        "RTL_REC_START band=FM frequency_hz=96100000 rate=48000 max_sec=12\n");
  g_writes.clear();
  CHECK(start(kHooks));
  CHECK(joined() == "RTL_REC_ALREADY\n");
  CHECK(held_bytes() == 0 && release_buffer() == 0);
  feed(4800);
  CHECK(active() && samples() == 4800 && !full());
  g_writes.clear();
  status_print(kHooks);
  CHECK(joined() ==
        "RTL_REC_STATUS active=true full=false samples=4800 sec=0.10 max_sec=12 rate=48000 band=FM "
        "frequency_hz=96100000 last_path=\"none\" sd=ready\n");
  g_writes.clear();
  CHECK(stop_and_export(kHooks));
  CHECK(joined() == "RTL_REC_STOP samples=4800 sec=0.10 full=false band=FM frequency_hz=96100000\n");
  CHECK(saving() && !active());
  CHECK(held_bytes() == 0 && release_buffer() == 0);  // being saved
  g_writes.clear();
  CHECK(stop_and_export(kHooks));
  CHECK(joined() == "RTL_REC_STOP busy=saving\n");
  g_writes.clear();
  CHECK(!start(kHooks));
  CHECK(joined() == "RTL_REC_ERROR saving\n");
  g_writes.clear();
  feed(100);  // stopped: nothing recorded
  CHECK(samples() == 4800);
  finish_save();
  CHECK(joined() ==
        "RTL_REC_WAV ok path=/orcsdr/rec_001_FM_96100000.wav samples=4800 rate=48000 channels=1 bits=16 "
        "note=post_demod_pcm\n");
  CHECK(g_refreshes == 1);
  CHECK(std::string(last_path()) == "/orcsdr/rec_001_FM_96100000.wav");
  const std::string& wav = g_card["/orcsdr/rec_001_FM_96100000.wav"];
  CHECK(wav.size() == kWavHeaderBytes + 9600);
  uint8_t h[kWavHeaderBytes];
  wav_header(h, 4800);
  CHECK(wav.size() > kWavHeaderBytes && std::memcmp(wav.data(), h, kWavHeaderBytes) == 0);
  CHECK(wav.size() == kWavHeaderBytes + 9600 && wav[kWavHeaderBytes + 2] == 1 && wav[kWavHeaderBytes + 3] == 0);
  g_writes.clear();
  CHECK(stop_and_export(kHooks));
  CHECK(joined() == "RTL_REC_STOP already_saved path=/orcsdr/rec_001_FM_96100000.wav\n");

  // saved: reclaim may take it; status keeps the length and the file
  size_t n = 0;
  memory_reclaim::Report out[memory_reclaim::kMaxHolders];
  CHECK(memory_reclaim::reclaim_all(out, memory_reclaim::kMaxHolders, &n) == 1152000);
  CHECK(n == 1 && std::strcmp(out[0].name, "Audio recording") == 0 && buffer() == nullptr && g_frees == 1);
  g_writes.clear();
  status_print(kHooks);
  CHECK(joined() ==
        "RTL_REC_STATUS active=false full=false samples=4800 sec=0.10 max_sec=12 rate=48000 band=FM "
        "frequency_hz=96100000 last_path=\"/orcsdr/rec_001_FM_96100000.wav\" sd=ready\n");
  g_writes.clear();
  CHECK(stop_and_export(kHooks));
  CHECK(joined() == "RTL_REC_STOP already_saved path=/orcsdr/rec_001_FM_96100000.wav\n");
  g_writes.clear();
  g_band_now = 1;
  g_freq = 1280000;
  CHECK(start(kHooks) && g_psram_allocs == 2);  // allocated again
  feed(480);
  CHECK(stop_and_export(kHooks));
  finish_save();
  CHECK(g_card.count("/orcsdr/rec_002_AM_1280000.wav") == 1);
}

// A full buffer is saved by the main loop, not by the audio task that filled it.
void test_full_saves_from_service() {
  reset();
  CHECK(start(kHooks));
  g_writes.clear();
  feed(kMaxSamples + 1000);
  CHECK(joined() == "RTL_REC_FULL samples=576000 sec=12.0\n");
  CHECK(!active() && full() && !saving() && g_writer.result().state == sd_writer::State::idle);
  CHECK(held_bytes() == 0);  // full and waiting to be saved
  g_writes.clear();
  CHECK(!start(kHooks));     // not yet saved: a new recording must not overwrite it
  CHECK(joined() == "RTL_REC_ERROR saving\n");
  g_writes.clear();
  service(kHooks);
  CHECK(joined() == "RTL_REC_STOP samples=576000 sec=12.00 full=true band=FM frequency_hz=96100000\n");
  CHECK(saving());
  g_writes.clear();
  finish_save();
  CHECK(joined().find("RTL_REC_WAV ok path=/orcsdr/rec_001_FM_96100000.wav samples=576000") == 0);
  CHECK(g_card["/orcsdr/rec_001_FM_96100000.wav"].size() == kWavHeaderBytes + 1152000);
  g_writes.clear();
  service(kHooks);  // once only
  CHECK(g_writes.empty());
  CHECK(held_bytes() == 1152000);
}

// A STOP that arrives after the buffer filled but before the main loop saved it: one save, not two.
void test_manual_stop_after_full() {
  reset();
  CHECK(start(kHooks));
  feed(kMaxSamples);
  g_writes.clear();
  CHECK(stop_and_export(kHooks));
  finish_save();
  service(kHooks);
  const std::string out = joined();
  CHECK(out.find("RTL_REC_STOP samples=576000") == 0);
  CHECK(out.find("RTL_REC_WAV ok") != std::string::npos);
  CHECK(out.find("RTL_REC_STOP", 5) == std::string::npos);
  CHECK(g_card.size() == 1);
}

void test_card_problems() {
  reset();
  g_sd = false;
  CHECK(start(kHooks));
  feed(960);
  g_writes.clear();
  CHECK(!stop_and_export(kHooks));
  CHECK(joined() ==
        "RTL_REC_STOP samples=960 sec=0.02 full=false band=FM frequency_hz=96100000\n"
        "RTL_REC_PSRAM_HOLD samples=960 rate=48000 note=insert_sd_then_RTL_REC_SAVE\n");
  CHECK(held_bytes() == 0);  // unsaved: kept
  g_sd = true;               // RTL_REC_SAVE after inserting a card (the failed try already used number 001, as before)
  g_writes.clear();
  CHECK(stop_and_export(kHooks));
  finish_save();
  CHECK(joined() ==
        "RTL_REC_STOP samples=960 sec=0.02 full=false band=FM frequency_hz=96100000\n"
        "RTL_REC_WAV ok path=/orcsdr/rec_002_FM_96100000.wav samples=960 rate=48000 channels=1 bits=16 "
        "note=post_demod_pcm\n");

  reset();
  g_existing = {"/orcsdr/rec_001_FM_96100000.wav"};
  CHECK(start(kHooks));
  feed(480);
  CHECK(stop_and_export(kHooks));
  finish_save();
  CHECK(g_card.count("/orcsdr/rec_002_FM_96100000.wav") == 1);  // an existing file is skipped

  reset();
  g_open_fails = true;
  CHECK(start(kHooks));
  feed(480);
  CHECK(stop_and_export(kHooks));
  g_writes.clear();
  finish_save();
  CHECK(joined() ==
        "RTL_REC_WAV_ERR open path=/orcsdr/rec_001_FM_96100000.wav\n"
        "RTL_REC_PSRAM_HOLD samples=480 rate=48000 note=insert_sd_then_RTL_REC_SAVE\n");

  reset();
  g_card_room = kWavHeaderBytes + 500;
  CHECK(start(kHooks));
  feed(480);
  CHECK(stop_and_export(kHooks));
  g_writes.clear();
  finish_save();
  CHECK(joined() ==
        "RTL_REC_WAV_ERR short_write got=500 want=960\n"
        "RTL_REC_PSRAM_HOLD samples=480 rate=48000 note=insert_sd_then_RTL_REC_SAVE\n");
  CHECK(held_bytes() == 0);
}

}  // namespace

int main() {
  test_wav_header();
  test_refusals();
  test_internal_fallback_kept();
  test_record_and_save();
  test_full_saves_from_service();
  test_manual_stop_after_full();
  test_card_problems();
  reset();
  if (failures) {
    std::printf("audio_rec_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("audio_rec_tests: ok\n");
  return 0;
}
