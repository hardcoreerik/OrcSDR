// Screenshot of the scaled display as a PNG over the serial console, in the wire format the repo's
// tools already speak (tools/screenshot.py, `SCREENSHOT`):
//   ORC_SNAP_BEGIN width=<w> height=<h> bytes=<n> sha256=<hex of the PNG bytes> elf_sha256=<firmware>
//   ORC_SNAP_DATA <byte offset> <base64>      (384 raw bytes per line)
//   ORC_SNAP_END
// elf_sha256 is an addition: it ties every image to the exact firmware that drew it.
#include "screen_snap.hpp"
#include <algorithm>
#include <cstdio>
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

bool screen_snap_stream(orcsdr::gfx::ScaledDisplay* display) {
  size_t size = 0;
  // Temporary forensic check: UI owner is paused throughout encoding. Confirm
  // PNG creation does not mutate the actual RGB565 backing allocation.
  uint8_t raw_before[32]{}, raw_after[32]{};
  const auto raw_bytes = display->stride_pixels() * display->target_height() * sizeof(uint16_t);
  const int64_t hash_started = esp_timer_get_time();
  mbedtls_sha256(reinterpret_cast<const uint8_t*>(display->buffer()), raw_bytes, raw_before, 0);
  // M5GFX's own PNG encoder, the same one the Tab5 uses for its screenshots.
  void* png = display->sprite().createPng(&size, 0, 0, display->target_width(), display->target_height());
  mbedtls_sha256(reinterpret_cast<const uint8_t*>(display->buffer()), raw_bytes, raw_after, 0);
  const bool unchanged = std::equal(raw_before, raw_before + 32, raw_after);
  char raw_hash[65]{};
  for (unsigned i = 0; i < 32; ++i) std::snprintf(raw_hash + i * 2, 3, "%02x", raw_before[i]);
  std::printf("RTL_SNAP_BUFFER unchanged=%d bytes=%u raw_sha256=%s hash_and_encode_us=%llu\n",
              unchanged, unsigned(raw_bytes), raw_hash,
              static_cast<unsigned long long>(esp_timer_get_time() - hash_started));
  if (!png || size == 0) {
    std::printf("ORC_SNAP_ERROR png_failed\n");
    return false;
  }
  const auto* bytes = static_cast<const uint8_t*>(png);
  uint8_t digest[32];
  mbedtls_sha256(bytes, size, digest, 0);
  char png_hash[65]{}, elf_hash[65]{};
  for (unsigned i = 0; i < 32; ++i) std::snprintf(png_hash + i * 2, 3, "%02x", digest[i]);
  const auto* app = esp_app_get_description();
  for (unsigned i = 0; i < 32; ++i) std::snprintf(elf_hash + i * 2, 3, "%02x", app->app_elf_sha256[i]);
  std::printf("ORC_SNAP_BEGIN width=%d height=%d bytes=%u sha256=%s elf_sha256=%s\n", display->target_width(),
              display->target_height(), unsigned(size), png_hash, elf_hash);
  for (size_t offset = 0; offset < size; offset += 384) {
    unsigned char text[513]{};
    size_t count = 0;
    const size_t n = std::min<size_t>(384, size - offset);
    if (mbedtls_base64_encode(text, sizeof(text), &count, bytes + offset, n) != 0) break;
    flockfile(stdout);
    std::fprintf(stdout, "ORC_SNAP_DATA %u ", unsigned(offset));
    std::fwrite(text, 1, count, stdout);
    std::fputc('\n', stdout);
    funlockfile(stdout);
    vTaskDelay(1);
  }
  std::printf("ORC_SNAP_END\n");
  heap_caps_free(png);
  return true;
}
