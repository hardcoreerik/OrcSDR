#include "map_view.hpp"
#include <M5GFX.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace orcsdr::maps {
RegionalView draw_region(lgfx::v1::LovyanGFX& display,int32_t latitude,int32_t longitude,
                         double range,int x,int y,int width,int height,
                         uint16_t background,uint16_t grid,uint16_t ink,const char* preferred,bool labels) {
  orcmap::Viewport camera;
  if(!regional_view(latitude,longitude,range,width,height,&camera))return {};
  return draw_camera(display,camera,x,y,background,grid,ink,preferred,labels);
}
RegionalView draw_camera(lgfx::v1::LovyanGFX& display,const orcmap::Viewport& camera,int x,int y,
                          uint16_t background,uint16_t grid,uint16_t ink,const char* preferred,bool labels,unsigned caption_size) {
  RegionalView result;result.camera=camera;
  const int width=camera.width_px,height=camera.height_px;
  result.valid=width>0&&height>0;
  if(!result.valid)return result;
  result.x=x;result.y=y;
  result.frame.state=draw(result.camera,display,x,y,&result.frame,preferred,labels);
  // Ready frames and cached previews are opaque. Clearing first briefly blanks
  // every dashboard map on data/age updates, despite preserving the cached view.
  // Pending may also mean a publication-lock miss: keep the previous visible
  // image then, rather than mistaking absent frame metadata for an empty map.
  if(result.frame.state==Frame::unavailable&&!result.frame.credit[0])
    display.fillRect(x,y,width,height,background);
  if(result.frame.state!=Frame::ready) {
    display.drawRect(x,y,width,height,grid);
    std::snprintf(result.frame.status,sizeof(result.frame.status),"%s",
        result.frame.state==Frame::pending ?
          (result.frame.credit[0] ? "UPDATING LOCAL MAP" : "LOADING LOCAL MAP") :
          "NO BASEMAP - COORDINATES ONLY");
  }
  display.setFont(nullptr);
  caption_size=std::clamp(caption_size,1u,2u);
  display.setTextSize(caption_size);display.setTextDatum(bottom_left);display.setTextColor(ink,background);
  const char* caption=result.frame.credit[0] ? result.frame.credit : result.frame.status;
  // Full required attribution, wrapped within the map instead of truncated.
  const int columns=std::min(192,width/int(6*caption_size));
  if(columns>0) {
    const size_t length=std::strlen(caption);
    const int lines=static_cast<int>((length+columns-1)/columns);
    for(int line=0;line<lines;++line) {
      char part[193]{};
      const size_t count=std::min(static_cast<size_t>(columns),length-line*columns);
      std::memcpy(part,caption+line*columns,count);
      display.drawString(part,x+2,y+height-2-(lines-1-line)*int(10*caption_size));
    }
  }
  return result;
}
bool project(const RegionalView& view,int32_t latitude,int32_t longitude,int* x,int* y) {
  if(!view.valid || !x || !y || latitude<-900000000 || latitude>900000000 ||
      longitude<-1800000000 || longitude>1800000000)return false;
  if(!project(view.camera,latitude,longitude,x,y))return false;
  *x+=view.x;*y+=view.y;return true;
}
} // namespace orcsdr::maps
