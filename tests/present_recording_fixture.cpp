#include "present.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace {
constexpr int kNativeWidth = 800, kNativeHeight = 480;
constexpr size_t kStride = kNativeWidth * 3;
constexpr size_t kBytes = kStride * kNativeHeight;
alignas(32) uint8_t framebuffers[2][kBytes];
struct SyncCall { int fb, y0, y1; };
SyncCall sync_calls[64];
int sync_count = 0;
int flip_indices[32];
int flip_count = 0;
int64_t fake_time = 0;

uint8_t expand5(unsigned value) { return static_cast<uint8_t>((value << 3) | (value >> 2)); }
uint8_t expand6(unsigned value) { return static_cast<uint8_t>((value << 2) | (value >> 4)); }
uint16_t test_color(int x, int y, int phase = 0) {
  const uint16_t r = static_cast<uint16_t>((x * 7 + y * 3 + phase) & 31);
  const uint16_t g = static_cast<uint16_t>((x * 5 + y * 11 + phase * 3) & 63);
  const uint16_t b = static_cast<uint16_t>((x * 13 + y * 17 + phase * 5) & 31);
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}
bool pixel_matches(const uint8_t* fb, int x, int y, uint16_t rgb565) {
  const size_t offset = static_cast<size_t>(y) * kStride + static_cast<size_t>(x) * 3;
  return fb[offset] == expand5(rgb565 & 31) &&
         fb[offset + 1] == expand6((rgb565 >> 5) & 63) &&
         fb[offset + 2] == expand5((rgb565 >> 11) & 31);
}
bool buffers_equal() { return std::memcmp(framebuffers[0], framebuffers[1], kBytes) == 0; }
void reset_board_logs() { sync_count = 0; flip_count = 0; }
bool setup_panel(orcsdr::gfx::ScaledDisplay& display, hosyond_display_t& panel) {
  std::memset(framebuffers, 0x5a, sizeof(framebuffers));
  sync_count = flip_count = 0;
  panel = {};
  panel.width = kNativeWidth;
  panel.height = kNativeHeight;
  panel.bytes_per_pixel = 3;
  panel.stride_bytes = kStride;
  panel.fb_bytes = kBytes;
  panel.fb_count = 2;
  panel.fb[0] = framebuffers[0];
  panel.fb[1] = framebuffers[1];
  if (!orcsdr_geometry_init(&panel.geometry, kNativeWidth, kNativeHeight, 0, 0, 0, 0, 0)) return false;
  if (!display.begin(kNativeWidth, kNativeHeight, kNativeWidth, kNativeHeight)) return false;
  return present_init(&display, &panel);
}
void seed_display(orcsdr::gfx::ScaledDisplay& display, int phase) {
  uint16_t* pixels = display.mutable_buffer();
  const int w = display.target_width(), h = display.target_height();
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      pixels[static_cast<size_t>(y) * w + x] = test_color(x, y, phase);
}
int check_view_pixel(const hosyond_display_t& panel, const orcsdr::gfx::ScaledDisplay& display,
                     int x, int y, uint16_t value) {
  orcsdr_point_t native{};
  const orcsdr_display_geometry_t* geometry = present_geometry();
  if (!geometry || !orcsdr_view_to_native(geometry, {x,y}, &native)) return 1;
  const size_t o = static_cast<size_t>(native.y) * kStride + static_cast<size_t>(native.x) * 3;
  const uint8_t expected[] = {expand5(value & 31), expand6((value >> 5) & 63), expand5((value >> 11) & 31)};
  for (int fb = 0; fb < 2; ++fb)
    for (int c = 0; c < 3; ++c)
      if (panel.fb[fb][o + c] != expected[c]) return 10 + fb * 3 + c;
  return 0;
}
}

esp_err_t hosyond_board_sync(const hosyond_display_t*, int fb_index, int y0, int y1) {
  if (sync_count < static_cast<int>(std::size(sync_calls))) sync_calls[sync_count++] = {fb_index,y0,y1};
  return 0;
}
esp_err_t hosyond_board_flip(const hosyond_display_t*, int fb_index) {
  if (flip_count < static_cast<int>(std::size(flip_indices))) flip_indices[flip_count++] = fb_index;
  return 0;
}
int64_t esp_timer_get_time() { fake_time += 10; return fake_time; }

extern "C" __declspec(dllexport) int test_disjoint_incremental_ranges_and_mirror() {
  static orcsdr::gfx::ScaledDisplay display;
  hosyond_display_t panel{};
  if (!setup_panel(display, panel)) return 1;
  seed_display(display, 1);
  if (present_frame() != kNativeHeight || flip_count != 1 || !buffers_equal()) return 2;
  reset_board_logs();
  std::memset(framebuffers, 0xa5, sizeof(framebuffers));
  display.mutable_buffer()[10 * kNativeWidth + 17] = test_color(17, 10, 21);
  display.mutable_buffer()[11 * kNativeWidth + 17] = test_color(17, 11, 21);
  display.mutable_buffer()[400 * kNativeWidth + 17] = test_color(17, 400, 21);
  display.mutable_buffer()[401 * kNativeWidth + 17] = test_color(17, 401, 21);
  display.add_dirty(10, 12);
  display.add_dirty(400, 402);
  const int copied = present_frame();
  if (copied != 4 || flip_count != 1 || !buffers_equal()) return 3;
  if (!pixel_matches(framebuffers[0],17,10,test_color(17,10,21)) ||
      !pixel_matches(framebuffers[1],17,401,test_color(17,401,21))) return 4;
  const size_t untouched = static_cast<size_t>(200) * kStride + 17 * 3;
  if (framebuffers[0][untouched] != 0xa5 || framebuffers[1][untouched] != 0xa5 || sync_count != 4) return 5;
  reset_board_logs();
  display.mutable_buffer()[300 * kNativeWidth + 33] = test_color(33, 300, 42);
  display.add_dirty(300, 301);
  if (present_frame() != 1 || flip_count != 1 || !buffers_equal() ||
      !pixel_matches(framebuffers[0],33,300,test_color(33,300,42))) return 6;
  if (framebuffers[0][untouched] != 0xa5 || framebuffers[1][untouched] != 0xa5) return 7;
  return 0;
}

extern "C" __declspec(dllexport) int test_rotation_maps_native_pixels() {
  static orcsdr::gfx::ScaledDisplay display;
  hosyond_display_t panel{};
  if (!setup_panel(display, panel)) return 1;
  if (present_frame() != kNativeHeight) return 2;
  const int rotations[] = {0, 90, 180, 270};
  for (const int rotation : rotations) {
    if (!present_set_rotation(rotation)) return 3;
    seed_display(display, rotation + 3);
    display.mark_all_dirty();
    reset_board_logs();
    if (present_frame() != display.target_height() || flip_count != 1 || !buffers_equal()) return 4;
    const int w = display.target_width(), h = display.target_height();
    const int points[][2] = {{0,0},{w-1,0},{0,h-1},{w-1,h-1},{w/2,h/2}};
    for (std::size_t point = 0; point < std::size(points); ++point)
      if (const int check = check_view_pixel(panel, display, points[point][0], points[point][1],
                            test_color(points[point][0],points[point][1],rotation+3)); check)
        return 50 + static_cast<int>(rotation / 90) * 10 + static_cast<int>(point) + check * 100;
  }
  return 0;
}

extern "C" __declspec(dllexport) int test_headless_drains_ranges_without_flip() {
  static orcsdr::gfx::ScaledDisplay display;
  reset_board_logs();
  if (!display.begin(kNativeWidth, kNativeHeight, kNativeWidth, kNativeHeight) ||
      !present_init_headless(&display)) return 1;
  if (present_frame() != kNativeHeight || !display.dirty_empty()) return 2;
  display.add_dirty(12, 14); display.add_dirty(200, 203); display.add_dirty(450, 451);
  if (present_frame() != 6 || !display.dirty_empty() || present_frame() != 0 || flip_count != 0) return 3;
  return 0;
}
