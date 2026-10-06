#include "../src/controller.hpp"
#include <cassert>

using namespace orc;
static Action turn(Dashboard d, uint8_t view=0) { return rotate(d, view, Focus::vfo, 1, 5, 100000); }
int main() {
  assert(turn(Dashboard::fm).kind == ActionKind::tune && turn(Dashboard::fm).value == 500000);
  assert(turn(Dashboard::am).kind == ActionKind::tune);
  assert(turn(Dashboard::rf_lab).kind == ActionKind::tune);
  assert(turn(Dashboard::weather).kind == ActionKind::channel);
  assert(turn(Dashboard::marine).kind == ActionKind::channel);
  assert(turn(Dashboard::cb).kind == ActionKind::channel);
  assert(turn(Dashboard::adsb).kind == ActionKind::radar_range);
  assert(turn(Dashboard::adsb, 1).kind == ActionKind::aircraft);
  assert(turn(Dashboard::lora).kind == ActionKind::lora_slot);
  assert(turn(Dashboard::lora, 1).kind == ActionKind::node);
  assert(turn(Dashboard::p25).kind == ActionKind::p25_candidate);
  assert(turn(Dashboard::p25, 2).kind == ActionKind::talkgroup);
  assert(turn(Dashboard::pocsag).kind == ActionKind::message);
  assert(turn(Dashboard::wifi_analysis).kind == ActionKind::wifi_ap);
  assert(turn(Dashboard::wifi_analysis, 1).kind == ActionKind::wifi_channel);
  assert(turn(Dashboard::settings).kind == ActionKind::setting);
  const Dashboard non_tuners[] = {Dashboard::adsb, Dashboard::lora, Dashboard::wifi_analysis,
                                  Dashboard::marine, Dashboard::cb, Dashboard::p25, Dashboard::pocsag};
  for (Dashboard d : non_tuners)
    for (uint8_t view=0; view<5; ++view) assert(!frequency_action(turn(d, view).kind));
  assert(rotate(Dashboard::lora, 1, Focus::vfo, 1, 5, 100000).value == 1);
  assert(press(Dashboard::adsb, 1).kind == ActionKind::activate);
  assert(press(Dashboard::lora, 0).kind == ActionKind::view);
  assert(next_focus(Dashboard::fm, Focus::vfo) == Focus::step);
  assert(next_focus(Dashboard::fm, Focus::step) == Focus::volume);
  assert(next_focus(Dashboard::fm, Focus::volume) == Focus::vfo);
  assert(next_focus(Dashboard::marine, Focus::vfo) == Focus::volume);
}
