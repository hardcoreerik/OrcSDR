// Exercise the real dashboard actions and repaint decisions with a recording display.
#include "ft8_dashboard.hpp"
#include <M5Unified.h>
#include <cassert>
#include <cstdio>
namespace orcsdr::audio_header { void draw_brand(const char*) {} }
using namespace orcsdr::ft8;
bool painted(const char* s) {
  for (const auto& p : lgfx::paints) if (p.text.find(s) != std::string::npos) return true;
  return false;
}
int main() {
  Snapshot s;
  s.mode = DigitalMode::ft4;
  s.decoder_state = DecoderState::armed;
  s.dial_hz = 14080000;
  s.expert_tuning = true;
  s.gain_available = true;
  s.gain_step_count = 2;
  s.gain_steps_tenth_db[1] = 100;
  enter(s);
  assert(painted("7.5 second"));
  select_tab(Tab::hunter);
  const Action band_pick = handle_touch(360, 375); // 20 m card, row 1 / column 1
  assert(band_pick.kind == ActionKind::tune_band && band_pick.value == 14080000);
  select_tab(Tab::live);
  (void)handle_touch(80, 130); // open Tune
  const Action first = handle_touch(885, 310); // + step
  const Action second = handle_touch(885, 310);
  assert(first.kind == ActionKind::tune_dial && second.value == first.value);
  select_tab(Tab::heard);
  lgfx::paints.clear();
  s.station_known = true;
  s.station_latitude = 44.0f;
  s.station_longitude = -123.0f;
  update(s);
  assert(!lgfx::paints.empty());
  select_tab(Tab::live);
  (void)handle_touch(1100, 130); // gain popup
  lgfx::paints.clear();
  s.gain_auto = false;
  s.gain_tenth_db = 100;
  update(s);
  assert(painted("AUTO") && painted("MANUAL"));
  // The decoder could not start for lack of memory: the screen says so (not "not bound"), shows the numbers and who holds memory,
  // and the FREE MEMORY button works only when something can actually be released.
  leave();   // closes the gain popup left open above
  lgfx::paints.clear();
  s.decoder_state = DecoderState::no_memory;
  s.mem_free = 262144;
  s.mem_largest = 163840;
  s.mem_needed = 4194304;
  static char holders[] = "SSTV pictures 3.8 MB";
  s.mem_holders = holders;
  s.mem_holders_rev = 1;
  enter(s);
  select_tab(Tab::live);
  assert(painted("NOT ENOUGH MEMORY") && !painted("DECODER NOT BOUND"));
  assert(painted("0.2 MB") && painted("4.0 MB") && painted("SSTV pictures 3.8 MB") && painted("FREE MEMORY"));
  const Action free_it = handle_touch(466, 440);   // the FREE MEMORY button
  assert(free_it.kind == ActionKind::free_memory);
  s.mem_holders = nullptr;   // nothing left that a feature can release
  s.mem_holders_rev = 2;
  update(s);
  assert(painted("restart OrcSDR"));
  assert(handle_touch(466, 440).kind != ActionKind::free_memory);   // nothing to release: the button is disabled
  s.decoder_state = DecoderState::unbound;
  lgfx::paints.clear();
  update(s);
  assert(painted("DECODER NOT BOUND") && !painted("NOT ENOUGH MEMORY"));
  leave();
  std::puts("ft8_dashboard_tests: PASS");
}
