#include "present.hpp"
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"

using orcsdr::gfx::ScaledDisplay;

namespace {
constexpr char TAG[] = "PRESENT";
ScaledDisplay* g_display = nullptr;
hosyond_display_t g_panel{};
int g_back = 0;           // frame buffer not currently scanned out
bool g_swapped = false;   // the sprite stores RGB565 with its bytes swapped
uint8_t g_lut5[32], g_lut6[64];
unsigned g_last_us = 0;

// RGB565 (sprite) -> the panel's [B][G][R] bytes, expanding each channel the way a 565 panel does.
void convert_rows(int fb_index, int y0, int y1) {
  const uint16_t* src = g_display->buffer();
  const size_t src_stride = g_display->stride_pixels();
  const int w = g_display->target_width();
  for (int y = y0; y < y1; ++y) {
    const uint16_t* s = src + static_cast<size_t>(y) * src_stride;
    uint8_t* d = g_panel.fb[fb_index] + static_cast<size_t>(y) * g_panel.stride_bytes;
    for (int x = 0; x < w; ++x) {
      uint16_t v = s[x];
      if (g_swapped) v = __builtin_bswap16(v);
      d[0] = g_lut5[v & 31];
      d[1] = g_lut6[(v >> 5) & 63];
      d[2] = g_lut5[v >> 11];
      d += 3;
    }
  }
  hosyond_board_sync(&g_panel, fb_index, y0, y1);
}
}  // namespace

bool present_init(ScaledDisplay* display, const hosyond_display_t* panel) {
  if (!display || !display->ready() || !panel || panel->fb_count < 1) return false;
  g_display = display;
  g_panel = *panel;
  for (int i = 0; i < 32; ++i) g_lut5[i] = static_cast<uint8_t>((i << 3) | (i >> 2));
  for (int i = 0; i < 64; ++i) g_lut6[i] = static_cast<uint8_t>((i << 2) | (i >> 4));
  // Probe the byte order: pure red must read back as 0xF800 (native) or 0x00F8 (swapped).
  display->sprite().drawPixel(0, 0, 0xF800);
  const uint16_t probe = display->buffer()[0];
  g_swapped = probe == 0x00F8;
  if (probe != 0xF800 && probe != 0x00F8) ESP_LOGW(TAG, "unexpected RGB565 probe value 0x%04X", probe);
  display->sprite().drawPixel(0, 0, 0x0000);
  g_back = panel->fb_count > 1 ? 1 : 0;
  display->mark_all_dirty();
  ESP_LOGI(TAG, "sprite RGB565 is %s, %d frame buffer(s)", g_swapped ? "byte-swapped" : "native", panel->fb_count);
  return true;
}

int present_frame() {
  if (!g_display) return 0;
  int y0 = 0, y1 = 0;
  if (!g_display->take_dirty(&y0, &y1)) return 0;
  const int64_t t0 = esp_timer_get_time();
  convert_rows(g_back, y0, y1);
  if (g_panel.fb_count > 1) {
    hosyond_board_flip(&g_panel, g_back);
    g_back = 1 - g_back;
    convert_rows(g_back, y0, y1);  // keep the buffer we just left identical to the one on screen
  }
  g_last_us = static_cast<unsigned>(esp_timer_get_time() - t0);
  return y1 - y0;
}

unsigned present_last_us() { return g_last_us; }
