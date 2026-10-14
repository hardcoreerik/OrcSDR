#include "sstv_export.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <mutex>
#define EXT_RAM_BSS_ATTR
#endif

namespace orcsdr::sstv_export {
namespace {
void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i)); }

// Same expansion as sstv_archive.cpp write_bmp_once: B = (c & 31) << 3, G = ((c >> 5) & 63) << 2, R = ((c >> 11) & 31) << 3.
void expand(uint16_t c, unsigned* r, unsigned* g, unsigned* b) {
  *b = static_cast<unsigned>(c & 31u) << 3;
  *g = static_cast<unsigned>((c >> 5) & 63u) << 2;
  *r = static_cast<unsigned>((c >> 11) & 31u) << 3;
}

void json_escape(char* out, size_t out_size, const char* in) {
  if (out_size == 0) return;
  size_t o = 0;
  if (in == nullptr) in = "";
  for (size_t i = 0; in[i] != '\0' && o + 2 < out_size; ++i) {
    const char c = in[i];
    if (c == '"' || c == '\\') {
      if (o + 3 >= out_size) break;   // no room for the escape and the NUL: stop rather than emit a bare delimiter
      out[o++] = '\\';
      out[o++] = c;
    } else if (static_cast<unsigned char>(c) >= 32) {
      out[o++] = c;
    }
  }
  out[o] = '\0';
}

// ---- published state ----
struct Slot {
  Meta meta;
  char json[kJsonCapacity];
  size_t json_bytes = 0;
  size_t bmp_bytes = 0;
  bool capture = false;
};
EXT_RAM_BSS_ATTR uint8_t g_bmp[2][kMaxBmpBytes];
Slot g_slot[2];
std::atomic<int> g_front{0};
std::atomic<bool> g_ready{false};
#ifdef ESP_PLATFORM
StaticSemaphore_t g_mutex_storage;
SemaphoreHandle_t g_mutex = nullptr;
bool lock() { return g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE; }
void unlock() { xSemaphoreGive(g_mutex); }
#else
std::mutex g_mutex;
bool lock() { g_mutex.lock(); return true; }
void unlock() { g_mutex.unlock(); }
#endif
}  // namespace

uint16_t preview_height(uint16_t width, uint16_t decoded_rows) {
  if (width == 0 || decoded_rows == 0) return 0;
  const unsigned h = (static_cast<unsigned>(decoded_rows) * kPreviewWidth + width / 2u) / width;   // round
  return static_cast<uint16_t>(std::clamp(h, 1u, static_cast<unsigned>(kPreviewMaxHeight)));
}

size_t encode_preview_bmp(const uint16_t* pixels, uint16_t width, uint16_t height, uint16_t decoded_rows, uint8_t* out,
                          size_t capacity, uint16_t* preview_height_out) {
  if (preview_height_out) *preview_height_out = 0;
  if (!pixels || !out || width == 0 || height == 0 || decoded_rows == 0) return 0;
  const uint16_t rows = std::min(decoded_rows, height);   // never read past what was decoded (or past the picture)
  const uint16_t ph = preview_height(width, rows);
  if (ph == 0) return 0;
  const size_t stride = static_cast<size_t>(kPreviewWidth) * 3u;   // 192: a multiple of 4, so no row padding
  const size_t total = kBmpHeaderBytes + stride * ph;
  if (capacity < total) return 0;
  std::memset(out, 0, kBmpHeaderBytes);
  out[0] = 'B';
  out[1] = 'M';
  put32(out + 2, static_cast<uint32_t>(total));
  put32(out + 10, static_cast<uint32_t>(kBmpHeaderBytes));
  put32(out + 14, 40);                                  // BITMAPINFOHEADER
  put32(out + 18, kPreviewWidth);
  put32(out + 22, ph);                                  // positive height: rows are stored bottom-up
  put16(out + 26, 1);
  put16(out + 28, 24);
  put32(out + 34, static_cast<uint32_t>(stride * ph));
  put32(out + 38, 2835);
  put32(out + 42, 2835);
  for (uint16_t y = 0; y < ph; ++y) {
    // The source rows [y0, y1) that this preview row covers: together they partition exactly [0, rows).
    const unsigned y0 = static_cast<unsigned>(y) * rows / ph;
    const unsigned y1 = std::max(y0 + 1u, static_cast<unsigned>(y + 1) * rows / ph);
    uint8_t* line = out + kBmpHeaderBytes + stride * static_cast<size_t>(ph - 1 - y);   // bottom-up
    for (uint16_t x = 0; x < kPreviewWidth; ++x) {
      const unsigned x0 = static_cast<unsigned>(x) * width / kPreviewWidth;
      const unsigned x1 = std::max(x0 + 1u, static_cast<unsigned>(x + 1) * width / kPreviewWidth);
      unsigned sr = 0, sg = 0, sb = 0, n = 0;
      for (unsigned sy = y0; sy < y1 && sy < rows; ++sy) {
        const uint16_t* src = pixels + static_cast<size_t>(sy) * width;
        for (unsigned sx = x0; sx < x1 && sx < width; ++sx) {
          unsigned r, g, b;
          expand(src[sx], &r, &g, &b);
          sr += r; sg += g; sb += b; ++n;
        }
      }
      if (n == 0) n = 1;
      line[x * 3 + 0] = static_cast<uint8_t>(sb / n);   // stored B, G, R
      line[x * 3 + 1] = static_cast<uint8_t>(sg / n);
      line[x * 3 + 2] = static_cast<uint8_t>(sr / n);
    }
  }
  if (preview_height_out) *preview_height_out = ph;
  return total;
}

size_t format_json(const Meta* meta, char* out, size_t capacity) {
  if (!out || capacity == 0) return 0;
  int n;
  if (!meta) {
    n = std::snprintf(out, capacity, "{\"none\":true}");
  } else {
    char mode[48], ident[80], end[48], file[130];
    json_escape(mode, sizeof(mode), meta->mode);
    json_escape(ident, sizeof(ident), meta->identification);
    json_escape(end, sizeof(end), meta->end);
    json_escape(file, sizeof(file), meta->filename);
    n = std::snprintf(out, capacity,
                      "{\"none\":false,\"serial\":%lu,\"session_id\":%lu,\"frequency_hz\":%lu,\"utc_epoch\":%lu,"
                      "\"mode\":\"%s\",\"width\":%u,\"height\":%u,\"rows\":%u,\"partial\":%s,\"start_unknown\":%s,"
                      "\"vis_code\":%u,\"sync_locked\":%u,\"sync_missed\":%u,\"identification\":\"%s\",\"end\":\"%s\","
                      "\"filename\":\"%s\",\"preview_width\":%u,\"preview_height\":%u}",
                      static_cast<unsigned long>(meta->serial), static_cast<unsigned long>(meta->session_id),
                      static_cast<unsigned long>(meta->frequency_hz), static_cast<unsigned long>(meta->utc_epoch), mode,
                      static_cast<unsigned>(meta->width), static_cast<unsigned>(meta->height), static_cast<unsigned>(meta->rows),
                      meta->partial ? "true" : "false", meta->start_unknown ? "true" : "false",
                      static_cast<unsigned>(meta->vis_code), static_cast<unsigned>(meta->sync_locked),
                      static_cast<unsigned>(meta->sync_missed), ident, end, file, static_cast<unsigned>(kPreviewWidth),
                      static_cast<unsigned>(meta->preview_height));
  }
  if (n < 0 || static_cast<size_t>(n) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

void init() {
  if (g_ready.load(std::memory_order_acquire)) return;
#ifdef ESP_PLATFORM
  if (!g_mutex) g_mutex = xSemaphoreCreateMutexStatic(&g_mutex_storage);
#endif
  Slot& empty = g_slot[g_front.load(std::memory_order_relaxed)];
  empty.capture = false;
  empty.bmp_bytes = 0;
  empty.json_bytes = format_json(nullptr, empty.json, sizeof(empty.json));
  g_ready.store(true, std::memory_order_release);
}
bool ready() { return g_ready.load(std::memory_order_acquire); }
uint8_t* back_bmp() {
  if (!ready()) return nullptr;
  return g_bmp[1 - g_front.load(std::memory_order_acquire)];
}

bool publish(const Meta& meta, size_t bmp_bytes) {
  if (!ready() || bmp_bytes < kBmpHeaderBytes || bmp_bytes > kMaxBmpBytes) return false;
  const int back = 1 - g_front.load(std::memory_order_acquire);
  Slot& slot = g_slot[back];
  slot.meta = meta;
  slot.json_bytes = format_json(&slot.meta, slot.json, sizeof(slot.json));
  if (slot.json_bytes == 0) return false;
  slot.bmp_bytes = bmp_bytes;
  slot.capture = true;
  if (!lock()) return false;     // the back buffer and its metadata are complete: flip under the short lock
  g_front.store(back, std::memory_order_release);
  unlock();
  return true;
}

void publish_empty() {
  if (!ready()) return;
  const int back = 1 - g_front.load(std::memory_order_acquire);
  Slot& slot = g_slot[back];
  slot.meta = Meta{};
  slot.capture = false;
  slot.bmp_bytes = 0;
  slot.json_bytes = format_json(nullptr, slot.json, sizeof(slot.json));
  if (!lock()) return;
  g_front.store(back, std::memory_order_release);
  unlock();
}

bool published_capture(uint32_t* serial, char* filename, size_t filename_size) {
  if (!ready() || !lock()) return false;   // same lock as the httpd readers: the slot is never read while it is rewritten
  const Slot& slot = g_slot[g_front.load(std::memory_order_acquire)];
  const bool have = slot.capture;
  if (have && serial) *serial = slot.meta.serial;
  if (have && filename && filename_size) std::snprintf(filename, filename_size, "%s", slot.meta.filename);
  unlock();
  return have;
}

size_t read_json(char* out, size_t capacity) {
  if (!out || capacity == 0) return 0;
  if (!ready() || !lock()) return format_json(nullptr, out, capacity);
  const int front = g_front.load(std::memory_order_acquire);
  const size_t n = g_slot[front].json_bytes;
  size_t copied = 0;
  if (n && n < capacity) {
    std::memcpy(out, g_slot[front].json, n + 1);
    copied = n;
  }
  unlock();
  return copied ? copied : format_json(nullptr, out, capacity);
}

size_t read_bmp(uint8_t* out, size_t capacity) {
  if (!out || !ready() || !lock()) return 0;
  const int front = g_front.load(std::memory_order_acquire);
  const Slot& slot = g_slot[front];
  size_t copied = 0;
  if (slot.capture && slot.bmp_bytes && slot.bmp_bytes <= capacity) {
    std::memcpy(out, g_bmp[front], slot.bmp_bytes);   // a short copy under the lock; the send happens after it is released
    copied = slot.bmp_bytes;
  }
  unlock();
  return copied;
}

bool self_check() {
  uint16_t ph = 0;
  uint16_t px[320 * 240];
  for (auto& p : px) p = 0xF800;   // pure red
  uint8_t bmp[kMaxBmpBytes];
  const size_t n = encode_preview_bmp(px, 320, 240, 240, bmp, sizeof(bmp), &ph);
  return n == kBmpHeaderBytes + 192u * 48u && ph == 48 && bmp[0] == 'B' && bmp[1] == 'M' && preview_height(320, 240) == 48 &&
         preview_height(320, 1) == 1 && preview_height(160, 240) == 64 && encode_preview_bmp(nullptr, 320, 240, 240, bmp, sizeof(bmp), &ph) == 0;
}

}  // namespace orcsdr::sstv_export
