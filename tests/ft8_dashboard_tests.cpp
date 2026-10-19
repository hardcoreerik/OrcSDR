// Exercise the real dashboard actions and repaint decisions with a recording display.
#include "ft8_dashboard.hpp"
#include "ft8_map_source.hpp"
#include "map_preferences.hpp"
#include <cstring>
#include <M5Unified.h>
#include <cassert>
#include <cstdio>
namespace orcsdr::audio_header { void draw_brand(const char*) {} }
namespace {
orcsdr::ft8::map_source::State test_frame=orcsdr::ft8::map_source::State::pending;
uint32_t test_map_revision=0;
bool test_labels=true, test_save_fail=false, last_requested_labels=true;
int map_requests=0;
}
namespace orcsdr::maps::preferences {
void load() {}
bool labels(Consumer c) { return c==Consumer::ft8 ? test_labels : true; }
bool set_labels(Consumer c,bool on) {
  if(c!=Consumer::ft8 || test_save_fail)return false;
  test_labels=on;return true;
}
}
namespace orcsdr::ft8::map_source {
Result draw(lgfx::LovyanGFX&,bool labels) {
  ++map_requests;last_requested_labels=labels;
  Result r{};r.state=test_frame;
  std::strcpy(r.credit,"Made with Natural Earth");
  r.low_memory=false;
  return r;
}
uint32_t revision(){return test_map_revision;}
}
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
  // The new FT8 map uses the pinned world worker, not the retired outline;
  // pending frames never undergo pixel reprojection.
  lgfx::paints.clear();lgfx::sampled_pixels=0;lgfx::drawn_pixels=0;
  test_frame=map_source::State::pending;
  select_tab(Tab::map);
  assert(painted("MAIDENHEAD GRID") && painted("LABELS ON") && painted("LOADING WORLD"));
  assert(last_requested_labels && lgfx::sampled_pixels==0);
  ++test_map_revision;test_frame=map_source::State::ready;
  lgfx::paints.clear();update(s);
  assert(painted("ORCMAPS WORLD") && lgfx::sampled_pixels>0 && lgfx::drawn_pixels>0);
  test_save_fail=true;
  (void)handle_touch(770,130);
  assert(test_labels && last_requested_labels); // failed persistence never flips UI or renderer
  test_save_fail=false;
  (void)handle_touch(770,130);
  assert(!test_labels && !last_requested_labels && painted("LABELS OFF"));
  assert(lgfx::sampled_pixels>0); // completed off-label generation uses same geometry
  test_frame=map_source::State::pending;
  lgfx::paints.clear();(void)handle_touch(770,130);
  assert(test_labels && painted("APPLYING LABELS")); // old labelled bitmap is declared pending
  test_frame=map_source::State::unavailable;
  ++test_map_revision;
  lgfx::paints.clear();update(s);
  assert(painted("LAST MAP FRAME")); // no repeated blank after an unavailable worker
  leave();
  // Fresh entry with simulated PSRAM allocation failure: a truthful grid only.
  lgfx::sprite_fail=true;
  enter(s);
  lgfx::paints.clear();select_tab(Tab::map);
  assert(painted("LOW MEMORY - GRID ONLY") && painted("LABELS ON"));
  lgfx::sprite_fail=false;
  leave();
  std::puts("ft8_dashboard_tests: PASS");
}
