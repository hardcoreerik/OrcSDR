// Portable specification-derived fixtures. Not real RF or independent field-by-field oracle evidence.
#include "ais_protocol.hpp"
#include "ais_station_store.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
using namespace orcsdr::ais;
namespace {
struct Fixture {
  uint8_t bytes[kMaxMessageBytes]{};
  void put(size_t at,unsigned width,uint32_t value) {
    assert(width<=32 && at+width <= kMaxMessageBits);
    for (unsigned i=0;i<width;++i) {
      const size_t b=at+i;
      const uint8_t mask=static_cast<uint8_t>(1u << (7-(b%8)));
      if ((value >> (width-i-1))&1u) bytes[b/8]|=mask;
      else bytes[b/8]&=static_cast<uint8_t>(~mask);
    }
  }
  void signed_field(size_t at,unsigned bits,int32_t value) { put(at,bits,static_cast<uint32_t>(value)); }
  void text(size_t at,const char* value,size_t chars) {
    const size_t available=std::strlen(value);
    for(size_t i=0;i<chars;++i) {
      const char c=i<available ? value[i] : '@';
      put(at+6*i,6,c>=64?static_cast<unsigned>(c-64):static_cast<unsigned>(c));
    }
  }
  void header(uint8_t type,uint32_t id) { put(0,6,type);put(6,2,0);put(8,30,id); }
};
void coverage_and_bounds() {
  for(uint8_t type=1;type<=28;++type) {
    const TypeLength range=type_length(type);
    assert(range.minimum>=38 && range.maximum<=kMaxMessageBits && range.maximum>=range.minimum);
    Fixture f;f.header(type,123000000u+type);
    Message m{}; assert(parse(f.bytes,range.minimum,&m)==ParseStatus::ok);
    assert(m.type==type && m.source_id==123000000u+type);
    assert(parse(f.bytes,range.minimum-1,&m)==ParseStatus::invalid_length || range.minimum-1<38);
    assert(parse(f.bytes,range.maximum+1,&m)!=ParseStatus::ok);
  }
  Fixture f; f.header(63,1);Message m{};
  assert(parse(f.bytes,168,&m)==ParseStatus::invalid_type);
  assert(parse(nullptr,168,&m)==ParseStatus::invalid_input);
  assert(parse(f.bytes,168,nullptr)==ParseStatus::invalid_input);
  assert(parse(f.bytes,2000,&m)==ParseStatus::invalid_input);
}
void class_a_and_class_b() {
  Fixture f; f.header(1,366967101);f.put(50,10,123);
  f.signed_field(61,28,-73380000); // -122.3 degrees
  f.signed_field(89,27,28440000);  // 47.4 degrees
  f.put(116,12,1845);f.put(128,9,184);
  Message m{}; assert(parse(f.bytes,168,&m)==ParseStatus::ok);
  assert(m.coverage==Coverage::position && m.has_position);
  assert(m.longitude_e7==-1223000000 && m.latitude_e7==474000000);
  assert(m.has_course && m.course_tenths_deg==1845);
  assert(m.has_heading && m.heading_deg==184);
  // Type 18 shifts the position fields: its reserved bits occupy a different offset.
  Fixture b;b.header(18,366967118);b.put(46,10,71);
  b.signed_field(57,28,-73380000);b.signed_field(85,27,28440000);
  b.put(112,12,901);b.put(124,9,90);
  assert(parse(b.bytes,168,&m)==ParseStatus::ok && m.has_position);
  assert(m.has_speed && m.speed_tenths_kn==71);
  assert(m.course_tenths_deg==901 && m.heading_deg==90);
  // AIS unavailable sentinels must never be mistaken for a position at the origin.
  f.signed_field(61,28,108600000);f.signed_field(89,27,54600000);
  f.put(50,10,1023);f.put(116,12,3600);f.put(128,9,511);
  assert(parse(f.bytes,168,&m)==ParseStatus::ok);
  assert(!m.has_position && !m.has_speed && !m.has_course && !m.has_heading);
}
void aid21_extended_name() {
  Fixture f;f.header(21,992345678);f.put(38,5,17);
  f.text(43,"BUOY NAME TEST",20);
  f.signed_field(164,28,-73410000);f.signed_field(192,27,28410000);
  f.put(269,1,1);f.text(272," EXT",4);
  Message m{};assert(parse(f.bytes,296,&m)==ParseStatus::ok);
  assert(m.coverage==Coverage::aid21 && m.has_position);
  assert(m.aid.virtual_flag_valid && m.aid.is_virtual);
  assert(std::strstr(m.aid.name,"BUOY NAME TEST")!=nullptr);
  assert(std::strstr(m.aid.name,"EXT")!=nullptr);
  assert(parse(f.bytes,271,&m)==ParseStatus::invalid_length);
}
void long_range27() {
  Fixture f;f.header(27,366967127);
  f.signed_field(44,18,-73380);f.signed_field(62,17,28440);
  f.put(79,6,21);f.put(85,9,150);
  Message m{};assert(parse(f.bytes,96,&m)==ParseStatus::ok);
  assert(m.coverage==Coverage::long_range27 && m.has_position);
  assert(m.longitude_e7==-1223000000 && m.latitude_e7==474000000);
  assert(m.speed_tenths_kn==210 && m.course_tenths_deg==1500);
  f.signed_field(44,18,108600);f.signed_field(62,17,54600);
  f.put(79,6,63);f.put(85,9,511);
  assert(parse(f.bytes,96,&m)==ParseStatus::ok);
  assert(!m.has_position && !m.has_speed && !m.has_course);
}
void type28_field_map() {
  Fixture f;f.header(28,123456789);
  f.put(38,6,27);f.signed_field(44,28,-73380000);f.signed_field(72,27,28440000);
  f.put(99,2,2);f.put(101,3,5);f.put(104,7,63);
  f.put(111,17,89012);f.put(128,4,11);f.put(132,9,345);f.put(141,11,1823);
  f.put(152,1,1);f.put(153,1,1);f.put(154,4,9);f.put(158,8,176);
  f.put(166,1,0);f.put(167,1,1);
  Message m{};assert(parse(f.bytes,168,&m)==ParseStatus::ok);
  assert(m.coverage==Coverage::aid28 && m.source_id==123456789 && m.has_position);
  assert(m.aid.timestamp==27 && m.aid.restricted_use==2 && m.aid.station_type==5);
  assert(m.aid.aid_type==63 && m.aid.iala_mrn==89012 && m.aid.dimension_type==11);
  assert(m.aid.dimension_a==345 && m.aid.dimension_b==1823);
  assert(m.aid.additional_data && m.aid.charted && m.aid.on_station==9);
  assert(m.aid.status==176 && m.aid.authenticated);
  assert(!m.aid.virtual_flag_valid); // Type 28's station type is not Type 21's virtual flag.
  assert(parse(f.bytes,167,&m)==ParseStatus::invalid_length);
}
void station_store() {
  static StationStore store;
  Fixture f;f.header(1,777888999);
  f.signed_field(61,28,-73380000);f.signed_field(89,27,28440000);
  Message m{};assert(parse(f.bytes,168,&m)==ParseStatus::ok);
  assert(store.ingest(m,1000));
  assert(store.size()==1);
  auto* s=store.find(777888999);assert(s && s->history_size==1 && !store.position_stale(*s,1100,200));
  assert(store.position_stale(*s,1201,200));
  f.signed_field(61,28,108600000);f.signed_field(89,27,54600000);
  assert(parse(f.bytes,168,&m)==ParseStatus::ok);
  assert(store.ingest(m,2000));
  s=store.find(777888999);assert(s && s->last_rx_ms==2000 && s->last_position_ms==1000);
  assert(store.position_stale(*s,2000,500));
  assert(!store.ingest(m,1500)); // out-of-order observation
  // Type 28 Source ID is a separate identity namespace even when numerically equal to MMSI.
  Fixture a;a.header(28,777888999);
  assert(parse(a.bytes,168,&m)==ParseStatus::ok && store.ingest(m,2200));
  assert(store.size()==2 && store.find(777888999,IdentityKind::source_id28));
  for(int i=0;i<15;++i) {
    f.signed_field(61,28,-73380000+i*6000);
    f.signed_field(89,27,28440000+i*6000);
    assert(parse(f.bytes,168,&m)==ParseStatus::ok && store.ingest(m,3000+i*100));
  }
  s=store.find(777888999);assert(s && s->history_size==StationStore::kHistory);
  assert(s->history[7].received_ms==4400);
  store.prune(10000,6000);
  assert(store.size()==1); // last-positioned vessel kept; older Type 28 expires.
  store.clear();assert(store.size()==0);
  // Bounded capacity rejects new targets rather than silently evicting existing ones.
  for(size_t i=0;i<StationStore::kCapacity;++i) {
    Fixture x;x.header(1,200000000u+static_cast<uint32_t>(i));
    assert(parse(x.bytes,168,&m)==ParseStatus::ok && store.ingest(m,100+i));
  }
  assert(store.size()==StationStore::kCapacity);
  Fixture x;x.header(1,333333333);
  assert(parse(x.bytes,168,&m)==ParseStatus::ok && !store.ingest(m,99999));
}
// Payloads below were produced by tools/ais_make_iq.py, an encoder written separately from this parser from the
// ITU-R M.1371-5 field tables, and its output was decoded field for field by the third-party pyais 2.9.4. They are
// therefore an independent check of the offsets used in ais_protocol.cpp, unlike the Fixture cases above.
size_t from_hex(const char* hex,uint8_t* out,size_t capacity) {
  size_t n=0;
  auto nibble=[](char c){return static_cast<uint8_t>(c<='9'?c-'0':c-'A'+10);};
  for(;hex[0]&&hex[1]&&n<capacity;hex+=2)out[n++]=static_cast<uint8_t>((nibble(hex[0])<<4)|nibble(hex[1]));
  return n;
}
void independent_fixtures() {
  uint8_t p[kMaxMessageBytes]{};
  Message m{};
  // Type 1, under way using engine, 12.3 kn, course and heading 090.
  size_t n=from_hex("0417D784040007BDC9039C9A728203842D00000000",p,sizeof(p));
  assert(n==21 && parse(p,168,&m)==ParseStatus::ok);
  assert(m.type==1 && m.source_id==100000001 && m.has_position && m.latitude_e7==462200000);
  assert(m.has_nav_status && m.nav_status==0 && m.has_speed && m.speed_tenths_kn==123);
  assert(m.has_course && m.course_tenths_deg==900 && m.has_heading && m.heading_deg==90);
  // Type 2, at anchor: speed 0.0 is a value, not "unavailable".
  n=from_hex("0817D7840C40000DC99BE81A76E700000004000000",p,sizeof(p));
  assert(parse(p,168,&m)==ParseStatus::ok && m.type==2 && m.source_id==100000003);
  assert(m.has_nav_status && m.nav_status==1 && m.has_speed && m.speed_tenths_kn==0);
  assert(m.latitude_e7==462500000 && m.longitude_e7==-1237300000);
  // Type 4 base station report.
  n=from_hex("10003D09047EAB530013724F8A069ED301C0000000",p,sizeof(p));
  assert(parse(p,168,&m)==ParseStatus::ok && m.type==4 && m.source_id==1000001);
  assert(m.coverage==Coverage::base_station && m.has_position);
  assert(m.latitude_e7==462800000 && m.longitude_e7==-1238100000 && !m.has_speed && !m.has_nav_status);
  // Type 5 static and voyage data.
  n=from_hex("1417D78404022B6B854153501C403D20D3112814153520071000000000462D078A286D887901415352040F4940"
             "0000000000000000",p,sizeof(p));
  assert(n==53 && parse(p,424,&m)==ParseStatus::ok && m.type==5 && m.source_id==100000001);
  assert(m.coverage==Coverage::static_voyage && !m.has_position);
  assert(m.vessel.imo==9100001 && m.vessel.has_callsign && !std::strcmp(m.vessel.callsign,"TESTA1"));
  assert(m.vessel.has_name && !std::strcmp(m.vessel.name,"ORCSDR TEST A1"));
  assert(m.vessel.has_ship_type && m.vessel.ship_type==70 && m.vessel.has_dimensions);
  assert(m.vessel.to_bow_m==90 && m.vessel.to_stern_m==30 && m.vessel.to_port_m==10 && m.vessel.to_starboard_m==10);
  assert(m.vessel.has_destination && !std::strcmp(m.vessel.destination,"TEST PORT"));
  assert(parse(p,416,&m)==ParseStatus::invalid_length);   // a truncated Type 5 is refused, not half-read
  uint8_t type5[kMaxMessageBytes];std::memcpy(type5,p,sizeof(type5));
  // Type 24 part A (name) and part B (ship type, call sign, dimensions).
  uint8_t part_a[kMaxMessageBytes]{},part_b[kMaxMessageBytes]{};
  n=from_hex("6017D7842C3D20D31128141535200B1000000000",part_a,sizeof(part_a));
  assert(n==20 && parse(part_a,160,&m)==ParseStatus::ok && m.coverage==Coverage::static_b && m.vessel.part==0);
  assert(m.source_id==100000011 && m.vessel.has_name && !std::strcmp(m.vessel.name,"ORCSDR TEST B1") && !m.vessel.has_callsign);
  n=from_hex("6017D7842D253D20D4153514153502C400281C2084",part_b,sizeof(part_b));
  assert(n==21 && parse(part_b,168,&m)==ParseStatus::ok && m.coverage==Coverage::static_b && m.vessel.part==1);
  assert(m.vessel.has_ship_type && m.vessel.ship_type==37 && m.vessel.has_callsign && !std::strcmp(m.vessel.callsign,"TESTB1"));
  assert(m.vessel.has_dimensions && m.vessel.to_bow_m==5 && m.vessel.to_stern_m==7 && m.vessel.to_port_m==2 && m.vessel.to_starboard_m==2);
  assert(!m.vessel.has_name);
  // Malformed: part B cut to the part A length, and an undefined part number, decode no static field.
  assert(parse(part_b,160,&m)==ParseStatus::ok && m.coverage==Coverage::header_only && !m.vessel.has_callsign);
  uint8_t bad[kMaxMessageBytes];std::memcpy(bad,part_a,sizeof(bad));bad[4]|=0x02;   // part number 2
  assert(parse(bad,160,&m)==ParseStatus::ok && m.coverage==Coverage::header_only && !m.vessel.has_name);
  // An all-'@' name is "not available", never an empty name presented as a value.
  uint8_t blank[kMaxMessageBytes]{};blank[0]=24<<2;blank[1]=0x17;blank[2]=0xD7;blank[3]=0x84;blank[4]=0x2C;
  assert(parse(blank,160,&m)==ParseStatus::ok && m.coverage==Coverage::static_b && !m.vessel.has_name);

  // Station state: static data creates and completes a station; it never invents a position or changes its class.
  static StationStore store;store.clear();
  assert(parse(type5,424,&m)==ParseStatus::ok && store.ingest(m,1000));
  const Station* s=store.find(100000001);
  assert(s && s->kind==StationKind::class_a && !s->has_position && !std::strcmp(s->name,"ORCSDR TEST A1"));
  assert(!std::strcmp(s->callsign,"TESTA1") && !std::strcmp(s->destination,"TEST PORT") && s->has_ship_type && s->ship_type==70);
  from_hex("0417D784040007BDC9039C9A728203842D00000000",p,sizeof(p));
  assert(parse(p,168,&m)==ParseStatus::ok && store.ingest(m,2000));
  s=store.find(100000001);
  assert(s && s->has_position && s->has_nav_status && s->nav_status==0 && !std::strcmp(s->name,"ORCSDR TEST A1"));
  assert(parse(part_a,160,&m)==ParseStatus::ok && store.ingest(m,3000));
  assert(parse(part_b,168,&m)==ParseStatus::ok && store.ingest(m,3400));
  s=store.find(100000011);
  assert(s && s->kind==StationKind::class_b && !std::strcmp(s->name,"ORCSDR TEST B1") && !std::strcmp(s->callsign,"TESTB1"));
  assert(s->ship_type==37 && !s->has_position && s->destination[0]==0);
  from_hex("10003D09047EAB530013724F8A069ED301C0000000",p,sizeof(p));
  assert(parse(p,168,&m)==ParseStatus::ok && store.ingest(m,4000));
  s=store.find(1000001);assert(s && s->kind==StationKind::base_station && s->has_position && store.size()==3);
  store.clear();
}
}
int main() {
  coverage_and_bounds();class_a_and_class_b();aid21_extended_name();long_range27();type28_field_map();station_store();independent_fixtures();
  std::puts("AIS portable protocol and bounded station tests passed (synthetic fixtures only)");
}
