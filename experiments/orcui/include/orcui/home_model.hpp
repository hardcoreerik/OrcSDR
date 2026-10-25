#pragma once
#include <cstdint>
namespace orcsdr::orcui {
enum class HomeCommand : uint8_t {
  open_menu, close_menu, span_down, span_up, step_down, step_up,
  mode, filter, gain, frequency_entry, tune_down, tune_up, cycle_step,
  cycle_palette, fm_preview
};
struct HomeCapabilities { bool tune=false,step=false,palette=false,fm=false; };
constexpr bool enabled(HomeCommand c,HomeCapabilities caps) {
  switch(c) {
    case HomeCommand::open_menu:case HomeCommand::close_menu:return true;
    case HomeCommand::tune_down:case HomeCommand::tune_up:return caps.tune;
    case HomeCommand::cycle_step:return caps.step;
    case HomeCommand::cycle_palette:return caps.palette;
    case HomeCommand::fm_preview:return caps.fm;
    default:return false;
  }
}
}
