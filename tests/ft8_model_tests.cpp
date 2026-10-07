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

  return 0;
}
