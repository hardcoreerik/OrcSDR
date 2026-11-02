#pragma once
// HFDL native display geometry: host-testable, allocation-free, no gfx dependency.
// Explicit known panels only. Same rectangles drive paint, touch and clipping.
#include <cstddef>
#include <cstdint>

namespace orcsdr::hfdl_ui::layout {
struct Rect {
  int x=0,y=0,w=0,h=0;
  constexpr bool contains(int px,int py) const {
    return px>=x && py>=y && px<x+w && py<y+h;
  }
};
constexpr bool inside(Rect parent,Rect child) {
  return child.w>0 && child.h>0 && parent.w>0 && parent.h>0 &&
         child.x>=parent.x && child.y>=parent.y &&
         child.x+child.w<=parent.x+parent.w &&
         child.y+child.h<=parent.y+parent.h;
}
constexpr bool overlaps(Rect a,Rect b) {
  return a.w>0 && a.h>0 && b.w>0 && b.h>0 &&
         a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h;
}
enum class Panel : uint8_t { tab5, square720, hosyond800, lilygo568, unsupported };
struct View {
  Panel panel=Panel::unsupported;
  Rect screen{}, header{}, summary{}, list{}, aside{}, detail{}, footer{};
  Rect rows[8]{}, newer{}, older{}, close{}, home{};
  uint8_t visible_rows=0;
  bool portrait=false, valid=false;
};
constexpr View make_view(int width,int height) {
  View v{};v.screen={0,0,width,height};v.portrait=height>width;
  if(width==1280 && height==720) {
    v.panel=Panel::tab5;v.header={0,0,1280,104};
    v.summary={20,110,1240,82};v.list={20,202,850,430};
    v.aside={882,202,378,430};v.footer={20,646,1240,66};
    v.visible_rows=7;v.home={1040,8,58,58};
  } else if(width==720 && height==720) {
    v.panel=Panel::square720;v.header={0,0,720,116};
    v.summary={12,124,696,82};v.list={12,216,696,400};
    v.footer={12,630,696,78};v.visible_rows=6;v.home={660,8,48,48};
  } else if(width==800 && height==400) {
    v.panel=Panel::hosyond800;v.header={0,0,800,56};
    v.summary={10,64,780,50};v.list={10,122,520,220};
    v.aside={540,122,250,220};v.footer={10,346,780,46};
    v.visible_rows=3;v.home={742,4,48,48};
  } else if(width==568 && height==1232) {
    v.panel=Panel::lilygo568;v.header={0,0,568,126};
    v.summary={12,134,544,122};v.list={12,266,544,820};
    v.footer={12,1112,544,108};v.visible_rows=8;
    v.home={508,12,48,48};
  } else return v;
  v.detail={v.list.x,v.list.y,v.aside.w>0 ? v.aside.x+v.aside.w-v.list.x : v.list.w,v.list.h};
  const int row_top=v.list.y+(v.panel==Panel::hosyond800?30:40);
  const int row_bottom=v.list.y+v.list.h-10;
  const int pitch=(row_bottom-row_top)/v.visible_rows;
  for(int i=0;i<v.visible_rows;++i)
    v.rows[i]={v.list.x+10,row_top+i*pitch,v.list.w-20,pitch-5};
  const int button_h=v.panel==Panel::hosyond800?40:50;
  const int button_w=(v.footer.w-32)/2;
  const int button_y=v.footer.y+(v.footer.h-button_h)/2;
  v.newer={v.footer.x+8,button_y,button_w,button_h};
  v.older={v.footer.x+v.footer.w-8-button_w,button_y,button_w,button_h};
  v.close={v.detail.x+v.detail.w-118,v.detail.y+8,106,46};
  v.valid=inside(v.screen,v.header)&&inside(v.screen,v.summary)&&
          inside(v.screen,v.list)&&inside(v.screen,v.footer)&&
          inside(v.screen,v.detail)&&inside(v.screen,v.home)&&
          inside(v.footer,v.newer)&&inside(v.footer,v.older)&&
          !overlaps(v.list,v.footer)&&!overlaps(v.newer,v.older)&&
          inside(v.detail,v.close);
  if(v.aside.w>0) v.valid=v.valid&&inside(v.screen,v.aside)&&!overlaps(v.list,v.aside);
  for(int i=0;i<v.visible_rows;++i)
    v.valid=v.valid&&inside(v.list,v.rows[i])&&v.rows[i].h>=44;
  return v;
}
}  // namespace orcsdr::hfdl_ui::layout
