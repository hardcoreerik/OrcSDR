#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#if !defined(__GLIBC__) || (__GLIBC__*100+__GLIBC_MINOR__<238)
inline size_t strlcpy(char* out,const char* in,size_t capacity) {
  const size_t size=std::strlen(in);
  if(capacity){const auto count=std::min(size,capacity-1);std::memcpy(out,in,count);out[count]=0;}
  return size;
}
#endif
using textdatum_t = int;
constexpr int middle_center = 0, middle_left = 1, middle_right = 2, top_left = 3, bottom_left = 4;
constexpr int top_right = 5;
constexpr uint16_t TFT_WHITE = 0xffff, TFT_BLACK = 0, TFT_RED = 0xf800, TFT_DARKGREY = 0x7bef;
constexpr uint16_t TFT_GREEN=0x07e0,TFT_ORANGE=0xfd20,TFT_LIGHTGREY=0xc618;
namespace fonts { inline int DejaVu24, DejaVu18, Font2; }
namespace lgfx {
inline namespace v1 {
struct rgb565_t {uint16_t raw;};
struct Paint { std::string text; int x, y; uint16_t color; int text_size=1; };
inline std::vector<Paint> paints;
inline bool sprite_fail=false;
inline int sprite_fail_width=0;
inline size_t live_sprite_bytes=0;
inline int sampled_pixels=0, drawn_pixels=0, row_reads=0;
struct RowImage {int x,y;std::vector<uint16_t> pixels;};
inline std::vector<RowImage> row_images;
inline uint16_t pixel_color(int x,int y){return static_cast<uint16_t>((y*257+x*73)^0xa55a);}
inline int screen_clears=0;
struct Fill {int x,y,w,h;uint16_t color;};inline std::vector<Fill> fills;
class LovyanGFX {
  uint16_t color_ = 0;
  int text_size_ = 1;
public:
  template<class... T> void setTextDatum(T...) {}
  void setTextSize(int size) { text_size_=size; }
  template<class... T> void setFont(T...) {}
  void setTextColor(uint16_t c, uint16_t = 0) { color_ = c; }
  template<class... T> void fillRoundRect(T...) {}
  template<class... T> void drawRoundRect(T...) {}
  template<class... T> void fillRect(T...) {}
  void fillRect(int x,int y,int w,int h,uint16_t c){fills.push_back({x,y,w,h,c});}
  template<class... T> void drawRect(T...) {}
  template<class... T> void fillScreen(T...) {++screen_clears;}
  template<class... T> void fillCircle(T...) {}
  template<class... T> void drawPixel(T...) { ++drawn_pixels; }
  template<class... T> void drawCircle(T...) {}
  template<class... T> void drawFastVLine(T...) {}
  template<class... T> void drawFastHLine(T...) {}
  template<class... T> void drawLine(T...) {}
  template<class... T> void drawArc(T...) {}
  template<class... T> void fillTriangle(T...) {}
  template<class... T> void pushImage(T...) {}
  void pushImage(int x,int y,int w,int h,rgb565_t* data) {pushImage(x,y,w,h,static_cast<const rgb565_t*>(data));}
  void pushImage(int x,int y,int w,int h,const rgb565_t* data) {
    RowImage row{x,y,{}};for(int i=0;i<w*h;++i)row.pixels.push_back(data[i].raw);row_images.push_back(row);drawn_pixels+=w*h;
  }
  template<class... T> void setScrollRect(T...) {}
  template<class... T> void scroll(T...) {}
  template<class... T> void setClipRect(T...) {}
  void clearClipRect() {}
  void clearScrollRect() {}
  void startWrite() {}
  void endWrite() {}
  void drawString(const char* s, int x, int y) { paints.push_back({s, x, y, color_, text_size_}); }
  int textWidth(const char* s) { return static_cast<int>(std::strlen(s)) * 12; }
};
}
}
class M5Canvas : public lgfx::LovyanGFX {
  size_t allocated_bytes_=0;
  bool swap_bytes_=false;
public:
  explicit M5Canvas(lgfx::LovyanGFX*) {}
  ~M5Canvas() { deleteSprite(); }
  template<class... T> void setPsram(T...) {}
  template<class... T> void setColorDepth(T...) {}
  template<class... T> void fillSprite(T...) {}
  template<class... T> void pushSprite(T...) {}
  void deleteSprite() {lgfx::live_sprite_bytes-=allocated_bytes_;allocated_bytes_=0;}
  uint32_t readPixel(int x,int y) {++lgfx::sampled_pixels;return lgfx::pixel_color(x,y);}
  bool getSwapBytes()const{return swap_bytes_;}
  void setSwapBytes(bool value){swap_bytes_=value;}
  void readRect(int x,int y,int w,int h,uint16_t* pixels){++lgfx::row_reads;lgfx::sampled_pixels+=w*h;for(int row=0;row<h;++row)for(int col=0;col<w;++col){const auto c=lgfx::pixel_color(x+col,y+row);pixels[row*w+col]=swap_bytes_?c:static_cast<uint16_t>((c>>8)|(c<<8));}}
  void* createSprite(int w, int h) {
    deleteSprite();
    if(lgfx::sprite_fail||w==lgfx::sprite_fail_width)return nullptr;
    allocated_bytes_=static_cast<size_t>(w)*h*2;lgfx::live_sprite_bytes+=allocated_bytes_;return this;
  }
};
inline struct M5Stub {
  lgfx::LovyanGFX Display;
  struct {int getBatteryLevel(){return 100;}} Power;
} M5;
inline uint32_t test_millis=1000;
inline uint32_t millis() { return test_millis; }
