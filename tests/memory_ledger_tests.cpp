// Host tests for the buffer ledger behind RTL_MEM BUFFERS.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/memory_ledger_tests.cpp apps/orcsdr-tab5/ui/memory_ledger.cpp
#include "memory_ledger.hpp"

#include <cstdio>
#include <cstring>

using namespace orcsdr::memory_ledger;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

char g_external[16];
char g_internal[16];
const void* g_audio = nullptr;
size_t g_audio_bytes = 0;
int g_bytes_calls = 0;

bool is_external(const void* pointer) { return pointer == g_external; }

const Buffer kBuffers[] = {
    {"audio_rec", [] { return g_audio; }, [] { ++g_bytes_calls; return g_audio_bytes; }},
    {"rds_capture", []() -> const void* { return g_external; }, []() -> size_t { return 3840000; }},
    {"never", []() -> const void* { return nullptr; }, []() -> size_t { return 99; }},
};

}  // namespace

int main() {
  // Not allocated: no bytes, and the size callback is not consulted.
  Line line = describe(kBuffers[0], is_external);
  CHECK(line.placement == Placement::none && line.bytes == 0 && g_bytes_calls == 0);
  CHECK(std::strcmp(line.name, "audio_rec") == 0);

  // The fallback case this exists to expose: the same buffer landing in internal RAM.
  g_audio = g_internal;
  g_audio_bytes = 1152000;
  line = describe(kBuffers[0], is_external);
  CHECK(line.placement == Placement::internal && line.bytes == 1152000);
  g_audio = g_external;
  line = describe(kBuffers[0], is_external);
  CHECK(line.placement == Placement::psram);

  // Without a placement oracle nothing is claimed to be in PSRAM.
  CHECK(describe(kBuffers[0], nullptr).placement == Placement::internal);

  // Broken table entries are harmless.
  CHECK(describe(Buffer{nullptr, nullptr, nullptr}, is_external).placement == Placement::none);
  CHECK(std::strcmp(describe(Buffer{nullptr, nullptr, nullptr}, is_external).name, "") == 0);

  g_audio = g_internal;
  Totals totals = summarize(kBuffers, 3, is_external);
  CHECK(totals.allocated == 2 && totals.psram_bytes == 3840000 && totals.internal_bytes == 1152000);
  totals = summarize(nullptr, 3, is_external);
  CHECK(totals.allocated == 0 && totals.psram_bytes == 0 && totals.internal_bytes == 0);

  char text[96];
  CHECK(format_line(text, sizeof(text), describe(kBuffers[0], is_external)) == std::strlen(text));
  CHECK(std::strcmp(text, "RTL_MEM_BUF audio_rec bytes=1152000 place=internal") == 0);
  format_line(text, sizeof(text), describe(kBuffers[2], is_external));
  CHECK(std::strcmp(text, "RTL_MEM_BUF never bytes=0 place=none") == 0);
  format_totals(text, sizeof(text), summarize(kBuffers, 3, is_external));
  CHECK(std::strcmp(text, "RTL_MEM_BUF_TOTAL allocated=2 psram_bytes=3840000 internal_bytes=1152000") == 0);

  // Too small: nothing partial is left behind.
  char tiny[12] = "x";
  CHECK(format_line(tiny, sizeof(tiny), describe(kBuffers[0], is_external)) == 0 && tiny[0] == '\0');
  CHECK(format_totals(tiny, sizeof(tiny), totals) == 0 && tiny[0] == '\0');
  CHECK(format_line(nullptr, 0, line) == 0);

  if (failures == 0) std::printf("PASS: memory ledger placement, totals and line formats\n");
  return failures == 0 ? 0 : 1;
}
