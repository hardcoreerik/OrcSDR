#pragma once
#include "state.hpp"
#include <M5Dial.h>

namespace orc {
enum class TuneStyle : uint8_t { reel, dial, odometer, tape, split, count };
enum class View : uint8_t { home, carousel, dashboard, connection };
void splash();
void draw(const RadioState& state, Focus focus, bool connected, bool pairing,
          bool demo, View view, Dashboard selected, bool pending,
          bool reel_active, int32_t reel_position, TuneStyle style);
} // namespace orc
