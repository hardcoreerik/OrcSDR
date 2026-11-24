// Host tests for the persistent discovery database (data-structure behaviour; RF content is covered by the real-capture corpus).
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "dv_discovery.hpp"
using namespace orcsdr::dv;
static Observation obs(uint32_t f, uint8_t area, uint32_t src, uint32_t dst, uint32_t utc, uint8_t ev = 5, uint8_t proto = 3) {
  Observation o; o.protocol = proto; o.frequency_hz = f; o.area = area; o.area_kind = AreaKind::colour_code; o.evidence = ev; o.slot = 2;
  o.has_source = src != 0; o.has_destination = dst != 0; o.source_id = src; o.destination_id = dst; o.group_call = true; o.utc = utc; return o;
}
int main() {
  static Discovery db;
  // Rejection: below integrity_valid, or no frequency, changes nothing.
  assert(!db.record(obs(152157500, 13, 1, 2, 10, 3)));
  assert(!db.record(obs(0, 13, 1, 2, 10)));
  assert(db.system_count() == 0 && db.revision() == 0);
  // Integrity-only observation: a system, but never a talkgroup or subscriber.
  assert(db.record(obs(152157500, 13, 4106011, 6011885, 100, 4)));
  assert(db.system_count() == 1 && db.talkgroup_count() == 0 && db.subscriber_count() == 0);
  // Identity-valid: talkgroup and subscribers appear, repeated calls dedupe and count.
  assert(db.record(obs(152157500, 13, 4106011, 6011885, 110)));
  assert(db.record(obs(152157500, 13, 4106019, 6011885, 120)));
  assert(db.record(obs(152157500, 13, 4106011, 6011885, 130)));
  assert(db.system_count() == 1 && db.talkgroup_count() == 1 && db.subscriber_count() == 2);
  assert(db.system(0).count == 4 && db.system(0).best_evidence == 5 && db.system(0).slots_seen == 2 && db.system(0).first_utc == 100 && db.system(0).last_utc == 130);
  assert(db.subscribers_of(db.system(0).uid) == 2 && db.talkgroups_of(db.system(0).uid) == 1);
  // Same frequency, other colour code = another system; same colour code on another frequency too.
  assert(db.record(obs(152157500, 8, 4106019, 6011885, 140)));
  assert(db.record(obs(152685000, 13, 4106019, 6011885, 150)));
  assert(db.system_count() == 3 && db.talkgroup_count() == 3 && db.subscriber_count() == 4);
  // Recency order.
  const uint16_t uid0 = db.system(db.find_system(3, 152157500, 13, AreaKind::colour_code)).uid;
  const size_t newest = db.nth_newest_subscriber(uid0, 0), second = db.nth_newest_subscriber(uid0, 1);
  assert(db.subscriber(newest).id == 4106011 && db.subscriber(second).id == 4106019 && db.nth_newest_subscriber(uid0, 2) == static_cast<size_t>(-1));
  // NXDN-style system: area is a RAN, RAN 0 is valid, no IDs recorded when none validated.
  Observation nx; nx.protocol = 7; nx.frequency_hz = 454225000; nx.area = 0; nx.area_kind = AreaKind::ran; nx.evidence = 4; nx.utc = 0;
  assert(db.record(nx));
  assert(db.system(db.find_system(7, 454225000, 0, AreaKind::ran)).area == 0 && db.system(db.find_system(7, 454225000, 0, AreaKind::ran)).first_utc == 0);
  // Structure revision moves only when something new is added, not when an entry is merely heard again.
  { static Discovery d; assert(d.record(obs(152157500, 13, 1, 2, 5)));
    const uint32_t st0 = d.structure_revision(), rv0 = d.revision();
    assert(d.record(obs(152157500, 13, 1, 2, 6)));
    assert(d.structure_revision() == st0 && d.revision() > rv0);
    assert(d.record(obs(152157500, 13, 3, 2, 7)) && d.structure_revision() == st0 + 1); }
  // Round trip.
  static char text[kMaxSerializedBytes];
  const size_t n = db.serialize(text, sizeof(text));
  assert(n > 6);
  static Discovery db2;
  assert(db2.parse(text, n));
  static char text2[kMaxSerializedBytes];
  assert(db2.serialize(text2, sizeof(text2)) == n && std::strcmp(text, text2) == 0);
  assert(db2.system_count() == db.system_count() && db2.talkgroup_count() == db.talkgroup_count() && db2.subscriber_count() == db.subscriber_count());
  // Reload keeps recency order.
  assert(db2.subscriber(db2.nth_newest_subscriber(db2.system(db2.find_system(3, 152157500, 13, AreaKind::colour_code)).uid, 0)).id == 4106011);
  // Malformed input never changes the database.
  const uint32_t before = db2.revision();
  assert(!db2.parse("", 0) && !db2.parse("DVDB2\nS 1\n", 10));
  assert(!db2.parse(text, n - 3));                                  // truncated final line
  { std::string bad(text, n); bad += "T 99 1 1 0 0 1\n"; assert(!db2.parse(bad.data(), bad.size())); }   // unknown system
  { std::string bad(text, n); bad += "X 1\n"; assert(!db2.parse(bad.data(), bad.size())); }
  { std::string bad = "DVDB1\nS 1 3 152157500 1 13 5 2 0 0 1\nS 1 3 152157500 1 14 5 2 0 0 1\n"; assert(!db2.parse(bad.data(), bad.size())); }   // duplicate uid
  { std::string bad = "DVDB1\nS 1 3 152157500 9 13 5 2 0 0 1\n"; assert(!db2.parse(bad.data(), bad.size())); }   // bad area kind
  assert(db2.revision() == before && db2.serialize(text2, sizeof(text2)) == n);
  // Too-small output buffer reports failure rather than truncating silently.
  char tiny[20];
  assert(db.serialize(tiny, sizeof(tiny)) == 0);
  // Eviction: filling past the caps drops the least recently seen first and keeps tables consistent.
  static Discovery big;
  for (uint32_t i = 0; i < kMaxSystems + 5; ++i) assert(big.record(obs(150000000 + i * 12500, 1, 1000 + i, 2000 + i, 1000 + i)));
  assert(big.system_count() == kMaxSystems);
  assert(big.find_system(3, 150000000, 1, AreaKind::colour_code) == static_cast<size_t>(-1));                       // oldest gone
  assert(big.find_system(3, 150000000 + (kMaxSystems + 4) * 12500, 1, AreaKind::colour_code) != static_cast<size_t>(-1));
  for (size_t i = 0; i < big.talkgroup_count(); ++i) {                                                               // no orphans
    bool ok = false; for (size_t k = 0; k < big.system_count(); ++k) ok |= big.system(k).uid == big.talkgroup(i).system_uid; assert(ok);
  }
  static Discovery full;
  for (uint32_t i = 0; i < 400; ++i) assert(full.record(obs(152157500, 13, 4000000 + i, 6000000 + i, 2000000000u + i)));
  assert(full.talkgroup_count() == kMaxTalkgroups && full.subscriber_count() == kMaxSubscribers);
  // Worst case text size stays inside the documented bound (every table full, maximal numbers).
  static Discovery worst;
  for (uint32_t s = 0; s < kMaxSystems; ++s)
    for (uint32_t i = 0; i < 10; ++i) {
      Observation o = obs(450000000u + s * 12500, 255, 4294967000u + i, 4294967000u - i, 4294967000u);
      o.protocol = 255; assert(worst.record(o));
    }
  static char wbuf[kMaxSerializedBytes + 4096];
  const size_t wn = worst.serialize(wbuf, sizeof(wbuf));
  assert(wn > 0 && wn <= kMaxSerializedBytes);
  std::printf("PASS dv_discovery_tests (worst-case text %zu of %zu bytes)\n", wn, kMaxSerializedBytes);
  return 0;
}
