// Host test for the RTL_MEM command family (serial_memory.cpp). The expected replies are the format strings the
// dispatcher in main.cpp used before the family moved, filled in with the scripted device facts below.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_memory_tests.cpp and apps/orcsdr-tab5/ui/{serial_memory,memory_ledger,memory_reclaim}.cpp
#include "serial_memory.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "memory_reclaim.hpp"

using namespace orcsdr::serial_memory;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// Every write is recorded separately, so the test also pins how the reply is split into writes:
// "W:" is one verbatim write (it carries its own '\n'), "L:" is a line written through println.
std::vector<std::string> g_out;
std::vector<std::string> g_calls;
bool g_hogged = false;
size_t g_hog_leave = 0;

uint32_t free_bytes(Region region) {
  g_calls.push_back("free");
  // After a HOG the reply must show the memory as it is afterwards, not as it was before.
  return region == Region::psram ? (g_hogged ? 65536u : 20900000u) : region == Region::internal ? 301000u : 45000u;
}
uint32_t largest_block(Region region) {
  return region == Region::psram ? (g_hogged ? 60000u : 16000000u) : region == Region::internal ? 120000u : 30000u;
}
uint32_t minimum_free(Region region) {
  return region == Region::psram ? 4000000u : region == Region::internal ? 250000u : 21000u;
}

char g_psram_block[8];
char g_internal_block[8];
const orcsdr::memory_ledger::Buffer kBuffers[] = {
    {"audio_rec", []() -> const void* { return g_internal_block; }, []() -> size_t { return 1152000; }},
    {"rds_capture", []() -> const void* { return g_psram_block; }, []() -> size_t { return 3840000; }},
    {"iq_capture", []() -> const void* { return nullptr; }, []() -> size_t { return 7680000; }},
};

Hooks make_hooks(bool with_orcdial) {
  Hooks hooks;
  hooks.write = [](const char* text) { g_out.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_out.push_back(std::string("L:") + text); };
  hooks.free_bytes = free_bytes;
  hooks.largest_block = largest_block;
  hooks.minimum_free = minimum_free;
  if (with_orcdial) hooks.orcdial = []() -> OrcDial { return {true, 5120}; };
  hooks.ft8 = []() -> Ft8 { return {true, "memory", 4194304, 3}; };
  hooks.buffers = kBuffers;
  hooks.buffer_count = 3;
  hooks.is_external_ram = [](const void* pointer) -> bool { return pointer == g_psram_block; };
  hooks.hog_fill = [](size_t leave) -> size_t {
    g_calls.push_back("hog");
    g_hogged = true;
    g_hog_leave = leave;
    return 20709376;
  };
  hooks.reclaim_and_retry = [] {
    g_calls.push_back("reclaim");
    g_out.push_back("W:ORC_MEMORY_RECLAIM total=0\n");
  };
  return hooks;
}

void expect(const std::vector<std::string>& expected) {
  if (g_out != expected) {
    ++failures;
    std::printf("FAIL output:\n");
    for (size_t i = 0; i < g_out.size() || i < expected.size(); ++i) {
      std::printf("  got:  %s  want: %s", i < g_out.size() ? g_out[i].c_str() : "<nothing>\n",
                  i < expected.size() ? expected[i].c_str() : "<nothing>\n");
      if ((i < g_out.size() ? g_out[i] : "x").back() != '\n') std::printf("\n");
    }
  }
  g_out.clear();
  g_calls.clear();
}

}  // namespace

int main() {
  orcsdr::memory_reclaim::clear_for_tests();
  orcsdr::memory_reclaim::register_holder({"SSTV pictures", []() -> size_t { return 921600; }, []() -> size_t { return 0; }});
  orcsdr::memory_reclaim::register_holder({"LoRa IQ buffers", []() -> size_t { return 15840000; }, []() -> size_t { return 0; }});
  const Hooks hooks = make_hooks(true);
  const Hooks no_dial = make_hooks(false);

  // STATUS needs no authentication. Holders are listed largest first.
  CHECK(handle_command("RTL_MEM STATUS", false, hooks));
  expect({"W:RTL_MEM psram_free=20900000 psram_largest=16000000 internal_free=301000 internal_largest=120000\n",
          "W:RTL_MEM_DMA dma_free=45000 dma_largest=30000\n",
          "W:RTL_MEM_ORCDIAL bridge=1 secure_stack_free_min=5120\n",
          "W:RTL_MEM_HOLDER LoRa IQ buffers bytes=15840000\n",
          "W:RTL_MEM_HOLDER SSTV pictures bytes=921600\n",
          "W:RTL_MEM_FT8 active=1 last_failure=memory needed=4194304 failures=3\n"});
  // A build without the OrcDial bridge prints no RTL_MEM_ORCDIAL line.
  CHECK(handle_command("RTL_MEM STATUS", true, no_dial));
  CHECK(g_out.size() == 5 && g_out[2] == "W:RTL_MEM_HOLDER LoRa IQ buffers bytes=15840000\n");
  g_out.clear();

  CHECK(handle_command("RTL_MEM BUFFERS", false, hooks));
  expect({"L:RTL_MEM_BUF audio_rec bytes=1152000 place=internal", "L:RTL_MEM_BUF rds_capture bytes=3840000 place=psram",
          "L:RTL_MEM_BUF iq_capture bytes=0 place=none",
          "L:RTL_MEM_BUF_TOTAL allocated=2 psram_bytes=3840000 internal_bytes=1152000",
          "W:RTL_MEM_MIN psram_min=4000000 internal_min=250000 dma_min=21000\n"});

  // HOG and RECLAIM refuse an unauthenticated caller before doing anything.
  CHECK(handle_command("RTL_MEM HOG 256", false, hooks));
  CHECK(g_calls.empty() && !g_hogged);
  expect({"L:RTL_MEM_ERROR auth_required"});
  CHECK(handle_command("RTL_MEM RECLAIM", false, hooks));
  CHECK(g_calls.empty());
  expect({"L:RTL_MEM_ERROR auth_required"});

  // HOG range: 64..32768 KiB; out of range and non-numbers are refused without allocating.
  for (const char* bad : {"RTL_MEM HOG 0", "RTL_MEM HOG 63", "RTL_MEM HOG 32769", "RTL_MEM HOG x", "RTL_MEM HOG "}) {
    CHECK(handle_command(bad, true, hooks));
    CHECK(g_calls.empty() && !g_hogged);
    expect({"L:RTL_MEM_ERROR use=RTL_MEM HOG <64..32768 KiB to leave free>"});
  }
  CHECK(handle_command("RTL_MEM HOG 64", true, hooks));
  CHECK(g_hogged && g_hog_leave == 64u * 1024u);
  CHECK(g_calls.size() == 2 && g_calls[0] == "hog" && g_calls[1] == "free");   // free memory is read after the fill
  expect({"W:RTL_MEM_OK held=20709376 psram_free=65536 psram_largest=60000\n"});
  g_hogged = false;
  CHECK(handle_command("RTL_MEM HOG 32768", true, hooks));
  CHECK(g_hog_leave == 32768u * 1024u);
  g_out.clear();
  g_calls.clear();
  g_hogged = false;

  // RECLAIM releases first (that prints its own lines), then reports what is free.
  CHECK(handle_command("RTL_MEM RECLAIM", true, hooks));
  CHECK(!g_calls.empty() && g_calls[0] == "reclaim");
  expect({"W:ORC_MEMORY_RECLAIM total=0\n", "W:RTL_MEM_OK psram_free=20900000 psram_largest=16000000\n"});

  // Anything else is left to the dispatcher: exact matches stay exact, HOG needs its trailing space.
  for (const char* other : {"RTL_MEM", "RTL_MEM ", "RTL_MEM STATUS ", "RTL_MEM STATUSX", "RTL_MEM HOG", "RTL_MEM RECLAIM NOW",
                            "RTL_MEM BUFFERS 1", "rtl_mem status", "RTL_MEMX", ""}) {
    CHECK(!handle_command(other, true, hooks));
  }
  CHECK(g_out.empty() && g_calls.empty());

  if (failures == 0) std::printf("PASS: RTL_MEM STATUS, BUFFERS, HOG and RECLAIM replies, refusals, ranges and call order\n");
  return failures == 0 ? 0 : 1;
}
