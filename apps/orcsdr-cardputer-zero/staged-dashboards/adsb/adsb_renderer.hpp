#pragma once
#include "adsb_module.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace orc::zero::adsb {
struct RenderStats {uint32_t full_clears=0,dirty_widgets=0;uint64_t pixels_painted=0;};
// Host constructs once; pixel backing store is 320*170*2=108,800 bytes.
// 'render' runs only on UI thread with a Snapshot copied off decoder thread.
// Palette index is host-owned: ADS-B intentionally does not draw RF/waterfall.
class Renderer {
 public:
  static constexpr int width=320,height=170;
  void render(const Snapshot&,unsigned palette_index=0);
  bool save_ppm(const std::string& path) const;
  const std::array<uint16_t,width*height>& pixels() const {return pixels_;}
  const RenderStats& stats() const {return stats_;}
 private:
  std::array<uint16_t,width*height> pixels_{};
  Snapshot previous_{};
  RenderStats stats_{};
  bool initialized_=false;
  void rect(int x,int y,int w,int h,uint16_t color);
  void text(int x,int y,const char* str,uint16_t color,int limit=32,int scale=1);
  void header(const Snapshot&);
  void list_row(const Snapshot&,size_t row);
  void detail(const Snapshot&);
  void footer(const Snapshot&);
  void main_area(const Snapshot&);
};
} // namespace orc::zero::adsb
