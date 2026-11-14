#include "screen_blit.hpp"

#include <M5Unified.h>
#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_err.h>
#include <esp_log.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

#include "display_rotate.hpp"

namespace orcsdr::screen_blit {
namespace {

ppa_client_handle_t g_client = nullptr;
bool g_client_failed = false;
Counters g_counters;

void fallback(m5gfx::M5Canvas* sprite, int x, int y) {
  ++g_counters.fallback;
  M5.Display.startWrite();
  sprite->pushSprite(x, y);
  M5.Display.endWrite();
}

ppa_client_handle_t client() {
  if (g_client != nullptr || g_client_failed) return g_client;
  ppa_client_config_t config = {};
  config.oper_type = PPA_OPERATION_SRM;
  config.max_pending_trans_num = 1;
  config.data_burst_length = PPA_DATA_BURST_LENGTH_128;
  const esp_err_t err = ppa_register_client(&config, &g_client);
  if (err != ESP_OK) {
    g_client = nullptr;
    g_client_failed = true;  // do not retry every frame
    g_counters.last_error = err;
    ESP_LOGW("screen_blit", "PPA unavailable (%s): screen copies stay on M5GFX", esp_err_to_name(err));
  }
  return g_client;
}

}  // namespace

void push(m5gfx::M5Canvas* sprite, int x, int y) {
  if (sprite == nullptr) return;
  auto* panel = static_cast<lgfx::Panel_DSI*>(M5.Display.getPanel());
  const int degrees = display_rotate::ppa_ccw_degrees(M5.Display.getRotation());
  const int w = sprite->width(), h = sprite->height();
  uint16_t* fb = panel ? static_cast<uint16_t*>(panel->config_detail().buffer) : nullptr;  // where M5GFX draws
  if (fb == nullptr || degrees < 0 || sprite->getColorDepth() != lgfx::rgb565_2Byte || sprite->getBuffer() == nullptr ||
      w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > M5.Display.width() || y + h > M5.Display.height() ||
      client() == nullptr) {
    fallback(sprite, x, y);
    return;
  }
  display_rotate::Geometry g;
  g.panel_w = panel->config().panel_width;
  g.panel_h = panel->config().panel_height;
  g.logical_w = M5.Display.width();
  g.logical_h = M5.Display.height();
  g.rotation = M5.Display.getRotation();
  const display_rotate::Rect r = display_rotate::panel_rect(g, x, y, w, h);

  // Everything the CPU drew in these framebuffer rows reaches memory before the PPA driver drops the cached copy.
  (void)esp_cache_msync(fb + static_cast<size_t>(r.y) * g.panel_w,
                        static_cast<size_t>(r.h) * g.panel_w * sizeof(uint16_t),
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

  ppa_srm_oper_config_t op = {};
  op.in.buffer = sprite->getBuffer();
  op.in.pic_w = w;
  op.in.pic_h = h;
  op.in.block_w = w;
  op.in.block_h = h;
  op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  op.out.buffer = fb;
  op.out.buffer_size = static_cast<uint32_t>(g.panel_w) * g.panel_h * sizeof(uint16_t);
  op.out.pic_w = g.panel_w;
  op.out.pic_h = g.panel_h;
  op.out.block_offset_x = r.x;
  op.out.block_offset_y = r.y;
  op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  op.rotation_angle = degrees == 90 ? PPA_SRM_ROTATION_ANGLE_90 : PPA_SRM_ROTATION_ANGLE_270;
  op.scale_x = 1.0f;
  op.scale_y = 1.0f;
  op.byte_swap = true;  // M5GFX sprites hold byte-swapped RGB565; the framebuffer is native
  op.mode = PPA_TRANS_MODE_BLOCKING;
  const esp_err_t err = ppa_do_scale_rotate_mirror(g_client, &op);
  if (err != ESP_OK) {
    if (g_counters.last_error != err)
      ESP_LOGW("screen_blit", "PPA copy failed (%s): this frame drawn by M5GFX", esp_err_to_name(err));
    g_counters.last_error = err;
    fallback(sprite, x, y);
    return;
  }
  ++g_counters.ppa;
}

Counters counters() { return g_counters; }

}  // namespace orcsdr::screen_blit
