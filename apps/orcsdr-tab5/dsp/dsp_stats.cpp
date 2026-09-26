#include "dsp_stats.hpp"

#include <atomic>
#include <cstdio>

namespace orcsdr::dsp::stats {
namespace {

constexpr size_t kStages = static_cast<size_t>(Stage::count);
std::atomic<uint32_t> g_stage_us[kStages]{};
std::atomic<uint32_t> g_blocks{0};
std::atomic<uint32_t> g_total_us{0};
std::atomic<uint32_t> g_max_us{0};
std::atomic<uint32_t> g_samples{0};
std::atomic<uint32_t> g_queue_hwm{0};
std::atomic<uint32_t> g_backlog_blocks{0};
std::atomic<uint32_t> g_overload_yields{0};

void store_max(std::atomic<uint32_t>& target, uint32_t value) {
  uint32_t previous = target.load(std::memory_order_relaxed);
  while (value > previous &&
         !target.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {
  }
}

}  // namespace

const char* stage_name(Stage stage) {
  switch (stage) {
    case Stage::level: return "level";
    case Stage::spectrum: return "spectrum";
    case Stage::decoders: return "decoders";
    case Stage::record: return "record";
    case Stage::demod: return "demod";
    case Stage::other: return "other";
    case Stage::rds: return "rds_in_demod";
    default: return "?";
  }
}

void add(Stage stage, uint32_t elapsed_us) {
  const size_t index = static_cast<size_t>(stage);
  if (index < kStages) g_stage_us[index].fetch_add(elapsed_us, std::memory_order_relaxed);
}

void block_done(uint32_t total_us, uint32_t samples, uint32_t queue_depth) {
  g_blocks.fetch_add(1, std::memory_order_relaxed);
  g_total_us.fetch_add(total_us, std::memory_order_relaxed);
  g_samples.fetch_add(samples, std::memory_order_relaxed);
  store_max(g_max_us, total_us);
  store_max(g_queue_hwm, queue_depth);
  if (queue_depth > 0) g_backlog_blocks.fetch_add(1, std::memory_order_relaxed);
}

void overload_yield() { g_overload_yields.fetch_add(1, std::memory_order_relaxed); }

size_t format_and_reset(char* out, size_t size, uint32_t window_ms) {
  if (out == nullptr || size == 0) return 0;
  const uint32_t blocks = g_blocks.exchange(0, std::memory_order_relaxed);
  const uint32_t total_us = g_total_us.exchange(0, std::memory_order_relaxed);
  const uint32_t max_us = g_max_us.exchange(0, std::memory_order_relaxed);
  const uint32_t samples = g_samples.exchange(0, std::memory_order_relaxed);
  const uint32_t hwm = g_queue_hwm.exchange(0, std::memory_order_relaxed);
  const uint32_t backlog = g_backlog_blocks.exchange(0, std::memory_order_relaxed);
  const uint32_t yields = g_overload_yields.exchange(0, std::memory_order_relaxed);
  const uint32_t window = window_ms ? window_ms : 1;
  int n = snprintf(out, size,
                   "RTL_DSP_STATS window_ms=%lu blocks=%lu samples_per_s=%lu load_pct=%lu "
                   "avg_us=%lu max_us=%lu queue_hwm=%lu backlog_blocks=%lu overload_yields=%lu",
                   static_cast<unsigned long>(window_ms), static_cast<unsigned long>(blocks),
                   static_cast<unsigned long>(static_cast<uint64_t>(samples) * 1000u / window),
                   static_cast<unsigned long>(static_cast<uint64_t>(total_us) / 10u / window),
                   static_cast<unsigned long>(blocks ? total_us / blocks : 0),
                   static_cast<unsigned long>(max_us), static_cast<unsigned long>(hwm),
                   static_cast<unsigned long>(backlog), static_cast<unsigned long>(yields));
  size_t used = n > 0 ? static_cast<size_t>(n) : 0;
  for (size_t i = 0; i < kStages && used < size; ++i) {
    const uint32_t us = g_stage_us[i].exchange(0, std::memory_order_relaxed);
    n = snprintf(out + used, size - used, " %s_avg_us=%lu",
                 stage_name(static_cast<Stage>(i)),
                 static_cast<unsigned long>(blocks ? us / blocks : 0));
    if (n > 0) used += static_cast<size_t>(n);
  }
  return used < size ? used : size - 1;
}

bool self_check() {
  char buf[64];
  return format_and_reset(buf, sizeof(buf), 1000) > 0 &&
         stage_name(Stage::demod)[0] == 'd' && stage_name(Stage::count)[0] == '?';
}

}  // namespace orcsdr::dsp::stats
