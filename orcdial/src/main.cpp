#include "control/link.hpp"
#include "ui.hpp"
#include <M5Dial.h>
#include <cstdlib>

// Set to 1 for a labeled local-only display during bench demonstrations.
#ifndef ORCDIAL_DEMO
#define ORCDIAL_DEMO 0
#endif

static orc::Link radio_link;
static orc::RadioState local;
static orc::Focus focus = orc::Focus::vfo;
static orc::TuneStyle tune_style = orc::TuneStyle::reel;
static int32_t pending_delta = 0;
static int32_t last_detent = 0;
static uint32_t last_turn_ms = 0, press_ms = 0, last_draw_ms = 0;
static uint32_t last_status_ms = 0;
static int32_t reel_position = 0;
static bool touch_down = false;
static orc::View view = orc::View::home;
static int selected_index = 0;
static orc::TuneStyle style_for(orc::Dashboard id) {
  switch (id) {
    case orc::Dashboard::fm: case orc::Dashboard::am: return orc::TuneStyle::dial;
    case orc::Dashboard::weather: case orc::Dashboard::airband: case orc::Dashboard::marine: return orc::TuneStyle::reel;
    case orc::Dashboard::cb: case orc::Dashboard::p25: return orc::TuneStyle::odometer;
    case orc::Dashboard::satellite: case orc::Dashboard::rf_lab: return orc::TuneStyle::tape;
    default: return orc::TuneStyle::split;
  }
}
static void preview_band(orc::Dashboard id) {
  switch (id) {
    case orc::Dashboard::fm: local.frequency_hz=98700000; local.step_hz=100000; local.mode=3; break;
    case orc::Dashboard::am: local.frequency_hz=1010000; local.step_hz=10000; local.mode=2; break;
    case orc::Dashboard::weather: local.frequency_hz=162550000; local.step_hz=25000; local.mode=1; break;
    case orc::Dashboard::airband: local.frequency_hz=121500000; local.step_hz=25000; local.mode=2; break;
    case orc::Dashboard::marine: local.frequency_hz=156800000; local.step_hz=25000; local.mode=1; break;
    case orc::Dashboard::cb: local.frequency_hz=27185000; local.step_hz=5000; local.mode=2; break;
    case orc::Dashboard::adsb: local.frequency_hz=1090000000; local.step_hz=100000; local.mode=1; break;
    case orc::Dashboard::satellite: local.frequency_hz=437100000; local.step_hz=100000; local.mode=1; break;
    case orc::Dashboard::lora: local.frequency_hz=433920000; local.step_hz=1000; local.mode=1; break;
    case orc::Dashboard::p25: local.frequency_hz=154000000; local.step_hz=12500; local.mode=1; break;
    case orc::Dashboard::shortwave: local.frequency_hz=7200000; local.step_hz=1000; local.mode=2; break;
    case orc::Dashboard::pocsag: local.frequency_hz=152480000; local.step_hz=5000; local.mode=1; break;
    default: break;
  }
}

static void select_dashboard(const orc::RadioState& state, bool online) {
  const auto target = orc::carousel[selected_index];
  if (!online) { local.dashboard = target; preview_band(target); }
  else if (target != state.dashboard && !radio_link.command(orc::Type::set_dashboard, uint8_t(target))) return;
  focus = orc::Focus::vfo;
  tune_style = style_for(target);
  view = target == orc::Dashboard::home ? orc::View::home : orc::View::dashboard;
}

static void change(orc::Type type, int32_t value) {
  if (!radio_link.connected() || ORCDIAL_DEMO) {
    switch (type) {
      case orc::Type::tune_relative: local.frequency_hz = orc::clamp_frequency(int64_t(local.frequency_hz) + value); break;
      case orc::Type::set_step: local.step_hz = value; break;
      case orc::Type::set_gain: local.gain_tenth_db = value; break;
      case orc::Type::set_squelch: local.squelch = value; break;
      case orc::Type::set_volume: local.volume = value; break;
      case orc::Type::set_mode: local.mode = value; break;
      default: break;
    }
  } else if (!radio_link.command(type, value) && type == orc::Type::tune_relative) {
    pending_delta += value;
  }
}
void setup() {
  Serial.begin(115200); Serial.println("ORCDIAL_BOOT");
  auto cfg = M5.config(); M5Dial.begin(cfg, true, false);
  orc::splash();
  if (!ORCDIAL_DEMO && !radio_link.begin()) Serial.println("ESPNOW_INIT_FAILED");
  last_detent = M5Dial.Encoder.read() / 4;
}
void loop() {
  M5Dial.update();
  if (!ORCDIAL_DEMO) radio_link.poll();
  const bool online = !ORCDIAL_DEMO && radio_link.connected();
  const orc::RadioState& state = online ? radio_link.state() : local;
  if (online) local = state;
  static orc::Dashboard last_dashboard = orc::Dashboard::home;
  if (online && state.dashboard != last_dashboard) {
    tune_style = style_for(state.dashboard);
    if (view != orc::View::carousel && view != orc::View::connection)
      view = state.dashboard == orc::Dashboard::home ? orc::View::home : orc::View::dashboard;
    last_dashboard = state.dashboard;
  }
  if (online && view == orc::View::dashboard && state.dashboard == orc::Dashboard::home && !radio_link.pending())
    view = orc::View::home;
  if (online && pending_delta && radio_link.command(orc::Type::tune_relative, pending_delta)) pending_delta = 0;
  const int32_t detent = M5Dial.Encoder.read() / 4;
  int32_t movement = detent - last_detent;
  if (movement) {
    last_detent = detent;
    if (view == orc::View::home) { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
    if (view == orc::View::carousel) {
      selected_index = (selected_index + movement % orc::carousel_count + orc::carousel_count) % orc::carousel_count;
    } else if (view == orc::View::dashboard && orc::tunable(state.dashboard)) {
    reel_position += movement;
    const uint32_t now = millis(), elapsed = now - last_turn_ms;
    last_turn_ms = now;
    const int boost = elapsed < 40 ? 5 : elapsed < 90 ? 2 : 1;
    const int32_t adjusted = movement * boost;
    if (focus == orc::Focus::vfo) {
      const int64_t hz = int64_t(adjusted) * state.step_hz;
      change(orc::Type::tune_relative, hz > 1000000000 ? 1000000000 : hz < -1000000000 ? -1000000000 : int32_t(hz));
    }
    if (focus == orc::Focus::step) {
      int index = orc::step_index(state.step_hz) + (adjusted > 0 ? 1 : -1);
      index = constrain(index, 0, orc::step_count - 1);
      change(orc::Type::set_step, orc::steps[index]);
    }
    if (focus == orc::Focus::gain) change(orc::Type::set_gain, constrain(int(state.gain_tenth_db) + adjusted * 10, 0, 500));
    if (focus == orc::Focus::squelch) change(orc::Type::set_squelch, constrain(int(state.squelch) + adjusted, 0, 100));
    if (focus == orc::Focus::volume) change(orc::Type::set_volume, constrain(int(state.volume) + adjusted * 2, 0, 100));
    }
  }
  if (M5Dial.BtnA.wasPressed()) press_ms = millis();
  if (M5Dial.BtnA.wasReleased()) {
    const uint32_t duration = millis() - press_ms;
    if (duration >= 4000 && !ORCDIAL_DEMO) { view = orc::View::connection; radio_link.start_pairing(); }
    else if (duration >= 900) view = orc::View::home;
    else if (view == orc::View::home) { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
    else if (view == orc::View::carousel) select_dashboard(state, online);
    else if (view == orc::View::connection) view = orc::View::home;
    else focus = orc::Focus((uint8_t(focus) + 1) % 5);
  }
  const bool touching = M5Dial.Touch.getCount() > 0;
  if (touching && !touch_down) {
    const auto t = M5Dial.Touch.getDetail();
    if (view == orc::View::connection) {
      if (t.y > 170) view = orc::View::home;
      else if (t.y >= 76 && !ORCDIAL_DEMO) radio_link.start_pairing();
    } else if (view == orc::View::home) {
      if (t.y < 70 && t.x > 150) view = orc::View::connection;
      else { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
    } else if (view == orc::View::carousel) {
      if (t.y > 177) view = orc::View::home;
      else if (t.x < 70) selected_index = (selected_index + orc::carousel_count - 1) % orc::carousel_count;
      else if (t.x > 170) selected_index = (selected_index + 1) % orc::carousel_count;
      else select_dashboard(state, online);
    } else if (t.y < 76) {
      if (t.x > 120 || !online) view = orc::View::connection;
      else change(orc::Type::set_mode, state.mode >= 3 ? 1 : state.mode + 1);
    } else if (t.x > 170 && t.y < 170) {
      tune_style = orc::TuneStyle((uint8_t(tune_style) + 1) % uint8_t(orc::TuneStyle::count));
      focus = orc::Focus::vfo;
    } else if (t.y > 170 && t.x < 85) view = orc::View::home;
    else if (t.y > 170 && t.x > 155) { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
    else if (t.y > 170) focus = orc::Focus::step;
    else focus = orc::Focus::vfo;
  }
  touch_down = touching;
  if (millis() - last_draw_ms > 75) {
    orc::draw(state, focus, online, !ORCDIAL_DEMO && radio_link.pairing(),
              ORCDIAL_DEMO || !online, view, orc::carousel[selected_index], pending_delta || radio_link.pending(),
              millis() - last_turn_ms < 700, reel_position, tune_style);
    last_draw_ms = millis();
  }
  if (millis() - last_status_ms >= 5000) {
    Serial.printf("ORCDIAL_STATUS link=%s freq=%lu\n", online ? "LINKED" : "OFFLINE",
                  (unsigned long)state.frequency_hz);
    last_status_ms = millis();
  }
  delay(5);
}
