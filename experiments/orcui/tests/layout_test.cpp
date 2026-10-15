#include "orcui/layout.hpp"
#include "orcui/raster.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace orcsdr::orcui;
bool within(Rect big,Rect inner){
 return inner.valid() && big.contains(inner.x,inner.y) &&
   big.contains(inner.right()-1,inner.bottom()-1);
}
bool overlap(Rect a,Rect b){
 return a.valid() && b.valid() && a.x<b.right() && b.x<a.right() &&
   a.y<b.bottom() && b.y<a.bottom();
}
void check(Display d,Form expected){
 const Layout l=plan(d);
 assert(l.valid && l.form==expected);
 assert(within(l.safe,l.header) && within(l.safe,l.menu_toggle));
 assert(within(l.safe,l.spectrum) && within(l.safe,l.waterfall));
 assert(within(l.safe,l.frequency) && within(l.safe,l.menu_overlay));
 assert(!overlap(l.spectrum,l.waterfall) && !overlap(l.waterfall,l.frequency));
 if(l.show_controls){
   assert(within(l.safe,l.controls) && !overlap(l.frequency,l.controls));
   for(int i=0;i<5;++i){
     assert(within(l.controls,l.control_buttons[i]));
     for(int j=i+1;j<5;++j)assert(!overlap(l.control_buttons[i],l.control_buttons[j]));
   }
 }
 if(l.show_sidebar)assert(within(l.safe,l.sidebar)&&!overlap(l.sidebar,l.spectrum));
 if(l.show_inspector)assert(within(l.safe,l.inspector)&&!overlap(l.inspector,l.spectrum));
 if(d.shape==Shape::circle){
   const double cx=d.width/2.0,cy=d.height/2.0,r=std::min(d.width,d.height)/2.0;
   for(int x:{l.safe.x,l.safe.right()-1})for(int y:{l.safe.y,l.safe.bottom()-1})
     assert(std::hypot(x-cx,y-cy)<=r+1);
 }
 std::printf("PASS %dx%d %s\n",d.width,d.height,form_name(l.form));
}
int main(){
 check({1280,720},Form::wide);
 check({720,720},Form::square);
 check({480,480,Shape::circle},Form::round);
 check({360,360,Shape::circle},Form::round);
 check({2560,720},Form::ultrawide);
 check({480,800},Form::compact);
 check({720,1280},Form::compact);
 check({800,480},Form::compact);
 check({1280,720,Shape::rectangle,{15,25,25,15}},Form::wide);
 assert(!plan({0,0}).valid);
 assert(!plan({200,200}).valid);
 assert(!plan({720,720,Shape::rectangle,{100,100,500,500}}).valid);
 for(int h=280;h<=1400;h+=53)for(int w=280;w<=2800;w+=71){
   const Layout l=plan({w,h});
   if(!l.valid)continue;
   assert(within(l.safe,l.spectrum)&&within(l.safe,l.waterfall));
   assert(within(l.safe,l.frequency)&&!overlap(l.spectrum,l.waterfall));
   if(l.show_controls)for(const Rect& button:l.control_buttons)
     assert(within(l.controls,button));
 }
 constexpr int width=73,height=31,stride=160;
 std::vector<uint8_t> bytes(stride*height,0xa5);
 Bitmap565 bitmap{reinterpret_cast<uint16_t*>(bytes.data()),width,height,stride};
 std::array<float,256> fft{};
 for(int i=0;i<256;++i)fft[i]=-110+35*std::sin(i/14.0f);
 paint_spectrum(bitmap,fft.data(),fft.size(),-120,-20);
 assert(bitmap.data[(height/2)*(stride/2)+width/2]==0x07ff);
 assert(bytes[stride-1]==0xa5);
 push_waterfall(bitmap,fft.data(),fft.size(),-120,-20);
 assert(bytes[stride-1]==0xa5);
 std::puts("PASS raster and invalid profiles");
}
