#include "ft8_map_source.hpp"
#include "ft8_world_projection.hpp"
#include "map_service.hpp"
#include <cstdio>
namespace orcsdr::ft8::map_source {
Result draw(lgfx::v1::LovyanGFX& target, bool labels) {
  // Exactly one complete Mercator world at either source zoom 0 or zoom 1.
  // The embedded firmware-world pack is available without Wi-Fi or SD.
  orcmap::Viewport view{};
  view.center_lat_deg=0.0;
  view.center_lon_deg=0.0;
  view.zoom=1;
  view.tile_size_px=world_map::kMercatorSide/2;
  view.width_px=view.height_px=world_map::kMercatorSide;
  maps::FrameInfo info{};
  const auto state=maps::draw(view,target,0,0,&info,"firmware-world",labels);
  Result result{};
  result.state=state==maps::Frame::ready ? State::ready :
               state==maps::Frame::pending ? State::pending : State::unavailable;
  result.low_memory=info.stats.out_of_memory;
  std::snprintf(result.credit,sizeof(result.credit),"%s",info.credit);
  std::snprintf(result.status,sizeof(result.status),"%s",info.status);
  return result;
}
uint32_t revision(){return maps::revision();}
}
