// Real OrcSDR source-model and dashboard implementation, compiled with a
// recording M5GFX stub. No USB, C6, receiver or aircraft data is simulated in production.
#include "M5Unified.h"
#include "drone_watch_model.hpp"
#include "drone_watch_dashboard.hpp"
#include "screen_controller.hpp"
#include "dashboard_audio_control.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using namespace orcsdr;
using namespace orcsdr::drone_watch;

MockM5 M5;
uint32_t fake_ms = 1000;
namespace orcsdr::audio_header {
void draw_brand(const char*) {}
void draw_battery(int32_t) {}
void draw_home_button() {}
void draw_mute_button(bool) {}
void draw_visualizer_button(bool) {}
void draw_settings_button() {}
}
static void text_is(const Message& m, const char* expected) {
  assert(std::strcmp(m.headline, expected) == 0);
}
int main() {
  SourceSnapshot s{};
  text_is(describe(s, 1000), "SOURCE UNAVAILABLE");
  assert(!describe(s, 1000).monitoring && !valid_packet_age(s, 1000));
  s.state = SourceState::listening;
  text_is(describe(s, 1000), "SOURCE UNAVAILABLE"); // silicon != verified acquisition
  s.capture_qualified = true;
  text_is(describe(s, 1000), "NO RID OBSERVED");
  assert(describe(s, 1000).monitoring);
  s.last_packet_valid = true;
  s.last_packet_ms = 200;
  s.packets_received = 1;
  text_is(describe(s, 1000), "REMOTE ID RECEIVED");
  assert(describe(s, 1000).observed);
  text_is(describe(s, 61000), "NO RID OBSERVED");
  s.state = SourceState::paused;
  text_is(describe(s, 1000), "SCANNER PAUSED");
  s.state = SourceState::ready;
  text_is(describe(s, 1000), "READY - NOT SCANNING");
  s.state = SourceState::degraded;
  text_is(describe(s, 1000), "SOURCE DEGRADED");
  s.state = SourceState::error;
  text_is(describe(s, 1000), "SOURCE ERROR");
  s.state = SourceState::unsupported;
  text_is(describe(s, 1000), "METHOD UNSUPPORTED");
  s.test_data = true;
  text_is(describe(s, 1000), "TEST DATA");
  s.test_data = false;
  s.capture_qualified = false;
  text_is(describe(s, 1000), "SOURCE UNAVAILABLE");

  install({[]() -> int32_t { return 75; }, []() -> bool { return true; }});
  open();
  assert(active() && screens::owns(screens::Id::drone_watch));
  assert(M5.Display.full_clears == 1);
  assert(M5.Display.saw("SOURCE UNAVAILABLE"));
  assert(M5.Display.saw("NOT IMPLEMENTED"));
  assert(touch(10, 10) == Action::none);
  const auto before_tick = M5.Display.rect_draws;
  for (int i = 0; i < 100; ++i) tick(1000 + i * 100);
  assert(M5.Display.full_clears == 1);
  assert(M5.Display.rect_draws == before_tick); // idle does no drawing
  touch(900, 545); // SOURCES
  assert(M5.Display.saw("SOURCE DETAILS"));
  touch(1100, 545); // HELP
  assert(M5.Display.saw("HOW IT WORKS"));
  assert(M5.Display.full_clears == 1); // partial content repaint only
  // A qualified test-only adapter may report a validated packet, but idle
  // display ticks must still repaint no static chrome or whole frame.
  SourceSnapshot qualified{};
  qualified.state = SourceState::listening;
  qualified.capture_qualified = true;
  qualified.last_packet_valid = true;
  qualified.last_packet_ms = 9000;
  qualified.packets_received = 1;
  publish_source(qualified);
  assert(M5.Display.saw("REMOTE ID RECEIVED"));
  assert(M5.Display.saw("VALID RID MESSAGES: 1"));
  tick(70000); // expired packet; not current reception
  assert(M5.Display.saw("NO RID OBSERVED"));
  assert(M5.Display.full_clears == 1);
  SourceSnapshot mock{};
  mock.test_data = true;
  publish_source(mock);
  assert(M5.Display.saw("TEST DATA"));
  assert(M5.Display.full_clears == 1);

  // Screen-controller ownership: inactive screens cannot paint on worker/UI ticks.
  screens::begin_transition(screens::Id::home, 12000);
  screens::finish_transition();
  const auto previous = M5.Display.rect_draws;
  tick(12500);
  publish_source(mock);
  assert(M5.Display.rect_draws == previous);
  close();
  assert(!active());

  // The 720x720 layout is one renderer with compact hit regions.
  M5.Display = MockDisplay{};
  M5.Display.display_width = 720;
  open();
  assert(M5.Display.full_clears == 1);
  assert(M5.Display.saw("SOURCE UNAVAILABLE"));
  assert(touch(720 - 100, 40) == Action::close); // compact HOME
  assert(touch(10, 10) == Action::none);
  touch(70, 600);
  assert(M5.Display.saw("SOURCE DETAILS"));
  assert(M5.Display.full_clears == 1);
  close();
  std::cout << "Drone Watch W01 host tests PASS (model, source truth, 1280/720, dirty redraw, owner)\n";
  return 0;
}
