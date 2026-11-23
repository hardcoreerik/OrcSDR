// Original station-store -> UI presentation tests. No device or RF signals.
#include "ais_snapshot_adapter.hpp"
#include "ais_map_policy.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace orcsdr;
namespace {
ais::Message pos(uint32_t id,int32_t lat,int32_t lon,uint8_t type=1) {
  ais::Message m{};m.type=type;m.source_id=id;
  m.coverage=ais::Coverage::position;m.has_position=true;
  m.latitude_e7=lat;m.longitude_e7=lon;
  return m;
}
void test_identity_age_history() {
  static ais::StationStore store;
  store.clear();
  auto vessel=pos(123456789,474000000,-1223000000);
  assert(store.ingest(vessel,1000));
  vessel.latitude_e7=475000000;vessel.longitude_e7=-1222000000;
  assert(store.ingest(vessel,2000));
  ais::Message aid{};
  aid.type=28;aid.source_id=123456789;aid.coverage=ais::Coverage::aid28;
  aid.has_position=true;aid.latitude_e7=486000000;aid.longitude_e7=-1226000000;
  assert(store.ingest(aid,6000));
  static ais_ui::Snapshot snapshot;
  snapshot.revision=22;snapshot.crc_ok[0]=17;
  ais_ui::populate_targets(store,9000,5000,&snapshot);
  assert(snapshot.revision==22&&snapshot.crc_ok[0]==17);
  assert(snapshot.target_count==2);
  const auto& top=snapshot.targets[0],&second=snapshot.targets[1];
  assert(top.identity_kind==ais_ui::IdentityKind::source_id28);
  assert(top.source_id28==123456789&&top.mmsi==0);
  assert(top.station==ais_ui::Station::aid_2026);
  assert(top.has_position&&!top.position_stale&&top.position_age_s==3);
  assert(top.history_count==1);
  assert(second.identity_kind==ais_ui::IdentityKind::mmsi&&second.mmsi==123456789);
  assert(second.source_id28==0&&second.station==ais_ui::Station::vessel_a);
  assert(second.has_position&&second.position_stale);
  assert(second.position_age_s==7&&second.heard_age_s==7);
  assert(second.history_count==2);
  assert(second.history[0].age_s==8&&second.history[1].age_s==7);
  // Decoded Type28 Source ID and MMSI remain distinct in map selection.
  const auto center=ais_ui::choose_map_center(snapshot,ais_ui::IdentityKind::source_id28,
                                             123456789,true);
  assert(center.origin==ais_ui::CenterOrigin::ais_target);
  assert(center.lat_e7==486000000);
  store.clear();
  ais_ui::populate_targets(store,10000,5000,&snapshot);
  assert(snapshot.target_count==0&&snapshot.targets[0].mmsi==0);
  ais_ui::populate_targets(store,11000,5000,nullptr);
}
void test_high_density_priority_and_bounds() {
  static ais::StationStore store;
  store.clear();
  for(uint32_t i=0;i<64;++i) {
    auto m=pos(200000000+i,400000000+static_cast<int32_t>(i)*1000,-1200000000);
    assert(store.ingest(m,1000+i*100));
  }
  static ais_ui::Snapshot snapshot;
  ais_ui::populate_targets(store,10000,5000,&snapshot);
  assert(snapshot.target_count==ais_ui::kTargets);
  assert(snapshot.targets[0].mmsi==200000063);
  assert(snapshot.targets[31].mmsi==200000032);
  for(size_t i=0;i<snapshot.target_count;++i) {
    assert(snapshot.targets[i].history_count==1);
    if(i)assert(snapshot.targets[i-1].heard_age_s<=snapshot.targets[i].heard_age_s);
  }
}
void test_missing_fix_and_sentinel() {
  static ais::StationStore store;
  store.clear();
  ais::Message no_fix{};no_fix.type=21;no_fix.source_id=992000001;
  no_fix.coverage=ais::Coverage::aid21;no_fix.aid.is_virtual=true;
  std::strcpy(no_fix.aid.name,"TEST VIRTUAL AtoN");
  assert(store.ingest(no_fix,100));
  static ais_ui::Snapshot snapshot;
  ais_ui::populate_targets(store,500,60000,&snapshot);
  assert(snapshot.target_count==1);
  const auto& t=snapshot.targets[0];
  assert(t.station==ais_ui::Station::aid_virtual);
  assert(!t.has_position&&t.position_stale&&t.position_age_s==0);
  assert(t.history_count==0&&std::strcmp(t.name,"TEST VIRTUAL AtoN")==0);
}
}
int main() {
  test_identity_age_history();
  test_high_density_priority_and_bounds();
  test_missing_fix_and_sentinel();
  std::puts("AIS real-station snapshot/identity/freshness/track host tests passed");
}
