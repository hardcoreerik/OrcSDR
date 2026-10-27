#pragma once
// Standalone host recording-display stub; never used by native firmware.
#include <cstdint>
#include <string>
#include <vector>
constexpr uint16_t TFT_BLACK = 0x0000;
constexpr uint16_t TFT_WHITE = 0xffff;
constexpr int middle_left = 0;
constexpr int middle_center = 1;
struct MockDisplay {
  int display_width = 1280;
  uint32_t full_clears = 0, text_draws = 0, rect_draws = 0;
  std::vector<std::string> strings;
  int width() const { return display_width; }
  void fillScreen(uint16_t) { ++full_clears; }
  void setFont(void*) {}
  void setTextDatum(int) {}
  void setTextColor(uint16_t, uint16_t) {}
  void setTextSize(uint8_t) {}
  void drawString(const char* s, int, int) { ++text_draws; strings.emplace_back(s ? s : ""); }
  void fillRoundRect(int, int, int, int, int, uint16_t) { ++rect_draws; }
  void drawRoundRect(int, int, int, int, int, uint16_t) { ++rect_draws; }
  void drawFastHLine(int, int, int, uint16_t) { ++rect_draws; }
  void fillRect(int, int, int, int, uint16_t) { ++rect_draws; }
  bool saw(const std::string& s) const {
    for (const auto& v : strings) if (v == s) return true;
    return false;
  }
};
struct MockM5 {
  MockDisplay Display;
};
extern MockM5 M5;
extern uint32_t fake_ms;
inline uint32_t millis() { return fake_ms; }
