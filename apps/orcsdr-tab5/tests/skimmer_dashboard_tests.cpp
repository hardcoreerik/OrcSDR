// Host regression: actual M5GFX dashboard logic with a recording display.
#include "skimmer_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include <M5Unified.h>
#include <cassert>
#include <cstdio>
#include <string>
FakeM5 M5;
namespace orcsdr::audio_header {
void draw_brand(const char*) {}
void draw_home_button() {}
void draw_mute_button(bool) {}
void draw_visualizer_button(bool) {}
void draw_settings_button() {}
}
static bool visible(const char* needle) {
 for (const auto& s : M5.Display.text) if (s.find(needle) != std::string::npos) return true;
 return false;
}
int main() {
 using namespace orcsdr::skimmer;
 assert(!active());
 enter();
 assert(active() && source() == Source::demo && tab() == Tab::live);
 assert(visible("UI PREVIEW - NO LIVE RF") && visible("DEMO-A"));
 const int base_clears = M5.Display.clears;
 update(); update(); update();
 assert(M5.Display.clears == base_clears);
 assert(handle_touch(1014, 141));
 assert(filter() == Mode::rtty);
 assert(visible("DEMO-D"));
 assert(M5.Display.clears == base_clears);
 assert(handle_touch(490, 678));
 assert(tab() == Tab::stations);
 assert(M5.Display.clears == base_clears + 1);
 assert(handle_touch(1107, 681)); assert(tab() == Tab::setup);
 assert(handle_touch(116, 394)); assert(source() == Source::unavailable);
 assert(visible("DECODER NOT CONNECTED"));
 assert(handle_touch(170, 681)); assert(tab() == Tab::live);
 Snapshot live{};
 live.source = Source::live;
 live.count = 1;
 live.signals[0].frequency_hz = 14031000;
 live.signals[0].mode = Mode::rtty;
 live.signals[0].decoded = true;
 live.signals[0].confidence = 93;
 std::snprintf(live.signals[0].callsign, sizeof(live.signals[0].callsign), "TEST-1");
 std::snprintf(live.signals[0].message, sizeof(live.signals[0].message), "HOST TEST DATA");
 live.signal_revision = 10;
 live.sample_revision = 4;
 set_snapshot(live);
 const int dynamic_base = M5.Display.clears;
 update();
 assert(visible("HOST TEST DATA") && M5.Display.clears == dynamic_base);
 live.signals[0].confidence = 97;
 live.signal_revision = 11;
 set_snapshot(live);
 update(); update();
 assert(M5.Display.clears == dynamic_base);
 leave(); assert(!active());
 enter(); assert(active());
 assert(M5.Display.clears == dynamic_base + 1);
 leave();
}
