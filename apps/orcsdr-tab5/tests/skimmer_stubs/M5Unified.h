#pragma once
#include <cstdint>
#include <string>
#include <vector>
using textdatum_t = int;
constexpr textdatum_t middle_left=0, middle_center=1, middle_right=2;
constexpr uint16_t TFT_BLACK=0, TFT_WHITE=0xffff;
namespace fonts { struct Font {}; inline Font DejaVu18, DejaVu24; }
struct FakeDisplay {
 int clears=0, rendered_text=0, rectangles=0;
 std::vector<std::string> text;
 void setFont(const fonts::Font*) {}
 void setTextSize(int) {}
 void setTextColor(uint16_t) {}
 void setTextDatum(textdatum_t) {}
 void drawString(const char* s,int,int) { ++rendered_text; text.emplace_back(s); }
 void fillScreen(uint16_t) { ++clears; }
 void fillRect(int,int,int,int,uint16_t) { ++rectangles; }
 void drawRoundRect(int,int,int,int,int,uint16_t) { ++rectangles; }
 void fillRoundRect(int,int,int,int,int,uint16_t) { ++rectangles; }
 void drawFastVLine(int,int,int,uint16_t) { ++rectangles; }
 void drawFastHLine(int,int,int,uint16_t) { ++rectangles; }
};
struct FakeM5 { FakeDisplay Display; };
extern FakeM5 M5;
