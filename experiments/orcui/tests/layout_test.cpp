#include "orcui/layout.hpp"
#include "orcui/raster.hpp"
#include <array>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace orcsdr::orcui;
// Assertions must execute in Release builds too.
#define CHECK(expr) do { if(!(expr)) { std::fprintf(stderr,"CHECK failed: %s:%d: %s\\n",__FILE__,__LINE__,#expr); std::abort(); } } while(0)
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
 CHECK(l.valid && l.form==expected);
 CHECK(within(l.safe,l.header) && within(l.safe,l.menu_toggle));
 CHECK(within(l.safe,l.spectrum) && within(l.safe,l.waterfall));
 CHECK(within(l.safe,l.frequency) && within(l.safe,l.menu_overlay));
 CHECK(!overlap(l.spectrum,l.waterfall) && !overlap(l.waterfall,l.frequency));
 if(l.show_controls){
   CHECK(within(l.safe,l.controls) && !overlap(l.frequency,l.controls));
   for(int i=0;i<5;++i){
     CHECK(within(l.controls,l.control_buttons[i]));
     CHECK(l.control_buttons[i].w>=44 && l.control_buttons[i].h>=44);
     for(int j=i+1;j<5;++j)CHECK(!overlap(l.control_buttons[i],l.control_buttons[j]));
   }
 }
 if(l.show_sidebar)CHECK(within(l.safe,l.sidebar)&&!overlap(l.sidebar,l.spectrum));
 if(l.show_inspector)CHECK(within(l.safe,l.inspector)&&!overlap(l.inspector,l.spectrum));
 if(d.shape==Shape::circle){
   const double cx=d.width/2.0,cy=d.height/2.0,r=std::min(d.width,d.height)/2.0;
   for(int x:{l.safe.x,l.safe.right()-1})for(int y:{l.safe.y,l.safe.bottom()-1})
     CHECK(std::hypot(x-cx,y-cy)<=r+1);
 }
 std::printf("PASS %dx%d %s\n",d.width,d.height,form_name(l.form));
}
bool same(Rect a,Rect b){return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h;}
// 800x480 (Hosyond 7") must get a deliberate landscape layout, not a squeezed
// portrait-style stack: the plots keep real height and every control is touchable.
void usable_landscape(){
 for(const Display d:{Display{800,480},Display{854,480},Display{640,360}}){
   const Layout l=plan(d);
   CHECK(l.valid && l.form==Form::landscape);
   CHECK(!l.show_sidebar && !l.show_inspector && l.show_controls);
   CHECK(l.header.h<=64);
   if(d.height>=480){CHECK(l.spectrum.h*100>=d.height*20 && l.waterfall.h*100>=d.height*24);}
   else {CHECK(l.spectrum.h>=48 && l.waterfall.h>=60);} // 640x360: smallest supported landscape
   CHECK(l.spectrum.w>=d.width*9/10);
   for(const Rect& b:l.control_buttons){CHECK(b.w>=44 && b.h>=44);}
   CHECK(l.control_buttons[0].y==l.control_buttons[4].y); // one row
   CHECK(l.menu_toggle.w>=44 && l.menu_toggle.h>=44);
   // Drawer rows: each retains a 44 px touch height and stays inside the overlay
   // for as many rows as fit.
   int fit=0;
   for(int i=0;i<12;++i){
     const Rect r=menu_row(l,i);
     CHECK(r.h>=44);
     if(r.bottom()<=l.menu_overlay.bottom()){CHECK(within(l.menu_overlay,r));++fit;}
   }
   CHECK(fit>=4);
 }
 const Layout l=plan({800,480});
 std::printf("INFO 800x480 landscape: header %d, spectrum %d, waterfall %d, readout %d, controls %d\n",
   l.header.h,l.spectrum.h,l.waterfall.h,l.frequency.h,l.controls.h);
 std::puts("PASS landscape usability");
}
// plan() is pure: the same display gives the same geometry, and rotating or resizing
// between supported sizes never produces an invalid intermediate layout.
void resize_continuity(){
 const Display sizes[]={{800,480},{480,800},{1280,720},{720,720},{854,480},{640,360},{800,480,Shape::rectangle,{10,10,10,10}}};
 for(const Display a:sizes){
   const Layout x=plan(a),y=plan(a);
   CHECK(x.valid && x.form==y.form && same(x.spectrum,y.spectrum) && same(x.controls,y.controls));
   for(const Display b:sizes){
     CHECK(plan(b).valid);
   }
 }
 const Layout rot=plan({480,800});
 CHECK(rot.valid && rot.form==Form::compact);
 std::puts("PASS resize continuity");
}
int main(){
 check({1280,720},Form::wide);
 check({720,720},Form::square);
 check({480,480,Shape::circle},Form::round);
 check({360,360,Shape::circle},Form::round);
 check({2560,720},Form::ultrawide);
 check({480,800},Form::compact);
 check({720,1280},Form::compact);
 check({800,480},Form::landscape);
 check({854,480},Form::landscape);
 check({640,360},Form::landscape);
 check({1024,600},Form::wide);
 check({600,1024},Form::compact);
 check({480,854},Form::compact);
 check({1280,720,Shape::rectangle,{15,25,25,15}},Form::wide);
 usable_landscape();
 resize_continuity();
 CHECK(!plan({0,0}).valid);
 CHECK(!plan({200,200}).valid);
 CHECK(!plan({720,720,Shape::rectangle,{100,100,500,500}}).valid);
 for(int h=280;h<=1400;h+=53)for(int w=280;w<=2800;w+=71){
   const Layout l=plan({w,h});
   if(!l.valid)continue;
   CHECK(within(l.safe,l.spectrum)&&within(l.safe,l.waterfall));
   CHECK(within(l.safe,l.frequency)&&!overlap(l.spectrum,l.waterfall));
   if(l.show_controls)for(const Rect& button:l.control_buttons)
     CHECK(within(l.controls,button));
 }
 constexpr int width=73,height=31,stride=160;
 std::vector<uint8_t> bytes(stride*height,0xa5);
 Bitmap565 bitmap{reinterpret_cast<uint16_t*>(bytes.data()),width,height,stride};
 std::array<float,256> fft{};
 for(int i=0;i<256;++i)fft[i]=-110+35*std::sin(i/14.0f);
 paint_spectrum(bitmap,fft.data(),fft.size(),-120,-20);
 CHECK(bitmap.data[(height/2)*(stride/2)+width/2]==0x07ff);
 CHECK(bytes[stride-1]==0xa5);
 push_waterfall(bitmap,fft.data(),fft.size(),-120,-20);
 CHECK(bytes[stride-1]==0xa5);
 std::puts("PASS raster and invalid profiles");
}
