#pragma once
#include "orcui/home_model.hpp"
namespace orcsdr::waveshare4b::adaptive {
// Exact legacy queue values; palette/drawer are UI-local and never reach USB.
enum class RadioAction { none,down,up,step };
constexpr RadioAction radio_action(orcui::HomeCommand c) {
  switch(c) {
    case orcui::HomeCommand::tune_down:return RadioAction::down;
    case orcui::HomeCommand::tune_up:return RadioAction::up;
    case orcui::HomeCommand::cycle_step:return RadioAction::step;
    default:return RadioAction::none;
  }
}
}
