#include "serial_health.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::serial_health {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  (void)authenticated;
  if (strcmp(command, "RTL_HEALTH") == 0) {
    const Snapshot s = hooks.snapshot();
    char line[512];
    const int length = snprintf(line, sizeof(line),
                                "RTL_HEALTH_STATUS uptime_ms=%u free_heap=%u min_free_heap=%u "
                                "dma_free=%u dma_min=%u dma_largest=%u tasks=%u main_stack_hwm=%u "
                                "reset_reason=%d internal_free=%u internal_min=%u internal_largest=%u "
                                "psram_free=%u psram_largest=%u\n",
                                (unsigned)s.uptime_ms, (unsigned)s.free_heap, (unsigned)s.min_free_heap,
                                (unsigned)s.dma_free, (unsigned)s.dma_min, (unsigned)s.dma_largest,
                                (unsigned)s.tasks, (unsigned)s.main_stack_hwm, s.reset_reason,
                                (unsigned)s.internal_free, (unsigned)s.internal_min, (unsigned)s.internal_largest,
                                (unsigned)s.psram_free, (unsigned)s.psram_largest);
    if (length > 0) hooks.write(line);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_health
