#include "realtime_log.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace orcsdr::realtime_log {
namespace {

constexpr uint32_t kSlots = 16;
constexpr size_t kLength = 384;

char g_slots[kSlots][kLength];
std::atomic<bool> g_ready[kSlots];
std::atomic<uint32_t> g_head{0};
std::atomic<uint32_t> g_tail{0};
std::atomic<uint32_t> g_dropped{0};

}  // namespace

void log(const char* format, ...) {
  uint32_t claimed = g_head.load(std::memory_order_relaxed);
  do {
    if (claimed - g_tail.load(std::memory_order_acquire) >= kSlots) {
      g_dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  } while (!g_head.compare_exchange_weak(claimed, claimed + 1, std::memory_order_acq_rel));
  char* slot = g_slots[claimed % kSlots];
  va_list args;
  va_start(args, format);
  vsnprintf(slot, kLength, format, args);
  va_end(args);
  g_ready[claimed % kSlots].store(true, std::memory_order_release);
}

void drain(void (*write)(const char* line)) {
  if (write == nullptr) return;
  uint32_t tail = g_tail.load(std::memory_order_relaxed);
  while (tail != g_head.load(std::memory_order_acquire)) {
    if (!g_ready[tail % kSlots].load(std::memory_order_acquire)) break;
    write(g_slots[tail % kSlots]);
    g_ready[tail % kSlots].store(false, std::memory_order_relaxed);
    g_tail.store(++tail, std::memory_order_release);
  }
}

unsigned dropped() { return g_dropped.load(std::memory_order_relaxed); }

}  // namespace orcsdr::realtime_log
