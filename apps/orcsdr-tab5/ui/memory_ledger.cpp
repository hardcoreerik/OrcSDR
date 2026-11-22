#include "memory_ledger.hpp"

#include <cstdio>

namespace orcsdr::memory_ledger {

namespace {

size_t finish(char* out, size_t capacity, int written) {
  if (written <= 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

}  // namespace

const char* placement_name(Placement placement) {
  switch (placement) {
    case Placement::psram: return "psram";
    case Placement::internal: return "internal";
    case Placement::none: break;
  }
  return "none";
}

Line describe(const Buffer& buffer, IsExternalRam is_external_ram) {
  Line line;
  line.name = buffer.name != nullptr ? buffer.name : "";
  if (buffer.pointer == nullptr || buffer.bytes == nullptr) return line;
  const void* pointer = buffer.pointer();
  if (pointer == nullptr) return line;
  line.bytes = buffer.bytes();
  line.placement = (is_external_ram != nullptr && is_external_ram(pointer)) ? Placement::psram : Placement::internal;
  return line;
}

Totals summarize(const Buffer* buffers, size_t count, IsExternalRam is_external_ram) {
  Totals totals;
  if (buffers == nullptr) return totals;
  for (size_t i = 0; i < count; ++i) {
    const Line line = describe(buffers[i], is_external_ram);
    if (line.placement == Placement::none) continue;
    ++totals.allocated;
    if (line.placement == Placement::psram) totals.psram_bytes += line.bytes;
    else totals.internal_bytes += line.bytes;
  }
  return totals;
}

size_t format_line(char* out, size_t capacity, const Line& line) {
  if (out == nullptr || capacity == 0) return 0;
  return finish(out, capacity,
                snprintf(out, capacity, "RTL_MEM_BUF %s bytes=%lu place=%s", line.name,
                         static_cast<unsigned long>(line.bytes), placement_name(line.placement)));
}

size_t format_totals(char* out, size_t capacity, const Totals& totals) {
  if (out == nullptr || capacity == 0) return 0;
  return finish(out, capacity,
                snprintf(out, capacity, "RTL_MEM_BUF_TOTAL allocated=%lu psram_bytes=%lu internal_bytes=%lu",
                         static_cast<unsigned long>(totals.allocated), static_cast<unsigned long>(totals.psram_bytes),
                         static_cast<unsigned long>(totals.internal_bytes)));
}

}  // namespace orcsdr::memory_ledger
