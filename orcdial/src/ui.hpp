#pragma once
#include "state.hpp"
#include "control/secure_session.hpp"
#include "dial_settings.hpp"
#include <M5Dial.h>

namespace orc {
enum class TuneStyle : uint8_t { reel, dial, odometer, tape, split, count };
enum class View : uint8_t { home, carousel, dashboard, connection, keypad, settings_menu, page };
void splash();
// Direct-tuning keypad (long press on the Home frequency): the entry text, whether to flag it as out of range, and
// which key a touch landed on ('0'-'9', '.', '\b', 'C' cancel, 'T' tune, 0 for none).
void keypad_state(const char* entry, bool out_of_range);
// Dial Settings (the vertical menu and its pages).
void settings_state(const SettingsView& view);
char keypad_hit(int x, int y);
void devices_state(const secure::Status& status,int selection,bool forget_confirmation);
#ifdef ORCDIAL_DOC_CAPTURE
void capture_frame(bool mark_demo = false);
void capture_splash();
#endif
void draw(const RadioState& state, Focus focus, bool connected, bool pairing,
          bool demo, View view, Dashboard selected, bool pending,
          bool reel_active, int32_t reel_position, TuneStyle style);
} // namespace orc
