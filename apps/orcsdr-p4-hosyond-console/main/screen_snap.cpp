// Serial screenshot service for the scaled M5GFX display. Same wire protocol as
// tools/orcui_screenshot.py expects: `ORCUI_SCREENSHOT <16 hex token> <view>` on the console UART
// answers with ORCUI_PNG_BEGIN / ORCUI_PNG_DATA (base64) / ORCUI_PNG_END, with the byte count,
// CRC-32 and the firmware ELF SHA-256 so every image carries build provenance.
#include "screen_snap.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "esp_rom_crc.h"

namespace {
orcsdr::gfx::ScaledDisplay* g_display = nullptr;
constexpr auto kPort = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);

void error(const char* token, const char* message) { printf("ORCUI_PNG_ERROR %s %s\n", token, message); }

void capture(const char* token) {
  size_t size = 0;
  // M5GFX's own PNG encoder, the same one the Tab5 uses for its screenshots.
  void* png = g_display->sprite().createPng(&size, 0, 0, g_display->target_width(), g_display->target_height());
  if (!png || size == 0) {
    error(token, "png_failed");
    return;
  }
  const auto* app = esp_app_get_description();
  char hash[65]{};
  for (unsigned i = 0; i < 32; ++i) std::snprintf(hash + i * 2, 3, "%02x", app->app_elf_sha256[i]);
  // CRC-32 of the PNG bytes (zlib polynomial).
  const uint32_t crc = esp_rom_crc32_le(0, static_cast<const uint8_t*>(png), size);
  printf("ORCUI_PNG_BEGIN %s %d %d %u %08lx %s\n", token, g_display->target_width(), g_display->target_height(),
         unsigned(size), static_cast<unsigned long>(crc), hash);
  unsigned seq = 0;
  const uint8_t* bytes = static_cast<const uint8_t*>(png);
  for (size_t offset = 0; offset < size; offset += 384) {
    unsigned char text[513]{};
    size_t count = 0;
    const size_t n = std::min<size_t>(384, size - offset);
    if (mbedtls_base64_encode(text, sizeof(text), &count, bytes + offset, n) != 0) break;
    flockfile(stdout);
    std::fprintf(stdout, "ORCUI_PNG_DATA %s %u ", token, seq++);
    std::fwrite(text, 1, count, stdout);
    std::fputc('\n', stdout);
    funlockfile(stdout);
    vTaskDelay(1);
  }
  printf("ORCUI_PNG_END %s\n", token);
  heap_caps_free(png);
}

void task(void*) {
  char line[100]{};
  size_t length = 0;
  for (;;) {
    uint8_t c = 0;
    if (uart_read_bytes(kPort, &c, 1, pdMS_TO_TICKS(100)) != 1) continue;
    if (c == '\n' || c == '\r') {
      if (!length) continue;
      line[length] = 0;
      length = 0;
      char token[17]{}, view[16]{}, extra[2]{};
      if (std::sscanf(line, "ORCUI_SCREENSHOT %16[0-9a-f] %15s %1s", token, view, extra) == 2 && std::strlen(token) == 16)
        capture(token);
    } else if (length < sizeof(line) - 1) {
      line[length++] = static_cast<char>(c);
    } else {
      length = 0;
    }
  }
}
}  // namespace

bool screen_snap_start(orcsdr::gfx::ScaledDisplay* display) {
  g_display = display;
  if (!uart_is_driver_installed(kPort) && uart_driver_install(kPort, 2048, 0, 0, nullptr, 0) != ESP_OK) return false;
  return xTaskCreate(task, "screen_snap", 8192, nullptr, 2, nullptr) == pdPASS;
}
