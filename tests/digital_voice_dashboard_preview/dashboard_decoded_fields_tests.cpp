// The decoded fields of a real call (values from the 2026-11-23 Terra capture: SRC 4106011/4106019 -> TG 6011885, CC13, TS2)
// must reach every view: Live, Last Heard, Systems, Scan. Same for an NXDN system (RAN 0 is a valid value) through the same widgets.
#include "digital_voice_dashboard.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace orcsdr::digital_voice_ui;
static Snapshot dmr_call() {
  Snapshot s;
  s.receiver = Receiver::receiving; s.origin = Origin::live; s.protocol = Protocol::dmr; s.evidence = Evidence::identity_valid;
  s.frequency_hz = 152157500; s.has_frequency = true;
  s.source_id = 4106011; s.destination_id = 6011885; s.has_source = s.has_destination = s.group_call = true;
  s.colour_code = 13; s.colour_code_valid = true; s.timeslot = 2; s.timeslot_valid = true;
  s.last_heard_available = s.systems_available = true;
  s.history_count = 2;
  s.history[0].protocol = Protocol::dmr; s.history[0].evidence = Evidence::identity_valid; s.history[0].source_id = 4106011; s.history[0].destination_id = 6011885;
  s.history[0].has_source = s.history[0].has_destination = s.history[0].group_call = true; s.history[0].colour_code = 13; s.history[0].colour_code_valid = true;
  s.history[0].timeslot = 2; s.history[0].timeslot_valid = true; s.history[0].has_time = true; std::strcpy(s.history[0].time_utc, "23:19:58");
  s.history[1].protocol = Protocol::nxdn96; s.history[1].evidence = Evidence::integrity_valid; s.history[1].ran = 0; s.history[1].ran_valid = true;
  s.observed_system_count = 2;
  ObservedSystem& d = s.observed_systems[0];
  d.protocol = Protocol::dmr; d.evidence = Evidence::identity_valid; d.frequency_hz = 152157500; d.has_frequency = true; d.colour_code = 13; d.colour_code_valid = true;
  d.talkgroup_count = 1; d.subscriber_count = 2; d.heard_count = 14; d.top_talkgroups[0] = 6011885; d.top_talkgroup_n = 1;
  d.top_subscribers[0] = 4106011; d.top_subscribers[1] = 4106019; d.top_subscriber_n = 2; std::strcpy(d.last_time, "23:19:58");
  ObservedSystem& n = s.observed_systems[1];
  n.protocol = Protocol::nxdn96; n.evidence = Evidence::integrity_valid; n.frequency_hz = 454225000; n.has_frequency = true; n.ran = 0; n.ran_valid = true; n.heard_count = 3;
  s.db_available = true; s.db_systems = 2; s.db_talkgroups = 1; s.db_subscribers = 2;
  s.scan_hit_count = 1; s.scan_hits[0].protocol = Protocol::dmr; s.scan_hits[0].evidence = Evidence::identity_valid; s.scan_hits[0].frequency_hz = 152157500;
  s.scan_hits[0].has_frequency = true; s.scan_hits[0].colour_code = 13; s.scan_hits[0].colour_code_valid = true;
  return s;
}
static void show(View v, Density mode, const Snapshot& s) {
  lgfx::LovyanGFX display; Dashboard dash; UiState ui; ui.view = v; ui.density = mode;
  host_stub::reset();
  assert(dash.present(display, s, ui, true, false));
}
int main() {
  const Snapshot s = dmr_call();
  for (Density mode : {Density::discovery_first, Density::analysis_first}) {
    {
      Snapshot sl = s; sl.scan.phase = ScanPhase::scanning; sl.scan.channel_index = 1; sl.scan.planned = 4;   // the default LIVE page shows a call only while something is listening
      sl.has_frequency = true; sl.frequency_hz = 152157500;
      show(View::live, mode, sl);
      assert(host_stub::contains("4106011") && host_stub::contains("6011885"));
      if (mode == Density::analysis_first) assert(host_stub::contains("13") && host_stub::contains("TS2") && host_stub::contains("TALKGROUP") && host_stub::contains("GROUP"));
      else assert(host_stub::contains("CC 13") && host_stub::contains("TS 2") && host_stub::contains("152.157500 MHz") && host_stub::contains("TG"));
    }
    show(View::last_heard, mode, s);
    assert(host_stub::contains("23:19:58  DMR  4106011>6011885  CC13 TS2"));
    assert(host_stub::contains("NXDN96") && host_stub::contains("RAN 0"));     // RAN 0 shown as a value, not hidden
    show(View::systems, mode, s);
    assert(host_stub::contains("CC13") && host_stub::contains("RAN 0"));
    assert(host_stub::contains("TG 1  UNITS 2  HEARD 14  23:19:58"));
    assert(host_stub::contains("2 SYS  1 TG  2 UNITS"));
    assert(host_stub::contains("6011885") && host_stub::contains("4106011  4106019"));
    show(View::explore, Density::discovery_first, s);
    assert(host_stub::contains("CC13") && host_stub::contains("152.157500 MHz"));
  }
  // An integrity-only DMR event never shows IDs: it says so.
  Snapshot u = dmr_call(); u.history[0].evidence = Evidence::integrity_valid; u.history_count = 1;
  show(View::last_heard, Density::discovery_first, u);
  assert(host_stub::contains("ID UNKNOWN") && !host_stub::contains("4106011>6011885"));
  std::puts("PASS decoded fields reach Live, Last Heard, Systems and Scan (DMR and NXDN through the same widgets)");
  return 0;
}
