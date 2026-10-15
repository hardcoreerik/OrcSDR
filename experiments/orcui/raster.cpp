#include "orcui/raster.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace orcsdr::orcui {
namespace {
constexpr uint16_t black=0x0000,grid=0x2104,cyan=0x07ff,green=0x07e0,yellow=0xffe0;
bool valid(Bitmap565 b){
  return b.data && b.width>0 && b.height>0 &&
    b.stride_bytes%alignof(uint16_t)==0 &&
    reinterpret_cast<uintptr_t>(b.data)%alignof(uint16_t)==0 &&
    b.stride_bytes>=static_cast<size_t>(b.width)*2 &&
    b.width<20000 && b.height<20000;
}
uint16_t* row(Bitmap565 b,int y){
  return reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(b.data)+y*b.stride_bytes);
}
float bin_at(const float* values,size_t n,int x,int w){
  if(!values || !n)return -120.0f;
  if(n==1 || w<=1)return std::isfinite(values[0])?values[0]:-120.0f;
  const double lo=static_cast<double>(x)*n/w;
  const double hi=static_cast<double>(x+1)*n/w;
  if(hi-lo>=1){
    const size_t begin=std::min(n-1,static_cast<size_t>(lo));
    const size_t end=std::min(n,std::max(begin+1,static_cast<size_t>(std::ceil(hi))));
    float peak=-120.0f;
    for(size_t i=begin;i<end;++i)if(std::isfinite(values[i]))peak=std::max(peak,values[i]);
    return peak;
  }
  const double pos=static_cast<double>(x)*(n-1)/(w-1);
  const size_t left=std::min(n-1,static_cast<size_t>(pos));
  const size_t right=std::min(n-1,left+1);
  const double t=pos-left;
  const float a=std::isfinite(values[left])?values[left]:-120.0f;
  const float b=std::isfinite(values[right])?values[right]:-120.0f;
  return static_cast<float>(a+(b-a)*t);
}
float normalized(float level,float min_db,float max_db){
  if(!std::isfinite(level)||!std::isfinite(min_db)||!std::isfinite(max_db)||max_db<=min_db)return 0;
  return std::clamp((level-min_db)/(max_db-min_db),0.0f,1.0f);
}
uint16_t palette(float t){
  t=std::clamp(t,0.0f,1.0f);
  const uint8_t r=static_cast<uint8_t>(t<.65f?0:std::min(255.0f,(t-.65f)*728.6f));
  const uint8_t g=static_cast<uint8_t>(t<.20f?0:std::min(255.0f,(t-.20f)*318.75f));
  const uint8_t b=static_cast<uint8_t>(t<.65f?35+t*320.0f:(1.0f-t)*180.0f);
  return static_cast<uint16_t>(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}
}
void paint_spectrum(Bitmap565 out,const float* levels,size_t bins,float min_db,float max_db){
  if(!valid(out)||!levels||!bins)return;
  for(int y=0;y<out.height;++y){
    uint16_t* p=row(out,y);
    for(int x=0;x<out.width;++x){
      p[x]=((x>0 && x%std::max(1,out.width/6)==0) ||
          (y>0 && y%std::max(1,out.height/4)==0))?grid:black;
    }
  }
  for(int x=0;x<out.width;++x){
    const float t=normalized(bin_at(levels,bins,x,out.width),min_db,max_db);
    const int peak_y=std::clamp((out.height-1)-static_cast<int>(t*(out.height-1)),0,out.height-1);
    for(int y=out.height-1;y>=peak_y;--y)
      row(out,y)[x]=(y==peak_y?yellow:(y-peak_y<3?green:0x0180));
  }
  for(int y=0;y<out.height;++y)row(out,y)[out.width/2]=cyan;
}
void push_waterfall(Bitmap565 out,const float* levels,size_t bins,float min_db,float max_db,const uint16_t* palette256){
  if(!valid(out)||!levels||!bins)return;
  uint8_t* bytes=reinterpret_cast<uint8_t*>(out.data);
  if(out.height>1)std::memmove(bytes,bytes+out.stride_bytes,
                             out.stride_bytes*static_cast<size_t>(out.height-1));
  uint16_t* p=row(out,out.height-1);
  for(int x=0;x<out.width;++x)
    {
    const float t=normalized(bin_at(levels,bins,x,out.width),min_db,max_db);
    p[x]=palette256?palette256[static_cast<unsigned>(t*255)]:palette(t);
    }
}
}