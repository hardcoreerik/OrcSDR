#pragma once
// Read-only accounting of the large on-demand buffers: which are allocated right now, how big each is, and whether it
// landed in PSRAM or in internal RAM. This is telemetry only. It owns nothing, allocates nothing, keeps no state and
// never frees anything; releasing memory is memory_reclaim's job.
//
// The caller supplies a constant table of buffers (it can live in flash) and a function that says whether an address is
// in external RAM (esp_ptr_external_ram on the device), so this file has no ESP-IDF dependency and builds on the host.
#include <cstddef>
#include <cstdint>

namespace orcsdr::memory_ledger {

enum class Placement : uint8_t { none, psram, internal };

struct Buffer {
  const char* name;             // one token, no spaces: it is printed as a serial field
  const void* (*pointer)();     // current allocation, nullptr when not allocated
  size_t (*bytes)();            // size of the current allocation; ignored while pointer() is nullptr
};

struct Line {
  const char* name = "";
  size_t bytes = 0;             // 0 when not allocated
  Placement placement = Placement::none;
};

struct Totals {
  size_t allocated = 0;         // number of buffers currently allocated
  size_t psram_bytes = 0;
  size_t internal_bytes = 0;
};

using IsExternalRam = bool (*)(const void*);

const char* placement_name(Placement placement);

// What one buffer holds right now. A null entry, null callbacks or a null pointer all report "none" with 0 bytes.
Line describe(const Buffer& buffer, IsExternalRam is_external_ram);

Totals summarize(const Buffer* buffers, size_t count, IsExternalRam is_external_ram);

// "RTL_MEM_BUF <name> bytes=<n> place=<none|psram|internal>" without a newline. Returns the length written, 0 when
// `out` is null or too small for the whole line (nothing partial is left behind: out[0] is then '\0').
size_t format_line(char* out, size_t capacity, const Line& line);

// "RTL_MEM_BUF_TOTAL allocated=<n> psram_bytes=<n> internal_bytes=<n>" without a newline. Same return rule.
size_t format_totals(char* out, size_t capacity, const Totals& totals);

}  // namespace orcsdr::memory_ledger
