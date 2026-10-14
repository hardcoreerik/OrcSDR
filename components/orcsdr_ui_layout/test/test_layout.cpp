#include "orcsdr_ui_layout.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace orcsdr::ui;
int main(){
  const struct {int w,h;Shape s;} devices[]={
    {1280,720,Shape::Wide},{720,1280,Shape::Tall},
    {720,720,Shape::Square},{320,480,Shape::Tall}
  };
  for(const auto &d:devices)for(auto f:{Focus::Balanced,Focus::Waterfall,Focus::Spectrum}){
    auto x=make_layout(d.w,d.h,f);
    assert(x.valid&&x.shape==d.s);
    assert(x.status.y+x.status.h<=d.h);
    assert(x.waterfall.y==x.spectrum.y+x.spectrum.h+6);
    assert(x.controls.y==x.waterfall.y+x.waterfall.h+6);
  }
  assert(!make_layout(0,0).valid);
  assert(!make_layout(64,128).valid);
  assert(make_layout(720,1280,Focus::Waterfall).waterfall.h>make_layout(720,1280).waterfall.h);
  RotationController r({1,0});
  r.configure(RotationMode::Auto);
  assert(!r.update({1,0,0},1000));
  assert(!r.update({1,0,0},2200)); assert(r.rotation()==0);
  assert(r.update({1,0,0},2250)); assert(r.rotation()==1);
  assert(!r.update({0,0,1},3000)); assert(r.rotation()==1);
  assert(!r.update({-1,0,0},3500));
  assert(!r.update({0.71f,0.71f,0},3700));
  assert(!r.update({-1,0,0},3900));
  assert(r.update({-1,0,0},5200)); assert(r.rotation()==3);
  r.configure(RotationMode::Locked);
  assert(!r.update({0,1,0},7000)); assert(r.rotation()==3);
  r.configure(RotationMode::Manual,2);
  assert(r.rotation()==2&&!r.update({0,-1,0},9900));
  r.configure(RotationMode::Auto);
  assert(!r.update({2,0,0},11000)&&r.rotation()==2);
  std::puts("ORC_UI_LAYOUT HOST PASS: 12 geometry variants and debounced rotation");
}
