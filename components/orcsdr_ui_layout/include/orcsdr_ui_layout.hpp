#pragma once
// Portable UI geometry/orientation policy: no ESP-IDF, M5Unified or LVGL.
#include <cmath>
#include <cstdint>
namespace orcsdr::ui {
enum class Shape:uint8_t { Headless, Wide, Square, Tall };
enum class Focus:uint8_t { Balanced, Waterfall, Spectrum };
enum class RotationMode:uint8_t { Manual, Auto, Locked };
struct Rect {
  int x=0,y=0,w=0,h=0;
  bool within(int W,int H) const {return x>=0 && y>=0 && w>=0 && h>=0 && x<=W && y<=H && w<=W-x && h<=H-y;}
};
struct DashboardLayout {
  Shape shape=Shape::Headless;
  Rect header,frequency,spectrum,waterfall,controls,status;
  bool valid=false;
};
inline int clamp(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}
inline Shape choose_shape(int w,int h){
  if(w<=0||h<=0)return Shape::Headless;
  if((int64_t)w*10 >= (int64_t)h*13)return Shape::Wide;
  if((int64_t)h*10 >= (int64_t)w*13)return Shape::Tall;
  return Shape::Square;
}
inline DashboardLayout make_layout(int w,int h,Focus focus=Focus::Balanced){
  DashboardLayout o; o.shape=choose_shape(w,h);
  if(o.shape==Shape::Headless || w<300 || h<300)return o;
  const int m=w<500?8:12,g=6,x=m,cw=w-2*m;
  const int hh=clamp(h/12,44,64),sh=clamp(h/16,32,60);
  const int fh=clamp(h/9,58,118),ch=clamp(h/7,70,142);
  const int core=h-hh-sh-fh-ch-5*g-2*m;
  if(core<90||cw<200)return o;
  int pct=o.shape==Shape::Tall?24:(o.shape==Shape::Wide?40:32);
  if(focus==Focus::Waterfall)pct=20;
  if(focus==Focus::Spectrum)pct=60;
  const int sp=core*pct/100, wf=core-sp;
  int y=m;
  o.header={x,y,cw,hh};y+=hh+g;
  o.frequency={x,y,cw,fh};y+=fh+g;
  o.spectrum={x,y,cw,sp};y+=sp+g;
  o.waterfall={x,y,cw,wf};y+=wf+g;
  o.controls={x,y,cw,ch};y+=ch+g;
  o.status={x,y,cw,sh};
  o.valid=o.header.within(w,h)&&o.frequency.within(w,h)&&o.spectrum.within(w,h)&&
      o.waterfall.within(w,h)&&o.controls.within(w,h)&&o.status.within(w,h);
  return o;
}
struct Gravity {float x,y,z;}; // accelerometer readings in g
struct AxisCalibration {
  // Physical axes to panel rotations MUST be calibrated on hardware.
  uint8_t positive_x=1,positive_y=0; // illustrative defaults only
};
class RotationController {
public:
  explicit RotationController(AxisCalibration a={}):axes_(a){}
  uint8_t rotation()const{return current_;}
  RotationMode mode()const{return mode_;}
  void configure(RotationMode m,uint8_t manual=0){
    mode_=m;pending_=-1;
    if(m==RotationMode::Manual)current_=manual&3;
  }
  bool update(Gravity g,uint64_t ms){
    if(mode_!=RotationMode::Auto)return false;
    float n=g.x*g.x+g.y*g.y+g.z*g.z;
    if(!std::isfinite(n)||n<0.75f*0.75f||n>1.25f*1.25f||std::fabs(g.z)>0.65f){
      pending_=-1;return false;
    }
    int want=-1;
    if(std::fabs(g.x)>0.78f&&std::fabs(g.x)>std::fabs(g.y)*1.65f)
      want=(axes_.positive_x+(g.x<0?2:0))&3;
    else if(std::fabs(g.y)>0.78f&&std::fabs(g.y)>std::fabs(g.x)*1.65f)
      want=(axes_.positive_y+(g.y<0?2:0))&3;
    if(want<0||want==current_){pending_=-1;return false;}
    if(want!=pending_){pending_=want;since_=ms;return false;}
    if(ms<since_||ms-since_<1250)return false;
    current_=static_cast<uint8_t>(want);pending_=-1;return true;
  }
private:
  AxisCalibration axes_;
  RotationMode mode_=RotationMode::Manual;
  uint8_t current_=0;
  int pending_=-1;
  uint64_t since_=0;
};
}
