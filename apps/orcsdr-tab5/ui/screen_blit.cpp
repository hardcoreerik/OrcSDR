#include "screen_blit.hpp"

#include <M5Unified.h>
#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

namespace orcsdr::screen_blit {
namespace {

constexpr size_t kAlign = 128;  // PSRAM: L1 and L2 cache line, as the PPA requires for anything it writes

ppa_client_handle_t g_srm = nullptr;
ppa_client_handle_t g_fill = nullptr;
bool g_srm_failed = false, g_fill_failed = false;
void* g_scratch = nullptr;  // scroll_up's intermediate copy, kept for the next frame
size_t g_scratch_bytes = 0;
Counters g_counters;

size_t align_up(size_t bytes) { return (bytes + kAlign - 1) & ~(kAlign - 1); }

ppa_client_handle_t client(ppa_operation_t type, ppa_client_handle_t* handle, bool* failed) {
  if (*handle != nullptr || *failed) return *handle;
  ppa_client_config_t config = {};
  config.oper_type = type;
  config.max_pending_trans_num = 1;
  config.data_burst_length = PPA_DATA_BURST_LENGTH_128;
  const esp_err_t err = ppa_register_client(&config, handle);
  if (err != ESP_OK) {
    *handle = nullptr;
    *failed = true;  // do not retry every frame
    g_counters.last_error = err;
    ESP_LOGW("screen_blit", "PPA unavailable (%s): this work stays on M5GFX", esp_err_to_name(err));
  }
  return *handle;
}

void note_error(int err) {
  if (g_counters.last_error != err) ESP_LOGW("screen_blit", "PPA failed (%s): drawn by M5GFX", esp_err_to_name(err));
  g_counters.last_error = err;
}

// Writes the framebuffer rows [y, y + h) back from the CPU cache before the PPA driver discards that cached copy.
void write_back_rows(const Target& t, int y, int h) {
  (void)esp_cache_msync(t.fb + static_cast<size_t>(y) * t.g.panel_w,
                        static_cast<size_t>(h) * t.g.panel_w * sizeof(uint16_t),
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

void srm_defaults(ppa_srm_oper_config_t* op) {
  op->in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  op->out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  op->rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  op->scale_x = 1.0f;
  op->scale_y = 1.0f;
  op->mode = PPA_TRANS_MODE_BLOCKING;
}

bool landscape(const Target& t) { return display_rotate::ppa_ccw_degrees(t.g.rotation) > 0; }

bool inside(const Target& t, int x, int y, int w, int h) {
  return w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= t.g.logical_w && y + h <= t.g.logical_h;
}

}  // namespace

bool live_target(Target* out) {
  auto* panel = static_cast<lgfx::Panel_DSI*>(M5.Display.getPanel());
  if (panel == nullptr || panel->config_detail().buffer == nullptr) return false;
  out->fb = static_cast<uint16_t*>(panel->config_detail().buffer);  // where M5GFX draws
  out->g.panel_w = panel->config().panel_width;
  out->g.panel_h = panel->config().panel_height;
  out->g.logical_w = M5.Display.width();
  out->g.logical_h = M5.Display.height();
  out->g.rotation = M5.Display.getRotation();
  return true;
}

int ppa_push(const Target& t, const void* pixels, int w, int h, int x, int y) {
  const int degrees = display_rotate::ppa_ccw_degrees(t.g.rotation);
  if (pixels == nullptr || degrees < 0 || !inside(t, x, y, w, h)) return ESP_ERR_INVALID_ARG;
  if (client(PPA_OPERATION_SRM, &g_srm, &g_srm_failed) == nullptr) return ESP_ERR_NOT_SUPPORTED;
  const display_rotate::Rect r = display_rotate::panel_rect(t.g, x, y, w, h);
  write_back_rows(t, r.y, r.h);
  ppa_srm_oper_config_t op = {};
  srm_defaults(&op);
  op.in.buffer = pixels;
  op.in.pic_w = w;
  op.in.pic_h = h;
  op.in.block_w = w;
  op.in.block_h = h;
  op.out.buffer = t.fb;
  op.out.buffer_size = static_cast<uint32_t>(t.g.panel_w) * t.g.panel_h * sizeof(uint16_t);
  op.out.pic_w = t.g.panel_w;
  op.out.pic_h = t.g.panel_h;
  op.out.block_offset_x = r.x;
  op.out.block_offset_y = r.y;
  op.rotation_angle = degrees == 90 ? PPA_SRM_ROTATION_ANGLE_90 : PPA_SRM_ROTATION_ANGLE_270;
  op.byte_swap = true;  // M5GFX sprites hold byte-swapped RGB565; the framebuffer is native
  return ppa_do_scale_rotate_mirror(g_srm, &op);
}

int ppa_scroll_up(const Target& t, int x, int y, int w, int h, int rows) {
  if (!landscape(t) || rows <= 0 || rows >= h || !inside(t, x, y, w, h)) return ESP_ERR_INVALID_ARG;
  if (client(PPA_OPERATION_SRM, &g_srm, &g_srm_failed) == nullptr) return ESP_ERR_NOT_SUPPORTED;
  // The content of (x, y + rows) moves to (x, y). Source and destination overlap, so it goes through a scratch copy.
  const display_rotate::Rect src = display_rotate::panel_rect(t.g, x, y + rows, w, h - rows);
  const display_rotate::Rect dst = display_rotate::panel_rect(t.g, x, y, w, h - rows);
  const size_t bytes = align_up(static_cast<size_t>(src.w) * src.h * sizeof(uint16_t));
  if (bytes > g_scratch_bytes) {
    heap_caps_free(g_scratch);
    g_scratch = heap_caps_aligned_alloc(kAlign, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_scratch_bytes = g_scratch ? bytes : 0;
    if (g_scratch == nullptr) return ESP_ERR_NO_MEM;
  }
  const int top = src.y < dst.y ? src.y : dst.y;
  const int bottom = (src.y + src.h > dst.y + dst.h) ? src.y + src.h : dst.y + dst.h;
  write_back_rows(t, top, bottom - top);
  const uint32_t fb_bytes = static_cast<uint32_t>(t.g.panel_w) * t.g.panel_h * sizeof(uint16_t);

  ppa_srm_oper_config_t op = {};
  srm_defaults(&op);
  op.in.buffer = t.fb;
  op.in.pic_w = t.g.panel_w;
  op.in.pic_h = t.g.panel_h;
  op.in.block_w = src.w;
  op.in.block_h = src.h;
  op.in.block_offset_x = src.x;
  op.in.block_offset_y = src.y;
  op.out.buffer = g_scratch;
  op.out.buffer_size = g_scratch_bytes;
  op.out.pic_w = src.w;
  op.out.pic_h = src.h;
  esp_err_t err = ppa_do_scale_rotate_mirror(g_srm, &op);
  if (err != ESP_OK) return err;

  op = {};
  srm_defaults(&op);
  op.in.buffer = g_scratch;
  op.in.pic_w = src.w;
  op.in.pic_h = src.h;
  op.in.block_w = src.w;
  op.in.block_h = src.h;
  op.out.buffer = t.fb;
  op.out.buffer_size = fb_bytes;
  op.out.pic_w = t.g.panel_w;
  op.out.pic_h = t.g.panel_h;
  op.out.block_offset_x = dst.x;
  op.out.block_offset_y = dst.y;
  err = ppa_do_scale_rotate_mirror(g_srm, &op);
  return err;
}

int ppa_clear(void* buffer, size_t bytes, int w, int h) {
  if (buffer == nullptr || w <= 0 || h <= 0 || (reinterpret_cast<uintptr_t>(buffer) & (kAlign - 1)) != 0 ||
      (bytes & (kAlign - 1)) != 0 || bytes < static_cast<size_t>(w) * h * sizeof(uint16_t))
    return ESP_ERR_INVALID_ARG;
  if (client(PPA_OPERATION_FILL, &g_fill, &g_fill_failed) == nullptr) return ESP_ERR_NOT_SUPPORTED;
  ppa_fill_oper_config_t op = {};
  op.out.buffer = buffer;
  op.out.buffer_size = bytes;
  op.out.pic_w = w;
  op.out.pic_h = h;
  op.out.fill_cm = PPA_FILL_COLOR_MODE_RGB565;
  op.fill_block_w = w;
  op.fill_block_h = h;
  op.fill_color_val = 0;  // black is 0x0000 in either byte order
  op.mode = PPA_TRANS_MODE_BLOCKING;
  return ppa_do_fill(g_fill, &op);
}

void push(m5gfx::M5Canvas* sprite, int x, int y) {
  if (sprite == nullptr) return;
  Target t;
  int err = ESP_ERR_INVALID_ARG;
  if (live_target(&t) && sprite->getColorDepth() == lgfx::rgb565_2Byte)
    err = ppa_push(t, sprite->getBuffer(), sprite->width(), sprite->height(), x, y);
  if (err == ESP_OK) {
    ++g_counters.push_ppa;
    return;
  }
  if (err != ESP_ERR_INVALID_ARG) note_error(err);
  ++g_counters.push_fallback;
  M5.Display.startWrite();
  sprite->pushSprite(x, y);
  M5.Display.endWrite();
}

void scroll_up(int x, int y, int w, int h, int rows, uint16_t fill_color) {
  if (rows <= 0) return;
  Target t;
  int err = ESP_ERR_INVALID_ARG;
  if (live_target(&t)) err = ppa_scroll_up(t, x, y, w, h, rows);
  M5.Display.startWrite();
  if (err == ESP_OK) {
    ++g_counters.scroll_ppa;
    M5.Display.fillRect(x, y + h - rows, w, rows, fill_color);  // what M5GFX's scroll fills the freed rows with
  } else {
    if (err != ESP_ERR_INVALID_ARG) note_error(err);
    ++g_counters.scroll_fallback;
    M5.Display.setScrollRect(x, y, w, h, fill_color);
    M5.Display.scroll(0, -rows);
  }
  M5.Display.endWrite();
}

DmaSprite::~DmaSprite() {
  delete canvas_;  // the buffer is ours (setBuffer), M5GFX does not free it
  heap_caps_free(buffer_);
}

m5gfx::M5Canvas* DmaSprite::begin(int width, int height, uint16_t background) {
  if (canvas_ != nullptr && (width_ != width || height_ != height)) {
    delete canvas_;  // the buffer is ours (setBuffer), M5GFX does not free it
    canvas_ = nullptr;
    heap_caps_free(buffer_);
    buffer_ = nullptr;
  }
  if (canvas_ == nullptr) {
    bytes_ = align_up(static_cast<size_t>(width) * height * sizeof(uint16_t));
    buffer_ = heap_caps_aligned_alloc(kAlign, bytes_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer_ == nullptr) return nullptr;
    canvas_ = new M5Canvas(&M5.Display);
    canvas_->setColorDepth(16);
    canvas_->setBuffer(buffer_, width, height, 16);
    width_ = width;
    height_ = height;
  }
  const int err = background == 0 ? ppa_clear(buffer_, bytes_, width_, height_) : ESP_ERR_INVALID_ARG;
  if (err == ESP_OK) {
    ++g_counters.clear_ppa;
  } else {
    if (err != ESP_ERR_INVALID_ARG) note_error(err);
    ++g_counters.clear_fallback;
    canvas_->fillSprite(background);
  }
  return canvas_;
}

Counters counters() { return g_counters; }

}  // namespace orcsdr::screen_blit
