#include "../apps/orcsdr-tab5/ui/ft8_model.hpp"

#include <cassert>
#include <cmath>
#include <cstring>

int main() {
  using namespace orcsdr::ft8;

  assert(self_check());

  const SlotClock a = slot_clock(14999);
  assert(a.elapsed_ms == 14999);
  assert(a.remaining_ms == 1);
  const SlotClock b = slot_clock(15000);
  assert(b.elapsed_ms == 0);
  assert(b.remaining_ms == 15000);

  // Multi-mode: FT8 stays the default and 15 s; FT4's 7.5 s slots land on half seconds.
  assert(DigitalMode::ft8 == Decode{}.mode && Decode{}.flags == decode_flag_none);
  assert(slot_ms(DigitalMode::ft8) == 15000 && slot_ms(DigitalMode::ft4) == 7500);
  assert(slot_ms(DigitalMode::js8_normal) == 15000 && slot_ms(DigitalMode::js8_fast) == 10000);
  assert(slot_ms(DigitalMode::js8_40) == 6000 && slot_ms(DigitalMode::js8_slow) == 30000);
  assert(slot_ms(DigitalMode::js8_60_experimental) == 4000 && mode_experimental(DigitalMode::js8_60_experimental));
  assert(!mode_experimental(DigitalMode::js8_slow) && mode_is_js8(DigitalMode::js8_40) && !mode_is_js8(DigitalMode::ft4));
  assert(valid_mode(6) && !valid_mode(7));
  for (size_t i = 0; i < kDigitalModeCount; ++i)
    assert(std::strlen(mode_name(static_cast<DigitalMode>(i))) <= 10);
  assert(slot_clock(14999).period_ms == 15000);   // the default mode is unchanged
  {
    const SlotClock early = slot_clock(7499, DigitalMode::ft4);
    const SlotClock edge = slot_clock(7500, DigitalMode::ft4);
    const SlotClock late = slot_clock(22499, DigitalMode::ft4);
    assert(early.elapsed_ms == 7499 && early.remaining_ms == 1 && early.slot_index == 0);
    assert(edge.elapsed_ms == 0 && edge.remaining_ms == 7500 && edge.slot_index == 1);
    assert(late.slot_index == 2 && late.remaining_ms == 1);
    // A 4 s JS8 60 slot and a 30 s JS8 Slow slot divide the same clock differently.
    assert(slot_clock(8000, DigitalMode::js8_60_experimental).slot_index == 2);
    assert(slot_clock(29999, DigitalMode::js8_slow).remaining_ms == 1);
  }
  {
    Decode flagged{};
    flagged.flags = decode_flag_assisted | decode_flag_multi_frame;
    flagged.kind = DecodeKind::cq;   // the kind keeps its own meaning; flags are independent provenance
    assert((flagged.flags & decode_flag_assisted) && !(flagged.flags & decode_flag_hash_resolved));
    assert(flagged.kind == DecodeKind::cq && flagged.mode == DigitalMode::ft8);
  }

  assert(maidenhead_valid("FN42"));
  assert(maidenhead_valid("fn42ab"));
  assert(!maidenhead_valid("ZZ99"));
  assert(!maidenhead_valid("FN4"));

  GeoPoint p{};
  assert(maidenhead_center("PM95", &p));
  assert(p.latitude > 35.0f && p.latitude < 36.0f);
  assert(p.longitude > 138.9f && p.longitude < 140.1f);

  char call[16]{};
  char grid[9]{};
  assert(parse_cq_fields("CQ DX DL1ABC JO62", call, sizeof(call), grid, sizeof(grid)));
  assert(std::strcmp(call, "DL1ABC") == 0);
  assert(std::strcmp(grid, "JO62") == 0);

  DecodeStore store;
  Decode d{};
  std::strcpy(d.callsign, "K1ABC");
  std::strcpy(d.grid, "FN42");
  d.kind = DecodeKind::cq;
  store.append(d);
  store.append(d);
  std::strcpy(d.callsign, "JA1XYZ");
  std::strcpy(d.grid, "PM95");
  store.append(d);
  assert(store.size() == 3);
  assert(store.unique_calls() == 2);
  assert(store.cq_count() == 3);
  assert(store.grid_count() == 3);
  assert(store.newest() && std::strcmp(store.newest()->callsign, "JA1XYZ") == 0);
  assert(store.newest()->flags & decode_flag_new_station);        // first JA1XYZ
  assert(!(store.newest(1)->flags & decode_flag_new_station));    // the repeated K1ABC is not new
  assert(store.newest(2)->flags & decode_flag_new_station);       // the first K1ABC was

  return 0;
}
