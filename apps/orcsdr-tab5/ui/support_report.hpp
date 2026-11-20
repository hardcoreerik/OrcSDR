// OrcSDR read-only, privacy-bounded support report formatter.
// Keep this platform-independent so it can be tested without ESP-IDF hardware.
#pragma once

#include <cstdint>
#include <cstdio>

namespace orcsdr::support_report {

struct Snapshot {
  const char* firmware = "";
  const char* build = "";
  const char* driver = "";
  const char* profile = "";
  const char* driver_state = "";
  const char* last_error = "";
  const char* capture_state = "";
  const char* band = "";
  const char* usb_speed = "";
  bool receiver_ready = false;
  bool driver_installed = false;
  bool metrics_ok = false;
  uint16_t vid = 0;
  uint16_t pid = 0;
  uint32_t frequency_hz = 0;
  uint32_t target_sps = 0;
  uint32_t effective_sps = 0;
  uint64_t bytes_total = 0;
  uint32_t short_transfers = 0;
  uint32_t usb_overruns = 0;
  uint32_t consumer_drops = 0;
  uint32_t uptime_ms = 0;
  int reset_reason = 0;
  uint32_t heap_free = 0;
  uint32_t dma_free = 0;
  uint32_t dma_largest = 0;
  uint32_t psram_free = 0;
};

// Calls write() once per line, without embedded newlines. No radio state is
// modified, no heap allocation is required, and no network/credential/position
// data or USB device serial number is included.
using WriteLine = void (*)(const char* line);

inline void emit(const Snapshot& s, WriteLine write) {
  if (!write) return;
  char line[320];
  write("ORC_SUPPORT_BEGIN schema=1 read_only=1 private_fields=excluded");
  std::snprintf(line, sizeof(line),
                "ORC_SUPPORT_BUILD firmware=\"%.32s\" build=\"%.40s\" driver=\"%.24s\"",
                s.firmware, s.build, s.driver);
  write(line);
  std::snprintf(line, sizeof(line),
                "ORC_SUPPORT_USB receiver_ready=%u vid=%04x pid=%04x speed=\"%.12s\"",
                s.receiver_ready ? 1u : 0u,
                static_cast<unsigned>(s.vid), static_cast<unsigned>(s.pid), s.usb_speed);
  write(line);
  std::snprintf(line, sizeof(line),
                "ORC_SUPPORT_RECEIVER installed=%u profile=\"%.48s\" state=\"%.24s\" last_error=\"%.60s\"",
                s.driver_installed ? 1u : 0u, s.profile, s.driver_state, s.last_error);
  write(line);
  std::snprintf(line, sizeof(line),
                "ORC_SUPPORT_RADIO capture=\"%.24s\" band=\"%.16s\" frequency_hz=%lu",
                s.capture_state, s.band, static_cast<unsigned long>(s.frequency_hz));
  write(line);
  std::snprintf(line, sizeof(line),
                "ORC_SUPPORT_STREAM metrics_ok=%u target_sps=%lu effective_sps=%lu "
                "bytes=%llu short_transfers=%lu overruns=%lu consumer_drops=%lu",
                s.metrics_ok ? 1u : 0u, static_cast<unsigned long>(s.target_sps),
                static_cast<unsigned long>(s.effective_sps),
                static_cast<unsigned long long>(s.bytes_total),
                static_cast<unsigned long>(s.short_transfers),
                static_cast<unsigned long>(s.usb_overruns),
                static_cast<unsigned long>(s.consumer_drops));
  write(line);
  std::snprintf(line, sizeof(line),
                "ORC_SUPPORT_SYSTEM uptime_ms=%lu reset_reason=%d "
                "heap_free=%lu dma_free=%lu dma_largest=%lu psram_free=%lu",
                static_cast<unsigned long>(s.uptime_ms), s.reset_reason,
                static_cast<unsigned long>(s.heap_free),
                static_cast<unsigned long>(s.dma_free),
                static_cast<unsigned long>(s.dma_largest),
                static_cast<unsigned long>(s.psram_free));
  write(line);
  write("ORC_SUPPORT_END");
}

} // namespace orcsdr::support_report
