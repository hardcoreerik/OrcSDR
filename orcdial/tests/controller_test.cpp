#include "../src/controller.hpp"
#include <cassert>
#include <string>

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
  assert(rotate(Dashboard::ft8, 0, Focus::vfo, 1, 5, 100000).kind == ActionKind::ft8_band);
  assert(rotate(Dashboard::ft8, 1, Focus::vfo, 1, 5, 100000).kind == ActionKind::ft8_item);
  assert(rotate(Dashboard::ft8, 2, Focus::vfo, 1, 5, 100000).kind == ActionKind::ft8_item);
  assert(rotate(Dashboard::ft8, 3, Focus::vfo, 1, 5, 100000).kind == ActionKind::ft8_band);
  assert(rotate(Dashboard::ft8, 4, Focus::vfo, 1, 5, 100000).kind == ActionKind::ft8_item);
  assert(rotate(Dashboard::ft8, 5, Focus::vfo, 1, 5, 100000).kind == ActionKind::none);
  const Dashboard non_tuners[] = {Dashboard::adsb, Dashboard::lora, Dashboard::wifi_analysis,
                                  Dashboard::marine, Dashboard::cb, Dashboard::p25, Dashboard::pocsag,
                                  Dashboard::ft8};
  for (Dashboard d : non_tuners)
    for (uint8_t view=0; view<5; ++view) assert(!frequency_action(turn(d, view).kind));
  assert(rotate(Dashboard::lora, 1, Focus::vfo, 1, 5, 100000).value == 1);
  assert(press(Dashboard::adsb, 1).kind == ActionKind::activate);
  assert(press(Dashboard::lora, 0).kind == ActionKind::view);
  assert(press(Dashboard::ft8, 0, ft8_control::kHunterSupported).kind == ActionKind::ft8_hunter);
  assert(press(Dashboard::ft8, 0, ft8_control::kHunterSupported).value ==
         static_cast<int32_t>(ft8_control::HunterCommand::start_fast));
  // Expert-tuning bits ride in the capabilities word without disturbing the hunter bits or the press command.
  {
    uint32_t caps = ft8_control::kHunterSupported | ft8_control::kExpertTuning;
    for (uint8_t step = 0; step < 6; ++step) {
      const uint32_t with = ft8_control::with_step(caps, step);
      assert(ft8_control::step_index(with) == step);
      assert((with & ft8_control::kExpertTuning) != 0 && (with & ft8_control::kHunterSupported) != 0);
      assert((with & (ft8_control::kHunterActive | ft8_control::kHunterComplete)) == 0);
      assert(press(Dashboard::ft8, 0, with).value == static_cast<int32_t>(ft8_control::HunterCommand::start_fast));
    }
    assert(std::string(ft8_control::step_label(2)) == "1 kHz" && std::string(ft8_control::step_label(7)) == "--");
    assert(ft8_control::with_step(ft8_control::kExpertTuning, 5) != ft8_control::with_step(ft8_control::kExpertTuning, 1));
    // Enabled (checkbox) and active (fine tuning now) are separate bits that do not overlap the step field or the hunter bits.
    assert((ft8_control::kExpertEnabled & (ft8_control::kExpertTuning | ft8_control::kStepMask | ft8_control::kHunterSupported |
                                          ft8_control::kHunterActive | ft8_control::kHunterComplete | ft8_control::kClockReady)) == 0);
    assert(static_cast<int>(ActionKind::ft8_fine) == static_cast<int>(ActionKind::ft8_hunter) + 1);   // appended: no existing value moved
  }
  assert(press(Dashboard::ft8, 3, ft8_control::kHunterActive).value ==
         static_cast<int32_t>(ft8_control::HunterCommand::stop));
  assert(press(Dashboard::ft8, 3, ft8_control::kHunterComplete).value ==
         static_cast<int32_t>(ft8_control::HunterCommand::listen_best));
  assert(press(Dashboard::ft8, 1).kind == ActionKind::activate);
  assert(next_focus(Dashboard::fm, Focus::vfo) == Focus::step);
  assert(next_focus(Dashboard::fm, Focus::step) == Focus::volume);
  assert(next_focus(Dashboard::fm, Focus::volume) == Focus::vfo);
  assert(next_focus(Dashboard::marine, Focus::vfo) == Focus::volume);
  // Home tunes by a count of steps (the Tab5 owns the band's step) and cycles vfo, step, volume.
  assert(turn(Dashboard::home).kind == ActionKind::tune && turn(Dashboard::home).value == 5);
  assert(rotate(Dashboard::home, 0, Focus::vfo, -2, 2, 12500).value == -4);
  assert(rotate(Dashboard::home, 0, Focus::step, 1, 5, 12500).kind == ActionKind::step);
  assert(rotate(Dashboard::home, 0, Focus::volume, 1, 5, 12500).kind == ActionKind::volume);
  assert(rotate(Dashboard::home, 0, Focus::vfo, 0, 1, 12500).kind == ActionKind::none);
  assert(next_focus(Dashboard::home, Focus::vfo) == Focus::step);
  assert(next_focus(Dashboard::home, Focus::step) == Focus::span);
  assert(next_focus(Dashboard::home, Focus::span) == Focus::filter);
  assert(next_focus(Dashboard::home, Focus::filter) == Focus::volume);
  assert(next_focus(Dashboard::home, Focus::volume) == Focus::vfo);
  // Appended after `activate`: these values are on the wire and must not move.
  assert(uint8_t(ActionKind::activate) == 20 && uint8_t(ActionKind::span) == 21 && uint8_t(ActionKind::filter) == 22);
  assert(rotate(Dashboard::home, 0, Focus::span, 1, 5, 12500).kind == ActionKind::span);
  assert(rotate(Dashboard::home, 0, Focus::filter, -1, 5, 12500).kind == ActionKind::filter);
  assert(rotate(Dashboard::home, 0, Focus::filter, -1, 5, 12500).value == -1);
}
