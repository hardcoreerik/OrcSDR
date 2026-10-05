#include "control/link.hpp"
#include "ui.hpp"
#include <M5Dial.h>
#include <cstdlib>
#include <cstring>
#include <esp_wifi.h>

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
static void act(orc::Action action, bool online) {
  using K = orc::ActionKind;
  if (action.kind == K::none) return;
  if (online) {
    if (!radio_link.command_action(action) && action.kind == K::tune &&
        !radio_link.pending()) pending_delta += action.value;
    return;
  }
  switch (action.kind) {
    case K::tune: local.frequency_hz = orc::clamp_frequency(int64_t(local.frequency_hz) + action.value); break;
    case K::step:
      if (local.dashboard == orc::Dashboard::fm) {
        static constexpr uint32_t fm_steps[] = {50000, 100000, 200000, 500000, 1000000};
        int i = 0;
        while (i < 5 && fm_steps[i] != local.step_hz) ++i;
        local.step_hz = fm_steps[(i + action.value % 5 + 5) % 5];
      } else {
        local.step_hz = orc::steps[constrain(orc::step_index(local.step_hz) + action.value, 0, orc::step_count-1)];
      }
      break;
    case K::gain: local.gain_tenth_db = constrain(int(local.gain_tenth_db) + action.value*10, 0, 500); break;
    case K::squelch: local.squelch = constrain(int(local.squelch) + action.value, 0, 100); break;
    case K::volume: local.volume = constrain(int(local.volume) + action.value*2, 0, 100); break;
    case K::view: local.view = (local.view + (action.value > 0 ? 1 : 4)) % 5; break;
    default: break; // No synthetic channels, aircraft, nodes, messages or APs.
  }
}
static void poll_serial_commands() {
  static char command[64];
  static unsigned length = 0;
  static bool overflow = false;
  while (Serial.available()) {
    const char c = char(Serial.read());
    if (c == '\r') continue;
    if (c != '\n') {
      if (length < sizeof(command) - 1) command[length++] = c;
      else overflow = true;
      continue;
    }
    command[length] = '\0';
    if (overflow) Serial.println("ORCDIAL_COMMAND_ERROR too_long");
    else if (!std::strcmp(command, "ORCDIAL_RESTART")) {
      Serial.println("ORCDIAL_RESTARTING"); Serial.flush(); ESP.restart();
    } else if (!std::strcmp(command, "ORCDIAL_PAIR START")) {
      if (ORCDIAL_DEMO) Serial.println("ORCDIAL_PAIR_ERROR demo_mode");
      else {
        pending_delta = 0;
        view = orc::View::connection;
        radio_link.start_pairing();
        Serial.println("ORCDIAL_PAIR_SEARCH_STARTED");
      }
    } else if (!std::strncmp(command, "ORCDIAL_DASHBOARD ", 18)) {
      char* end = nullptr;
      const long id = std::strtol(command + 18, &end, 10);
      if (end == command + 18 || *end || id < 0 || id > 255 || !orc::valid_dashboard(uint8_t(id)))
        Serial.println("ORCDIAL_CONTROL_ERROR invalid_dashboard");
      else if (!radio_link.connected()) Serial.println("ORCDIAL_CONTROL_ERROR offline");
      else if (!radio_link.command(orc::Type::set_dashboard, int32_t(id))) Serial.println("ORCDIAL_CONTROL_ERROR busy");
      else {
        focus = orc::Focus::vfo;
        tune_style = style_for(orc::Dashboard(id));
        view = id == 0 ? orc::View::home : orc::View::dashboard;
        Serial.println("ORCDIAL_DASHBOARD_QUEUED");
      }
    } else if (!std::strcmp(command, "ORCDIAL_FOCUS NEXT")) {
      if (!radio_link.connected()) Serial.println("ORCDIAL_CONTROL_ERROR offline");
      else {
        focus = orc::next_focus(radio_link.state().dashboard, focus);
        Serial.printf("ORCDIAL_FOCUS value=%u\n", unsigned(focus));
      }
    } else if (!std::strncmp(command, "ORCDIAL_ROTATE ", 15)) {
      char* end = nullptr;
      const long detents = std::strtol(command + 15, &end, 10);
      if (end == command + 15 || *end || !detents || detents < -20 || detents > 20)
        Serial.println("ORCDIAL_CONTROL_ERROR invalid_delta");
      else if (!radio_link.connected()) Serial.println("ORCDIAL_CONTROL_ERROR offline");
      else {
        const auto& state = radio_link.state();
        const auto action = orc::rotate(state.dashboard, state.view, focus, int(detents), 1, state.step_hz);
        if (action.kind == orc::ActionKind::none) Serial.println("ORCDIAL_CONTROL_ERROR unsupported");
        else if (!radio_link.command_action(action)) Serial.println("ORCDIAL_CONTROL_ERROR busy");
        else Serial.printf("ORCDIAL_CONTROL_QUEUED action=%u value=%ld seq=%lu\n", unsigned(action.kind), long(action.value), (unsigned long)radio_link.pending_sequence());
      }
    } else if (!std::strcmp(command, "ORCDIAL_STATUS")) {
      uint8_t channel = 0;
      wifi_second_chan_t secondary;
      const bool channel_valid = esp_wifi_get_channel(&channel, &secondary) == ESP_OK;
      const auto& state = radio_link.connected() ? radio_link.state() : local;
      Serial.printf("ORCDIAL_STATUS link=%s pairing=%d channel=%u channel_valid=%d freq=%lu dashboard=%u focus=%u step=%lu volume=%u ack=%lu pending=%d transport=%s\n",
                    radio_link.connected() ? "LINKED" : "OFFLINE", radio_link.pairing() ? 1 : 0,
                    unsigned(channel), channel_valid ? 1 : 0,
                    (unsigned long)state.frequency_hz, unsigned(state.dashboard), unsigned(focus),
                    (unsigned long)state.step_hz, unsigned(state.volume),
                    (unsigned long)radio_link.last_ack(), radio_link.pending() ? 1 : 0,
                    "ESPNOW");
    } else if (!std::strcmp(command, "ORCDIAL_HELP")) {
      Serial.println("ORCDIAL_STATUS | ORCDIAL_PAIR START | ORCDIAL_FOCUS NEXT | ORCDIAL_ROTATE <-20..20, nonzero>");
    } else if (length) Serial.println("ORCDIAL_COMMAND_ERROR unknown");
    length = 0;
    overflow = false;
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
  poll_serial_commands();
  if (!ORCDIAL_DEMO) radio_link.poll();
  const bool online = !ORCDIAL_DEMO && radio_link.connected();
  const orc::RadioState& state = online ? radio_link.state() : local;
  if (online) local = state;
  static orc::Dashboard last_dashboard = orc::Dashboard::home;
  if (online && state.dashboard != last_dashboard) {
    pending_delta = 0;
    tune_style = style_for(state.dashboard);
    if (view != orc::View::carousel && view != orc::View::connection)
      view = state.dashboard == orc::Dashboard::home ? orc::View::home : orc::View::dashboard;
    last_dashboard = state.dashboard;
  }
  if (online && view == orc::View::dashboard && state.dashboard == orc::Dashboard::home && !radio_link.pending())
    view = orc::View::home;
  if (online && pending_delta && radio_link.command_action({orc::ActionKind::tune, pending_delta})) pending_delta = 0;
  const int32_t detent = M5Dial.Encoder.read() / 4;
  int32_t movement = detent - last_detent;
  if (movement) {
    last_detent = detent;
    if (view == orc::View::home) { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
    if (view == orc::View::carousel) {
      selected_index = (selected_index + movement % orc::carousel_count + orc::carousel_count) % orc::carousel_count;
    } else if (view == orc::View::dashboard) {
      reel_position += movement;
      const uint32_t now = millis(), elapsed = now - last_turn_ms;
      last_turn_ms = now;
      const int boost = elapsed < 40 ? 5 : elapsed < 90 ? 2 : 1;
      act(orc::rotate(state.dashboard, state.view, focus, movement, boost, state.step_hz), online);
    }
  }
  if (M5Dial.BtnA.wasPressed()) press_ms = millis();
  if (M5Dial.BtnA.wasReleased()) {
    const uint32_t duration = millis() - press_ms;
    if (duration >= 4000 && !ORCDIAL_DEMO &&
        (view == orc::View::home || state.dashboard == orc::Dashboard::settings)) {
      view = orc::View::connection; radio_link.start_pairing();
    }
    else if (duration >= 900) view = orc::View::home;
    else if (view == orc::View::home) { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
    else if (view == orc::View::carousel) select_dashboard(state, online);
    else if (view == orc::View::connection) view = orc::View::home;
    else if (orc::tunable(state.dashboard) || orc::channel_dashboard(state.dashboard))
      focus = orc::next_focus(state.dashboard, focus);
    else act(orc::press(state.dashboard, state.view), online);
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
      else if (orc::tunable(state.dashboard)) change(orc::Type::set_mode, state.mode >= 3 ? 1 : state.mode + 1);
      else act({orc::ActionKind::view, 1}, online);
    } else if (state.dashboard == orc::Dashboard::fm && t.y >= 150 && t.y < 183) {
      focus = t.x < 86 ? orc::Focus::vfo : t.x < 154 ? orc::Focus::step : orc::Focus::volume;
    } else if (t.x > 170 && t.y < 170 && orc::tunable(state.dashboard) && state.dashboard != orc::Dashboard::fm) {
      tune_style = orc::TuneStyle((uint8_t(tune_style) + 1) % uint8_t(orc::TuneStyle::count));
      focus = orc::Focus::vfo;
    } else if (t.y > 170 && t.x < 85) view = orc::View::home;
    else if (t.y > 170 && t.x > 155) { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
    else if (t.y > 170) focus = orc::next_focus(state.dashboard, focus);
    else focus = orc::Focus::vfo;
  }
  touch_down = touching;
  if (millis() - last_draw_ms > 75) {
    orc::draw(state, focus, online, !ORCDIAL_DEMO && radio_link.pairing(),
              ORCDIAL_DEMO, view, orc::carousel[selected_index], pending_delta || radio_link.pending(),
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
