#pragma once
// Pure, host-testable map-centering policy for the AIS dashboard.
// The receiver's own location is optional; actual decoded AIS coordinates
// can still provide a map center. No map/renderer/DSP ownership here.
#include "ais_dashboard.hpp"
#include <cstddef>
#include <cstdint>

namespace orcsdr::ais_ui {
enum class CenterOrigin : uint8_t { none, receiver, ais_target };
struct MapCenter {
  CenterOrigin origin=CenterOrigin::none;
  int32_t lat_e7=0,lon_e7=0;
  uint32_t identity=0;
  IdentityKind identity_kind=IdentityKind::mmsi;
  bool valid() const {return origin!=CenterOrigin::none;}
};
inline uint32_t map_target_id(const Target& t) {
  return t.identity_kind==IdentityKind::source_id28?t.source_id28:t.mmsi;
}
inline bool map_coordinate_valid(int32_t lat_e7,int32_t lon_e7) {
  return lat_e7>=-850511287&&lat_e7<=850511287 &&
         lon_e7>=-1800000000&&lon_e7<=1800000000;
}
inline bool map_target_valid(const Target& t) {
  return t.has_position && !t.position_stale &&
         map_coordinate_valid(t.lat_e7,t.lon_e7);
}
// Preference: real receiver position, explicitly selected valid station,
// previously anchored station, then first valid position. This prevents
// changing map center merely because a snapshot's station order changed.
inline MapCenter choose_map_center(
    const Snapshot& snapshot,
    IdentityKind selected_kind=IdentityKind::mmsi,uint32_t selected_id=0,bool has_selection=false,
    IdentityKind anchor_kind=IdentityKind::mmsi,uint32_t anchor_id=0,bool has_anchor=false) {
  if(snapshot.coordinates_valid &&
     map_coordinate_valid(snapshot.receiver_lat_e7,snapshot.receiver_lon_e7))
    return {CenterOrigin::receiver,snapshot.receiver_lat_e7,snapshot.receiver_lon_e7,0,IdentityKind::mmsi};
  const size_t count=snapshot.target_count<kTargets?snapshot.target_count:kTargets;
  auto match=[&](IdentityKind kind,uint32_t id)->MapCenter {
    for(size_t i=0;i<count;++i) {
      const Target& t=snapshot.targets[i];
      if(t.identity_kind!=kind||map_target_id(t)!=id||!map_target_valid(t))continue;
      return {CenterOrigin::ais_target,t.lat_e7,t.lon_e7,id,kind};
    }
    return {};
  };
  if(has_selection) {
    MapCenter selected=match(selected_kind,selected_id);
    if(selected.valid())return selected;
  }
  if(has_anchor) {
    MapCenter anchor=match(anchor_kind,anchor_id);
    if(anchor.valid())return anchor;
  }
  for(size_t i=0;i<count;++i) {
    const Target& t=snapshot.targets[i];
    if(map_target_valid(t))
      return {CenterOrigin::ais_target,t.lat_e7,t.lon_e7,map_target_id(t),t.identity_kind};
  }
  return {};
}
} // namespace orcsdr::ais_ui
