#pragma once
#include <algorithm>

// Geometry for the existing shared keypad. Rendering, touch and keyboard focus
// consume these same rectangles; no per-board keypad implementation is needed.
namespace orcsdr::freq_keypad {
struct Rect { int x=0, y=0, w=0, h=0; };
struct Layout {
  Rect viewport, card, title, field, keys[12], cancel, tune;
  int entry_size=5, hint_size=4, key_size=5, action_size=4;
  bool valid=false;
};
inline bool contains(Rect r, int x, int y) {
  return x>=r.x && y>=r.y && x-r.x<r.w && y-r.y<r.h;
}
inline bool inside(Rect outer, Rect inner) {
  return inner.w>0 && inner.h>0 && inner.x>=outer.x && inner.y>=outer.y &&
      inner.x-outer.x<=outer.w-inner.w && inner.y-outer.y<=outer.h-inner.h;
}
inline Layout make_layout(Rect v) {
  Layout l{};l.viewport=v;
  if (v.w<=0 || v.h<=0) return l;
  // Preserve the established Tab5 layout exactly; existing call sites clear
  // their content area from clear_top (normally <=140).
  if (v.x==0 && v.w==1280 && v.y>=0 && v.y<=140 && v.y+v.h==720) {
    l.card={220,140,840,572};l.title={250,154,780,24};l.field={250,192,780,70};
    for (int i=0;i<12;++i) l.keys[i]={250+(i%3)*265,276+(i/3)*88,250,78};
    l.cancel={250,630,380,72};l.tune={650,630,380,72};l.valid=true;
    return l;
  }
  const int w=std::min(840,v.w-16),h=std::min(572,v.h-16);
  if (w<240 || h<328) return l;
  l.card={v.x+(v.w-w)/2,v.y+(v.h-h)/2,w,h};
  const bool tall=h>=500;
  const int pad=tall?20:8,gap=tall?12:4;
  const int title_h=tall?28:24,field_h=tall?70:44,action_h=tall?60:44;
  const int inner_w=w-2*pad;
  const int key_w=(inner_w-2*gap)/3;
  const int key_h=(h-2*pad-title_h-field_h-action_h-6*gap)/4;
  const int x=l.card.x+pad;
  l.title={x,l.card.y+pad,inner_w,title_h};
  l.field={x,l.title.y+title_h+gap,inner_w,field_h};
  for (int i=0;i<12;++i)
    l.keys[i]={x+(i%3)*(key_w+gap),l.field.y+field_h+gap+(i/3)*(key_h+gap),key_w,key_h};
  const int action_w=(inner_w-gap)/2,action_y=l.card.y+h-pad-action_h;
  l.cancel={x,action_y,action_w,action_h};l.tune={x+action_w+gap,action_y,action_w,action_h};
  l.entry_size=tall?5:4;l.hint_size=tall?4:3;
  l.key_size=std::max(3,std::min(5,(key_h-12)/8));l.action_size=tall?4:3;
  l.valid=inside(v,l.card) && inside(l.card,l.field) && inside(l.card,l.cancel) && inside(l.card,l.tune) &&
      key_w>=44 && key_h>=44 && l.keys[11].y+key_h<action_y && l.field.y+field_h<l.keys[0].y;
  return l;
}
} // namespace orcsdr::freq_keypad
