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
bool g_headless = false;  // no panel: dirty rows are consumed but never converted
uint8_t g_lut5[32], g_lut6[64];
unsigned g_last_us = 0;

// RGB565 (sprite) -> the panel's [B][G][R] bytes, expanding each channel the way a 565 panel does.
void convert_rows(int fb_index,int y0,int y1) {
  const uint16_t* src=g_display->buffer();const size_t stride=g_display->stride_pixels();
  const auto& g=g_panel.geometry;
  for (int y=y0;y<y1;++y) for (int x=0;x<g.width;++x) {
    orcsdr_point_t native;
    if (!orcsdr_view_to_native(&g,{x,y},&native)) continue;
    uint16_t v=src[size_t(y)*stride+x];
    if (g_swapped) v=uint16_t(v<<8|v>>8);
    uint8_t* d=g_panel.fb[fb_index]+size_t(native.y)*g_panel.stride_bytes+size_t(native.x)*3;
    d[0]=g_lut5[v&31];d[1]=g_lut6[(v>>5)&63];d[2]=g_lut5[v>>11];
  }
  orcsdr_rect_t rect;
  if (orcsdr_view_rect_to_native(&g,{0,y0,g.width,y1-y0},&rect))
    hosyond_board_sync(&g_panel,fb_index,rect.y,rect.y+rect.height);
}

}  // namespace

bool present_init(ScaledDisplay* display, const hosyond_display_t* panel) {
  if (!display || !display->ready() || !panel || panel->fb_count < 1) return false;
  if (panel->bytes_per_pixel!=3 || display->target_width()!=panel->geometry.width ||
      display->target_height()!=panel->geometry.height) return false;
  g_headless=false;g_display = display;
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

bool present_init_headless(ScaledDisplay* display) {
  if (!display || !display->ready()) return false;
  g_display = display;
  g_headless = true;
  display->mark_all_dirty();
  ESP_LOGW(TAG, "headless: rendering off-screen only (no panel)");
  return true;
}

int present_frame() {
  if (!g_display) return 0;
  int y0 = 0, y1 = 0;
  if (!g_display->take_dirty(&y0, &y1)) return 0;
  if (g_headless) return y1 - y0;
  const int64_t t0 = esp_timer_get_time();
  convert_rows(g_back, y0, y1);
  if (g_panel.fb_count > 1) {
    hosyond_board_flip(&g_panel, g_back);
    g_back = 1 - g_back;
    // Keep the buffer we just left identical to the one on screen: copy the rows (a plain memcpy is
    // several times cheaper than converting them a second time).
    orcsdr_rect_t native;
    if (orcsdr_view_rect_to_native(&g_panel.geometry,{0,y0,g_panel.geometry.width,y1-y0},&native)) {
      for (int y=native.y;y<native.y+native.height;++y) {
        const size_t offset=size_t(y)*g_panel.stride_bytes+size_t(native.x)*3;
        memcpy(g_panel.fb[g_back]+offset,g_panel.fb[1-g_back]+offset,size_t(native.width)*3);
      }
      hosyond_board_sync(&g_panel,g_back,native.y,native.y+native.height);
    }
  }
  g_last_us = static_cast<unsigned>(esp_timer_get_time() - t0);
  return y1 - y0;
}

unsigned present_last_us() { return g_last_us; }

const orcsdr_display_geometry_t* present_geometry() { return g_headless?nullptr:&g_panel.geometry; }

bool present_set_rotation(int degrees) {
  if (!g_display || g_headless) return false;
  orcsdr_display_geometry_t next;
  // Physical insets remain unchanged; derive them from the current orientation.
  orcsdr_rect_t native;
  if (!orcsdr_view_rect_to_native(&g_panel.geometry,g_panel.geometry.safe,&native) ||
      !orcsdr_geometry_init(&next,g_panel.width,g_panel.height,degrees,
        native.x,native.y,g_panel.width-native.x-native.width,g_panel.height-native.y-native.height)) return false;
  if (next.rotation==g_panel.geometry.rotation) return true;
  if (!g_display->begin(next.width,next.height,next.width,next.height)) return false;
  g_panel.geometry=next;g_display->mark_all_dirty();
  return true;
}
