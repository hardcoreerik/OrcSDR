#include "serial_memory.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "memory_reclaim.hpp"

namespace orcsdr::serial_memory {

namespace {

// One formatted line, one write: what Serial.printf did for these replies.
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[192];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

// What is free and which features hold memory they could give up (largest first), plus why the FT8 decoder last
// failed to start.
void print_status(const Hooks& hooks) {
  say(hooks, "RTL_MEM psram_free=%u psram_largest=%u internal_free=%u internal_largest=%u\n",
      (unsigned)hooks.free_bytes(Region::psram), (unsigned)hooks.largest_block(Region::psram),
      (unsigned)hooks.free_bytes(Region::internal), (unsigned)hooks.largest_block(Region::internal));
  // The figures that predict a Wi-Fi failure.
  say(hooks, "RTL_MEM_DMA dma_free=%u dma_largest=%u\n", (unsigned)hooks.free_bytes(Region::dma),
      (unsigned)hooks.largest_block(Region::dma));
  if (hooks.orcdial != nullptr) {
    const OrcDial dial = hooks.orcdial();
    say(hooks, "RTL_MEM_ORCDIAL bridge=%d secure_stack_free_min=%u\n", dial.bridge ? 1 : 0,
        (unsigned)dial.secure_stack_free_min);
  }
  memory_reclaim::Report holders[memory_reclaim::kMaxHolders];
  const size_t n = memory_reclaim::list(holders, memory_reclaim::kMaxHolders);
  for (size_t i = 0; i < n; ++i) say(hooks, "RTL_MEM_HOLDER %s bytes=%u\n", holders[i].name, (unsigned)holders[i].held);
  const Ft8 ft8 = hooks.ft8();
  say(hooks, "RTL_MEM_FT8 active=%d last_failure=%s needed=%u failures=%u\n", ft8.active ? 1 : 0, ft8.last_failure,
      (unsigned)ft8.needed, (unsigned)ft8.failures);
}

// The large on-demand buffers, whether each is allocated and where it landed, and the lowest free memory since boot.
void print_buffers(const Hooks& hooks) {
  char line[96];
  for (size_t i = 0; i < hooks.buffer_count; ++i) {
    if (memory_ledger::format_line(line, sizeof(line),
                                   memory_ledger::describe(hooks.buffers[i], hooks.is_external_ram)) > 0) {
      hooks.write_line(line);
    }
  }
  if (memory_ledger::format_totals(
          line, sizeof(line), memory_ledger::summarize(hooks.buffers, hooks.buffer_count, hooks.is_external_ram)) > 0) {
    hooks.write_line(line);
  }
  say(hooks, "RTL_MEM_MIN psram_min=%u internal_min=%u dma_min=%u\n", (unsigned)hooks.minimum_free(Region::psram),
      (unsigned)hooks.minimum_free(Region::internal), (unsigned)hooks.minimum_free(Region::dma));
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (!strcmp(command, "RTL_MEM STATUS")) {
    print_status(hooks);
    return true;
  }
  if (!strcmp(command, "RTL_MEM BUFFERS")) {
    print_buffers(hooks);
    return true;
  }
  if (!strncmp(command, "RTL_MEM HOG ", 12)) {   // bench aid: RTL_MEM HOG <kib to leave free>; undone by RTL_MEM RECLAIM
    if (!authenticated) { hooks.write_line("RTL_MEM_ERROR auth_required"); return true; }
    const unsigned long leave_kib = strtoul(command + 12, nullptr, 10);
    if (leave_kib < 64 || leave_kib > 32768) {
      hooks.write_line("RTL_MEM_ERROR use=RTL_MEM HOG <64..32768 KiB to leave free>");
      return true;
    }
    const size_t held = hooks.hog_fill(static_cast<size_t>(leave_kib) * 1024u);
    say(hooks, "RTL_MEM_OK held=%u psram_free=%u psram_largest=%u\n", (unsigned)held,
        (unsigned)hooks.free_bytes(Region::psram), (unsigned)hooks.largest_block(Region::psram));
    return true;
  }
  if (!strcmp(command, "RTL_MEM RECLAIM")) {
    if (!authenticated) { hooks.write_line("RTL_MEM_ERROR auth_required"); return true; }
    hooks.reclaim_and_retry();
    say(hooks, "RTL_MEM_OK psram_free=%u psram_largest=%u\n", (unsigned)hooks.free_bytes(Region::psram),
        (unsigned)hooks.largest_block(Region::psram));
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_memory
