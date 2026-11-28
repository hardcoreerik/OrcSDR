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
#include "dv_dmr_control.hpp"
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
  // Tier III C_ALOHA / C_BCAST / MBC parser (real Eugene 152.1575 bytes).
  {
    orcsdr::dv_radio::Event ev{};
    ev.protocol = orcsdr::digital_voice_ui::Protocol::dmr;
    ev.evidence = orcsdr::digital_voice_ui::Evidence::integrity_valid;
    ev.colour_code_valid = true; ev.colour_code = 13;
    ev.slot_valid = true; ev.slot = 1;
    ev.has_data_type = true; ev.data_type = 3; ev.info_valid = true;
    const uint8_t aloha[12] = {0x99, 0x00, 0x21, 0x00, 0xD6, 0xB0, 0x37, 0x00, 0x00, 0x00, 0x49, 0xE2};
    std::memcpy(ev.info, aloha, 12);
    const DmrControlInfo ci = parse_dmr_event(ev);
    assert(ci.role_flags == kRoleControl && ci.has_sys && ci.model == 2 && ci.sys_code == 0x2C0D && ci.net_id == 12 && ci.site_id == 14);
    // C_BCAST with byte 1 = 0x08 (not Hytera XPT): still parses SYScode.
    const uint8_t bcast[12] = {0x28, 0x08, 0x35, 0x81, 0x96, 0xB0, 0x37, 0xFF, 0xFC, 0x80, 0x0A, 0xEE};
    std::memcpy(ev.info, bcast, 12);
    const DmrControlInfo cb = parse_dmr_event(ev);
    assert(cb.role_flags == kRoleControl && cb.has_sys && cb.sys_code == 0x2C0D && cb.is_adj_site);
    // MBC Header (dt=4) + Continuation (dt=5) harvests Adjacent Site frequency (160.485 MHz).
    MbcTracker mbc;
    ev.data_type = 4; std::memcpy(ev.info, bcast, 12);
    (void)mbc.on_event(ev);
    const uint8_t mbcc[12] = {0xA8, 0x00, 0x00, 0xD1, 0xC9, 0xF8, 0xAC, 0x14, 0x0F, 0x28, 0xD5, 0x98};
    ev.data_type = 5; std::memcpy(ev.info, mbcc, 12);
    const DmrControlInfo cm = mbc.on_event(ev);
    assert(cm.role_flags == kRoleControl && cm.is_adj_site && cm.has_harvest_hz && cm.harvest_hz == 160485000u);
  }
  // DVDB1 backward compatibility.
  {
    static Discovery v1;
    const char* legacy = "DVDB1\nS 1 3 152157500 1 13 5 2 100 200 5\nT 1 6011885 1 110 120 3\n";
    assert(v1.parse(legacy, std::strlen(legacy)));
    assert(v1.system_count() == 1 && v1.system(0).frequency_hz == 152157500u && (v1.system(0).role_flags & kRoleVoice) != 0);
  }
  // Near-frequency dedup (+-6.25 kHz), Net ID grouping, Ghost Guard, and voice-first ranking.
  {
    static Discovery grp;
    Observation ctrl = obs(152157500u, 13, 0, 0, 100, 4);
    ctrl.role_flags = kRoleControl; ctrl.has_sys = true; ctrl.sys_model = 2; ctrl.sys_code = 0x2C0D; ctrl.net_id = 12; ctrl.site_id = 14; ctrl.level_milli = 185;
    assert(grp.record(ctrl));
    // Near-frequency hit within 6.25 kHz merges into the same system (and adopts stronger center).
    Observation near = ctrl; near.frequency_hz = 152160000u; near.level_milli = 220;
    assert(grp.record(near));
    assert(grp.system_count() == 1 && grp.system(0).frequency_hz == 152160000u);
    // Second site in the same Net ID groups under the same network_uid.
    Observation site2 = obs(152685000u, 13, 0, 0, 110, 4);
    site2.role_flags = kRoleControl; site2.has_sys = true; site2.sys_model = 2; site2.sys_code = 0x2C0E; site2.net_id = 12; site2.site_id = 15; site2.level_milli = 160;
    assert(grp.record(site2));
    assert(grp.system_count() == 2 && grp.network_member_count(grp.system(0).network_uid) == 2);
    // UHF channel sharing the same Net ID (12) stays in its own band group and does NOT merge into VHF Net 12.
    Observation uhf = obs(453950000u, 13, 0, 0, 115, 4);
    uhf.role_flags = kRoleControl; uhf.has_sys = true; uhf.sys_model = 2; uhf.sys_code = 0x2C0F; uhf.net_id = 12; uhf.site_id = 16; uhf.level_milli = 150;
    assert(grp.record(uhf));
    assert(grp.network_member_count(grp.system(0).network_uid) == 2);
    // Weak ghost/image (>6.25 kHz away, exact same sys_code, <1/3 level) is flagged suspected_ghost.
    Observation ghost = ctrl; ghost.frequency_hz = 153307500u; ghost.level_milli = 20;
    assert(grp.record(ghost));
    assert(grp.system_count() == 4);
    assert(grp.system(grp.find_system(3, 153307500u, 13, AreaKind::colour_code)).suspected_ghost);
    // Voice channel ranks ahead of Control-only and Ghost in nth_ranked_system().
    Observation vch = obs(152900000u, 13, 4106011u, 6011885u, 120, 5);
    vch.role_flags = kRoleVoice;
    assert(grp.record(vch));
    assert(grp.system(grp.nth_ranked_system(0)).frequency_hz == 152900000u);
    assert(grp.system(grp.nth_ranked_system(4)).suspected_ghost);
  }
  // Retroactive consolidate() on legacy DVDB1: merges +-6.25 kHz, suppresses 1-hit glitched CC & 2-hit noise, and collapses shared (TG, Unit) sites.
  {
    static Discovery leg;
    const char* raw =
        "DVDB1\n"
        "S 1 3 152157500 1 13 5 2 100 200 50\n"
        "S 2 3 152160000 1 13 5 2 201 210 5\n"
        "S 3 3 152157500 1 3 4 0 215 215 1\n"
        "S 4 3 462725000 1 4 4 0 220 225 2\n"
        "S 5 3 152685000 1 13 5 2 230 240 12\n"
        "T 1 6011885 1 110 120 3\n"
        "T 5 6011885 1 230 240 2\n"
        "U 1 4106011 1 6011885 110 120 3\n"
        "U 5 4106011 1 6011885 230 240 2\n";
    assert(leg.parse(raw, std::strlen(raw)));
    // S1+S2 merged (+-6.25 kHz), S3 (CC3 HEARD 1) flagged ghost, S4 (HEARD 2 no TG) unconfirmed, S1+S5 grouped by shared (TG, Unit).
    assert(leg.system_count() == 4);
    assert(leg.grouped_system_count() == 1);
    GroupSummary gs{};
    assert(leg.nth_ranked_group(0, gs));
    assert(gs.site_count == 2 && gs.total_heard == 67 && gs.site_listed == 2);
  }
  std::printf("PASS dv_discovery_tests (worst-case text %zu of %zu bytes)\n", wn, kMaxSerializedBytes);
  return 0;
}
