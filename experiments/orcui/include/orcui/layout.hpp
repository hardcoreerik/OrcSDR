#pragma once
#include <cstdint>
namespace orcsdr::orcui {
enum class Shape : uint8_t { rectangle, circle };
enum class Form : uint8_t { compact, square, wide, ultrawide, round };
struct Insets { int left=0,top=0,right=0,bottom=0; };
struct Display {
  int width=0,height=0;
  Shape shape=Shape::rectangle;
  Insets insets{};
  int density_dpi=160;
};
struct Rect {
  int x=0,y=0,w=0,h=0;
  constexpr int right() const { return x+w; }
  constexpr int bottom() const { return y+h; }
  constexpr bool contains(int px,int py) const {
    return px>=x && py>=y && px<right() && py<bottom();
  }
  constexpr bool valid() const { return w>0 && h>0; }
};
struct Layout {
  Form form=Form::compact;
  Rect safe{},header{},menu_toggle{},sidebar{},spectrum{},waterfall{};
  Rect frequency{},controls{},status{},inspector{},menu_overlay{};
  Rect control_buttons[5]{};
  bool show_sidebar=false,show_inspector=false,show_controls=false,valid=false;
};
Layout plan(const Display& screen);
// Drawer rows scroll vertically; each target retains a 44px touch height.
Rect menu_row(const Layout& layout,int index);
const char* form_name(Form form);
}