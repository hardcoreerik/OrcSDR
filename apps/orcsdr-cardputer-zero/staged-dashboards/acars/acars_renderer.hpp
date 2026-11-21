// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "acars_dashboard.hpp"
#include <array>
#include <cstdint>
#include <string>
namespace orc::zero::acars_dashboard {
struct Rect { int x=0,y=0,w=0,h=0; };
class Renderer {
 public:
  static constexpr int width=320,height=170;
  const std::array<uint16_t,width*height>& pixels() const{return pixels_;}
  const std::array<Rect,16>& dirty_rects() const{return dirty_;}
  size_t dirty_count() const{return dirty_count_;}
  uint32_t full_clear_count() const{return full_clears_;}
  // UI owner only. Dirty rectangles returned here must be flushed by host LCD adapter.
  void render(const Snapshot& s,uint64_t now_ns);
  bool save_ppm(const std::string& path)const;
 private:
  void fill(int x,int y,int w,int h,uint16_t color);
  void glyph_text(int x,int y,const char* bytes,size_t n,uint16_t color,size_t max_chars);
  void text(int x,int y,const char* value,uint16_t color,size_t max_chars);
  void dirty(int x,int y,int w,int h);
  void header(const Snapshot&);
  void status(const Snapshot&);
  void rows(const Snapshot&,uint64_t);
  void detail(const Snapshot&);
  std::array<uint16_t,width*height> pixels_{};
  std::array<Rect,16> dirty_{};
  size_t dirty_count_=0;
  uint32_t full_clears_=0;
  bool initialized_=false;
  Snapshot previous_{};
  uint64_t previous_second_=0;
};
// Untrusted payloads: explicit length, no printf/markup/escape execution.
std::string escaped_payload(const orcsdr::acars::Message& message);
size_t detail_line_count(const orcsdr::acars::Message& message);
}
