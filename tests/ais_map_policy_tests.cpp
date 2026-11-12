// Pure map-centering policy tests. All targets are synthetic fixtures.
#include "ais_map_policy.hpp"
#include <cassert>
#include <cstdio>

using namespace orcsdr::ais_ui;
namespace {
void test_unknown_receiver_falls_back_to_real_target() {
  Snapshot s{};
  assert(!choose_map_center(s).valid());
  s.target_count=2;
  s.targets[0].mmsi=101000001;
  s.targets[0].has_position=true;
  s.targets[0].lat_e7=474000000;s.targets[0].lon_e7=-1223000000;
  s.targets[1].mmsi=101000002;
  s.targets[1].has_position=true;
  s.targets[1].lat_e7=485000000;s.targets[1].lon_e7=-1233000000;
  auto c=choose_map_center(s);
  assert(c.valid()&&c.origin==CenterOrigin::ais_target);
  assert(c.identity==101000001&&c.lat_e7==474000000);
  // Reordering reports must not jump the camera if the same anchor remains valid.
  const Target temp=s.targets[0];s.targets[0]=s.targets[1];s.targets[1]=temp;
  c=choose_map_center(s,IdentityKind::mmsi,0,false,
                      IdentityKind::mmsi,101000001,true);
  assert(c.identity==101000001&&c.lat_e7==474000000);
  c=choose_map_center(s,IdentityKind::mmsi,101000002,true,
                      IdentityKind::mmsi,101000001,true);
  assert(c.identity==101000002);
  // Receiver location wins when it is valid, including the real 0,0 location.
  s.coordinates_valid=true;s.receiver_lat_e7=0;s.receiver_lon_e7=0;
  c=choose_map_center(s);
  assert(c.origin==CenterOrigin::receiver&&c.lat_e7==0&&c.lon_e7==0);
  // A receiver fix beyond Web Mercator bounds does not hide valid target fixes.
  s.receiver_lat_e7=895000000;
  c=choose_map_center(s);
  assert(c.origin==CenterOrigin::ais_target);
}
void test_identity_and_freshness() {
  Snapshot s{};
  s.target_count=3;
  auto& m=s.targets[0];m.mmsi=123456789;m.has_position=true;
  m.lat_e7=123000000;m.lon_e7=234000000;m.position_stale=true;
  auto& source=s.targets[1];
  source.identity_kind=IdentityKind::source_id28;
  source.source_id28=123456789;
  source.has_position=true;source.lat_e7=234000000;source.lon_e7=456000000;
  auto& conventional=s.targets[2];
  conventional.mmsi=123456789;
  conventional.has_position=true;conventional.lat_e7=240000000;conventional.lon_e7=450000000;
  auto c=choose_map_center(s,IdentityKind::mmsi,123456789,true);
  assert(c.origin==CenterOrigin::ais_target && c.identity_kind==IdentityKind::mmsi);
  assert(c.lat_e7==240000000);
  c=choose_map_center(s,IdentityKind::source_id28,123456789,true);
  assert(c.identity_kind==IdentityKind::source_id28&&c.lat_e7==234000000);
  assert(map_target_id(source)==123456789&&map_target_id(conventional)==123456789);
  source.lat_e7=900000000;
  assert(!map_target_valid(source));
  conventional.position_stale=true;
  assert(!choose_map_center(s).valid()); // stale or unprojectable positions never anchor.
  conventional.position_stale=false;conventional.lat_e7=0;conventional.lon_e7=0;
  c=choose_map_center(s);
  assert(c.valid()&&c.lat_e7==0&&c.lon_e7==0); // 0,0 can be valid when explicitly reported.
}
}
int main() {
  test_unknown_receiver_falls_back_to_real_target();
  test_identity_and_freshness();
  std::puts("AIS map center, stable identity and stale-coordinate policy host tests passed");
}
