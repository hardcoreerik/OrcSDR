#include <cstdio>
#include <cstdlib>

#include "waterfall_style.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

using namespace orcsdr::waterfall_style;

int g_hook_calls = 0;
Screen g_hook_screen = Screen::home;
uint8_t g_hook_value = 0;
void hook(Screen screen, uint8_t packed) {
  ++g_hook_calls;
  g_hook_screen = screen;
  g_hook_value = packed;
}

void test_defaults_keep_each_screens_look() {
  unpack(Screen::home, 0xFF);
  unpack(Screen::lora, 0xFF);
  CHECK(palette(Screen::home) == 0 && speed(Screen::home) == 0);
  CHECK(palette(Screen::lora) == 6 && speed(Screen::lora) == 0);
  CHECK(rows_per_frame(Screen::home) == 2);   // Home's existing 2 rows per frame
  CHECK(rows_per_frame(Screen::lora) == 1);   // LoRa's existing 1 row per frame
}

void test_speed_adds_rows() {
  set_speed(Screen::home, 2);
  CHECK(rows_per_frame(Screen::home) == 4);
  set_speed(Screen::lora, 2);
  CHECK(rows_per_frame(Screen::lora) == 3);
  set_speed(Screen::home, 99);   // out of range wraps to Normal
  CHECK(speed(Screen::home) == 0);
}

void test_cycling_wraps() {
  set_palette(Screen::home, kPaletteCount - 1);
  next_palette(Screen::home);
  CHECK(palette(Screen::home) == 0);
  set_speed(Screen::home, kSpeedCount - 1);
  next_speed(Screen::home);
  CHECK(speed(Screen::home) == 0);
}

void test_screens_are_independent() {
  set_palette(Screen::home, 3);
  set_palette(Screen::lora, 5);
  CHECK(palette(Screen::home) == 3 && palette(Screen::lora) == 5);
}

void test_pack_roundtrip_and_bad_values() {
  set_palette(Screen::home, 4);
  set_speed(Screen::home, 2);
  const uint8_t packed = pack(Screen::home);
  unpack(Screen::home, 0xFF);
  CHECK(palette(Screen::home) == 0);
  unpack(Screen::home, packed);
  CHECK(palette(Screen::home) == 4 && speed(Screen::home) == 2);
  unpack(Screen::home, 0x73);   // palette 7 does not exist
  CHECK(palette(Screen::home) == 0 && speed(Screen::home) == 0);
  unpack(Screen::lora, 0x13);   // speed 3 does not exist
  CHECK(palette(Screen::lora) == 6 && speed(Screen::lora) == 0);
}

void test_hook_runs_on_changes_not_loads() {
  set_persist_hook(hook);
  g_hook_calls = 0;
  next_palette(Screen::lora);
  CHECK(g_hook_calls == 1 && g_hook_screen == Screen::lora);
  CHECK(g_hook_value == pack(Screen::lora));
  unpack(Screen::lora, 0x00);
  CHECK(g_hook_calls == 1);
  set_persist_hook(nullptr);
  next_speed(Screen::lora);   // no hook: must not crash
}

void test_colors() {
  for (uint8_t p = 0; p < kPaletteCount; ++p) {
    CHECK(color565(p, -5.0f) == color565(p, 0.0f));   // clamped
    CHECK(color565(p, 5.0f) == color565(p, 1.0f));
    CHECK(color565(p, 0.0f) != color565(p, 1.0f));    // every palette actually spans a range
  }
  CHECK(color565(uint8_t{5}, 0.0f) == 0x0000);   // gray starts black
  CHECK(color565(uint8_t{5}, 1.0f) == 0xFFFF);   // and ends white
  CHECK(color565(uint8_t{99}, 0.5f) == color565(uint8_t{0}, 0.5f));   // unknown falls back to classic
  CHECK(palette_name(0)[0] == 'C' && palette_name(200)[0] == 'C');
  CHECK(speed_name(1)[0] == 'F' && speed_name(200)[0] == 'N');
}

}  // namespace

int main() {
  test_defaults_keep_each_screens_look();
  test_speed_adds_rows();
  test_cycling_wraps();
  test_screens_are_independent();
  test_pack_roundtrip_and_bad_values();
  test_hook_runs_on_changes_not_loads();
  test_colors();
  std::puts("waterfall_style_tests: PASS");
  return 0;
}
