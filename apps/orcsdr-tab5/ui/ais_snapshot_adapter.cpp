#include "ais_snapshot_adapter.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

namespace orcsdr::ais_ui {
namespace {
uint32_t age_seconds(uint64_t now_ms,uint64_t then_ms) {
  if (now_ms < then_ms) return UINT32_MAX;
  const uint64_t seconds=(now_ms-then_ms)/1000;
  return static_cast<uint32_t>(std::min<uint64_t>(seconds,UINT32_MAX));
}
Station to_ui_station(ais::StationKind kind) {
  switch(kind) {
    case ais::StationKind::class_a: return Station::vessel_a;
    case ais::StationKind::class_b: return Station::vessel_b;
    case ais::StationKind::aid21_physical: return Station::aid_physical;
    case ais::StationKind::aid21_virtual: return Station::aid_virtual;
    case ais::StationKind::aid28: return Station::aid_2026;
    case ais::StationKind::long_range: return Station::other;
  }
  return Station::other;
}
bool before(const ais::Station& left,const ais::Station& right) {
  if(left.last_rx_ms!=right.last_rx_ms)return left.last_rx_ms>right.last_rx_ms;
  if(left.identity_kind!=right.identity_kind)
    return static_cast<unsigned>(left.identity_kind)<static_cast<unsigned>(right.identity_kind);
  return left.identity<right.identity;
}
void fill(const ais::Station& source,const ais::StationStore& store,
          uint64_t now_ms,uint64_t stale_after_ms,Target* target) {
  Target t{};
  t.identity_kind=source.identity_kind==ais::IdentityKind::source_id28
                    ? IdentityKind::source_id28 : IdentityKind::mmsi;
  if(t.identity_kind==IdentityKind::source_id28)t.source_id28=source.identity;
  else t.mmsi=source.identity;
  t.station=to_ui_station(source.kind);
  t.last_type=source.last_type;
  std::memcpy(t.name,source.name,sizeof(t.name)-1);
  t.name[sizeof(t.name)-1]='\0';
  t.has_position=source.has_position;
  t.position_stale=store.position_stale(source,now_ms,stale_after_ms);
  if(t.has_position) {
    t.lat_e7=source.lat_e7;t.lon_e7=source.lon_e7;
    t.position_age_s=age_seconds(now_ms,source.last_position_ms);
  }
  t.heard_age_s=age_seconds(now_ms,source.last_rx_ms);
  t.has_speed=source.has_speed;t.speed_tenths_kn=source.speed_tenths_kn;
  t.has_course=source.has_course;t.course_tenths_deg=source.course_tenths_deg;
  t.has_heading=source.has_heading;t.heading_deg=source.heading_deg;
  t.history_count=static_cast<uint8_t>(
      std::min<size_t>(source.history_size,sizeof(t.history)/sizeof(t.history[0])));
  for(size_t i=0;i<t.history_count;++i) {
    t.history[i]={source.history[i].lat_e7,source.history[i].lon_e7,
                  age_seconds(now_ms,source.history[i].received_ms)};
  }
  *target=t;
}
} // namespace

void populate_targets(const ais::StationStore& stations,
                      uint64_t now_ms,uint64_t stale_after_ms,Snapshot* snapshot) {
  if(!snapshot)return;
  // Keep the newest 32 of up to 64 stations; stable tie-breaking avoids
  // random rearrangements without retaining UI pointers into receiver memory.
  size_t ordered[kTargets]{};
  size_t count=0;
  for(size_t source_idx=0;source_idx<stations.size();++source_idx) {
    const ais::Station* source=stations.at(source_idx);
    if(!source)continue;
    size_t at=0;
    while(at<count) {
      const ais::Station* existing=stations.at(ordered[at]);
      if(existing && before(*source,*existing))break;
      ++at;
    }
    if(at>=kTargets)continue;
    if(count<kTargets)++count;
    for(size_t j=count-1;j>at;--j)ordered[j]=ordered[j-1];
    ordered[at]=source_idx;
  }
  for(size_t i=0;i<kTargets;++i)snapshot->targets[i]=Target{};
  for(size_t i=0;i<count;++i) {
    const ais::Station* s=stations.at(ordered[i]);
    if(s)fill(*s,stations,now_ms,stale_after_ms,&snapshot->targets[i]);
  }
  snapshot->target_count=static_cast<uint8_t>(count);
}
} // namespace orcsdr::ais_ui
