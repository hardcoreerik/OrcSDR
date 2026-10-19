#include "ft8_world_projection.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace orcsdr::ft8::world_map;
int main() {
  constexpr int w=824,h=420;
  int x=-1,y=-1,sx=-1,sy=-1;
  assert(project(0,0,w,h,&x,&y) && x==w/2 && y==h/2);
  assert(source_pixel(x,y,w,h,&sx,&sy) && std::abs(sx-256)<=1 && std::abs(sy-256)<=1);
  assert(project(90,-180,w,h,&x,&y) && x==0 && y==0);
  assert(project(-90,180,w,h,&x,&y) && x==0 && y==h-1);
  assert(project(0,-180,w,h,&x,&y) && x==0);
  assert(project(0,180,w,h,&x,&y) && x==0);
  assert(project(0,179.9,w,h,&x,&y) && x==w-1);
  assert(project(0,-179.9,w,h,&x,&y) && x==0);
  assert(!source_pixel(0,0,w,h,&sx,&sy));
  assert(!source_pixel(0,h-1,w,h,&sx,&sy));
  assert(source_pixel(0,h/2,w,h,&sx,&sy) && sx==0);
  assert(source_pixel(w-1,h/2,w,h,&sx,&sy) && sx==511);
  assert(!project(91,0,w,h,&x,&y) && !project(0,181,w,h,&x,&y));
  assert(!project(NAN,0,w,h,&x,&y) && !source_pixel(w,0,w,h,&sx,&sy));
  // The warped pixels and plotted locators agree to within one source pixel
  // at each sample centre (including high latitudes near the Mercator cutoff).
  for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx) {
    if(!source_pixel(xx,yy,w,h,&sx,&sy))continue;
    const double lat=90-(yy+.5)*180.0/h;
    const double lon=-180+(xx+.5)*360.0/w;
    int marker_x=0,marker_y=0;
    assert(project(lat,lon,w,h,&marker_x,&marker_y));
    assert(marker_x==xx && marker_y==yy);
    const double merc=(1.0-std::asinh(std::tan(lat*kPi/180))/kPi)*0.5*kMercatorSide;
    assert(std::abs(sy-merc)<=1);
    assert(std::abs(sx-(xx+.5)*kMercatorSide/w)<=1);
  }
  std::puts("ft8_world_projection_tests: PASS (full 824x420 pixel-centre sweep, seams, poles)");
}
