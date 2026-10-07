#include "control/link.hpp"
#include "ui.hpp"
#include <M5Dial.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
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
#ifdef ORCDIAL_DOC_CAPTURE
static bool documentation_active = false;
#endif
static orc::View view = orc::View::home;
static int selected_index = 0;
static int device_selection=0;
static bool forget_confirmation=false;
// Dial Settings.
static orc::DialSettings dial_settings;
static orc::Page current_page = orc::Page::link;
static int menu_index = 0, page_row = 0;
static bool reset_armed = false, display_asleep = false, press_swallow = false, touch_swallow = false;
static uint32_t reset_armed_ms = 0, last_activity_ms = 0;
static void apply_display() { M5Dial.Display.setBrightness(display_asleep ? 0 : dial_settings.brightness); }
static void note_activity() {
  last_activity_ms = millis();
  if (display_asleep) { display_asleep = false; apply_display(); }
}
static void open_settings_menu() { forget_confirmation = false; reset_armed = false; view = orc::View::settings_menu; }
static void open_page(orc::Page page) {
  if (page == orc::Page::pairing) { device_selection = 0; forget_confirmation = false; view = orc::View::connection; return; }
  current_page = page; page_row = 0; reset_armed = false; view = orc::View::page;
}
// Direct-tuning keypad, opened by a long press on the Home frequency.
static char keypad_entry[12];
static bool keypad_out_of_range = false;
static bool hold_armed = false;
static uint32_t hold_start_ms = 0;
static void keypad_open() { keypad_entry[0] = 0; keypad_out_of_range = false; view = orc::View::keypad; }
static void keypad_key(char key) {
  const size_t length = strlen(keypad_entry);
  keypad_out_of_range = false;
  if (key == '\b') { if (length) keypad_entry[length - 1] = 0; }
  else if (key == '.') { if (length && length < 8 && !strchr(keypad_entry, '.')) { keypad_entry[length] = '.'; keypad_entry[length + 1] = 0; } }
  else if (key >= '0' && key <= '9') { if (length < 8) { keypad_entry[length] = key; keypad_entry[length + 1] = 0; } }
}
// Tune: MHz across the whole range; the Tab5 validates again and moves to the band the frequency belongs to.
static void keypad_tune(bool online) {
  char* end = nullptr;
  const double mhz = strtod(keypad_entry, &end);
  if (end == keypad_entry || *end || mhz < 24.0 || mhz > 1766.0) { keypad_out_of_range = true; return; }
  if (online) (void)radio_link.command(orc::Type::tune_absolute, int32_t(mhz * 1000000.0 + 0.5));
  view = orc::View::home;
}
static void device_activate() {
  const auto status=radio_link.security_status();
  pending_delta=0;
  if(forget_confirmation) {
    if(device_selection==1)radio_link.forget_and_pair();
    forget_confirmation=false;device_selection=0;return;
  }
  if(status.state==orc::secure::State::verify) {
    if(device_selection==0)radio_link.confirm_pairing(status.code);else radio_link.cancel_pairing();
  }else if(device_selection==0) {
    if(radio_link.pairing())radio_link.cancel_pairing();
    else if(status.state==orc::secure::State::connected)radio_link.disconnect();
    else if(status.trusted)radio_link.connect();else radio_link.start_pairing();
  }else if(device_selection==1) {
    if(status.trusted){forget_confirmation=true;device_selection=0;}else open_settings_menu();
  }else if(status.trusted)radio_link.boot_connect(!status.boot_connect);
}
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
  if(target==orc::devices_entry){open_settings_menu();return;}
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
    // Keep detents that arrive while a command is pending; the flush in loop() sends the sum once it is acknowledged.
    if (!radio_link.command_action(action) && action.kind == K::tune)
      pending_delta += action.value;
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
static void settings_adjust(int delta) {
  auto& s = dial_settings;
  if (current_page == orc::Page::display) {
    if (page_row == 0) { s.brightness = uint8_t(constrain(int(s.brightness) + delta * 15, 15, 255)); apply_display(); }
    else s.sleep = uint8_t((s.sleep + delta + 4) % 4);
  } else if (current_page == orc::Page::knob) {
    if (page_row == 0) s.accel = uint8_t(constrain(int(s.accel) + delta, 0, 2));
    else if (page_row == 1) s.invert = !s.invert;
    else s.click = !s.click;
  } else return;
  orc::save_settings(s);
}
static void settings_press(bool online) {
  const auto status = radio_link.security_status();
  switch (current_page) {
    case orc::Page::link:
      if (online) radio_link.disconnect();
      else if (status.trusted) radio_link.connect();
      else open_page(orc::Page::pairing);
      break;
    case orc::Page::display: case orc::Page::knob:
      page_row = (page_row + 1) % orc::page_rows(current_page);
      break;
    case orc::Page::reset:
      if (!reset_armed) { reset_armed = true; reset_armed_ms = millis(); break; }
      reset_armed = false;
      radio_link.forget();
      orc::erase_settings();
      dial_settings = orc::DialSettings();
      apply_display();
      view = orc::View::home;
      break;
    default: break;
  }
}
static int knob_boost(uint32_t elapsed) {
  switch (dial_settings.accel) {
    case 0: return 1;
    case 2: return elapsed < 40 ? 10 : elapsed < 90 ? 4 : elapsed < 160 ? 2 : 1;
    default: return elapsed < 40 ? 5 : elapsed < 90 ? 2 : 1;
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
    #ifdef ORCDIAL_DOC_CAPTURE
    else if (!std::strcmp(command, "ORCDIAL_DOC_BOOT")) {
      documentation_active = true;
      orc::capture_splash();
    } else if (!std::strncmp(command,"ORCDIAL_DOC_DEVICE ",19)) {
      unsigned state,selection;char trailing;
      if(std::sscanf(command+19,"%u %u %c",&state,&selection,&trailing)!=2 || state>8 || selection>2)Serial.println("ORCDIAL_DOC_ERROR invalid_device_state");
      else {
        documentation_active=true;orc::secure::Status status{};
        status.identity[0]=0xab;status.identity[1]=0xcd;status.identity[14]=0x12;status.identity[15]=0x34;
        std::memcpy(status.peer_identity,status.identity,16);status.trusted=state>=2 && state!=7;status.boot_connect=true;
        status.state=state==1?orc::secure::State::searching:state==3?orc::secure::State::connected:state==4?orc::secure::State::paused:state==5?orc::secure::State::verify:state==8?orc::secure::State::failed:orc::secure::State::offline;
        status.code=123456;status.upgrade=state==7;status.failure=state==8?orc::secure::Failure::timeout:orc::secure::Failure::none;
        orc::devices_state(status,selection,state==6);
        orc::draw(local,focus,false,state==1,true,orc::View::connection,local.dashboard,false,false,0,tune_style);orc::capture_frame(false);
      }
    } else if (!std::strncmp(command, "ORCDIAL_DOC_SHOW ", 17)) {
      unsigned v, id, style, f, content, pairing, demo, pending;
      char trailing;
      if (std::sscanf(command + 17, "%u %u %u %u %u %u %u %u %c",
                      &v, &id, &style, &f, &content, &pairing, &demo, &pending, &trailing) != 8 ||
          v > 3 || !orc::valid_dashboard(id) || style >= unsigned(orc::TuneStyle::count) ||
          f > unsigned(orc::Focus::volume) || content > 4 || pairing > 1 || demo > 1 || pending > 1) {
        Serial.println("ORCDIAL_DOC_ERROR invalid_arguments");
      } else {
        documentation_active = true;
        local = orc::RadioState{};
        local.dashboard = orc::Dashboard(id);
        preview_band(local.dashboard);
        local.view = content;
        orc::draw(local, orc::Focus(f), false, pairing, demo, orc::View(v), local.dashboard,
                  pending, false, 0, orc::TuneStyle(style));
        orc::capture_frame(false);
      }
    } else if (!std::strcmp(command, "ORCDIAL_DOC_EXIT")) {
      documentation_active = false;
      view = orc::View::home;
      Serial.println("ORCDIAL_DOC_EXIT_DONE");
    }
    #endif
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
    } else if (!std::strcmp(command,"ORCDIAL_PAIR CANCEL")) {radio_link.cancel_pairing();
    } else if (!std::strncmp(command,"ORCDIAL_PAIR CONFIRM ",21)) {
      char* end=nullptr;const long code=std::strtol(command+21,&end,10);
      if(end!=command+21 && !*end && code>=0 && code<1000000)radio_link.confirm_pairing(uint32_t(code));
    } else if (!std::strcmp(command,"ORCDIAL_CONNECT")) {radio_link.connect();
    } else if (!std::strcmp(command,"ORCDIAL_FORGET")) {radio_link.forget();
    } else if (!std::strcmp(command, "ORCDIAL_DISCONNECT")) {
      radio_link.disconnect(); pending_delta = 0; view = orc::View::connection;
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
    } else if (!std::strcmp(command, "ORCDIAL_RF")) {
      radio_link.rf_report();
    } else if (!std::strcmp(command, "ORCDIAL_RF RESET")) {
      radio_link.rf_reset(); Serial.println("ORCDIAL_RF_RESET_OK");
    } else if (!std::strncmp(command, "ORCDIAL_RF TRACE ", 17) && (command[17]=='0'||command[17]=='1'||command[17]=='2') && !command[18]) {
      radio_link.rf_trace(uint8_t(command[17]-'0')); Serial.println("ORCDIAL_RF_TRACE_OK");
    } else if (!std::strcmp(command, "ORCDIAL_STATUS")) {
      const auto security=radio_link.security_status();
      Serial.printf("ORCDIAL_SECURITY trust=%d connection=%s boot=%d failure=%s code=%06lu protocol=4\n",security.trusted,orc::secure::state_name(security.state),security.boot_connect,orc::secure::failure_name(security.failure),(unsigned long)security.code);
      Serial.printf("ORCDIAL_LINK paused=%d\n", radio_link.paused() ? 1 : 0);
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
      Serial.println("ORCDIAL_STATUS | ORCDIAL_PAIR START/CANCEL/CONFIRM <code> | ORCDIAL_CONNECT | ORCDIAL_DISCONNECT | ORCDIAL_FORGET | ORCDIAL_FOCUS NEXT | ORCDIAL_ROTATE <-20..20, nonzero>");
    } else if (length) Serial.println("ORCDIAL_COMMAND_ERROR unknown");
    length = 0;
    overflow = false;
  }
}
void setup() {
  Serial.begin(115200); Serial.println("ORCDIAL_BOOT");
  auto cfg = M5.config(); M5Dial.begin(cfg, true, false);
  orc::load_settings(dial_settings);
  apply_display();
  orc::splash();
  if (!ORCDIAL_DEMO && !radio_link.begin()) Serial.println("ESPNOW_INIT_FAILED");
  last_detent = M5Dial.Encoder.read() / 4;
}
void loop() {
  M5Dial.update();
  poll_serial_commands();
#ifdef ORCDIAL_DOC_CAPTURE
  if (documentation_active) { delay(5); return; }
#endif
  if (!ORCDIAL_DEMO) radio_link.poll();
  const bool online = !ORCDIAL_DEMO && radio_link.connected();
  const orc::RadioState& state = online ? radio_link.state() : local;
  if (online) local = state;
  static orc::Dashboard last_dashboard = orc::Dashboard::home;
  if (online && state.dashboard != last_dashboard) {
    pending_delta = 0;
    focus = orc::Focus::vfo;
    tune_style = style_for(state.dashboard);
    if (view != orc::View::carousel && view != orc::View::connection && view != orc::View::settings_menu &&
        view != orc::View::page)
      view = state.dashboard == orc::Dashboard::home ? orc::View::home : orc::View::dashboard;
    last_dashboard = state.dashboard;
  }
  if (online && view == orc::View::dashboard && state.dashboard == orc::Dashboard::home && !radio_link.pending())
    view = orc::View::home;
  // Online with Home on screen, Home is a full-range VFO; otherwise it stays the launcher.
  const bool home_tune = online && state.dashboard == orc::Dashboard::home;
  if (online && pending_delta && radio_link.command_action({orc::ActionKind::tune, pending_delta})) pending_delta = 0;
  if (reset_armed && millis() - reset_armed_ms > 4000) reset_armed = false;
  if (!display_asleep && dial_settings.sleep && millis() - last_activity_ms > orc::sleep_ms[dial_settings.sleep % 4]) {
    display_asleep = true;
    apply_display();
  }
  const int32_t detent = M5Dial.Encoder.read() / 4;
  int32_t movement = detent - last_detent;
  if (movement) {
    last_detent = detent;
    note_activity();
    if (dial_settings.invert) movement = -movement;
    if (dial_settings.click) M5Dial.Speaker.tone(3200, 6);
    if (view == orc::View::home && !home_tune) { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
    if (view == orc::View::carousel) {
      selected_index = (selected_index + movement % orc::carousel_count + orc::carousel_count) % orc::carousel_count;
    } else if(view==orc::View::connection) {
      const auto status=radio_link.security_status();
      const int count=forget_confirmation||status.state==orc::secure::State::verify||!status.trusted?2:3;
      device_selection=(device_selection+movement%count+count)%count;
    } else if (view == orc::View::settings_menu) {
      menu_index = (menu_index + movement % orc::page_count + orc::page_count) % orc::page_count;
    } else if (view == orc::View::page) {
      settings_adjust(movement > 0 ? 1 : -1);
    } else if (view == orc::View::dashboard || (view == orc::View::home && home_tune)) {
      reel_position += movement;
      const uint32_t now = millis(), elapsed = now - last_turn_ms;
      last_turn_ms = now;
      const int boost = knob_boost(elapsed);
      act(orc::rotate(state.dashboard, state.view, focus, movement, boost, state.step_hz), online);
    }
  }
  if (M5Dial.BtnA.wasPressed()) { press_swallow = display_asleep; press_ms = millis(); note_activity(); }
  if (M5Dial.BtnA.wasReleased()) {
    const uint32_t duration = millis() - press_ms;
    if (press_swallow) press_swallow = false;
    else if (duration >= 4000 && !ORCDIAL_DEMO &&
        (view == orc::View::home || state.dashboard == orc::Dashboard::settings)) {
      open_settings_menu();
    }
    else if (duration >= 900) {
      if(forget_confirmation){forget_confirmation=false;device_selection=0;}
      else if (view == orc::View::connection || view == orc::View::page) open_settings_menu();
      else if (view == orc::View::settings_menu) view = orc::View::home;
      else if (view == orc::View::home && home_tune) { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
      else view=orc::View::home;
    }
    else if (view == orc::View::keypad) keypad_tune(online);
    else if (view == orc::View::home) {
      if (home_tune) focus = orc::next_focus(state.dashboard, focus);
      else { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
    }
    else if (view == orc::View::carousel) select_dashboard(state, online);
    else if (view == orc::View::connection) device_activate();
    else if (view == orc::View::settings_menu) open_page(orc::Page(menu_index));
    else if (view == orc::View::page) settings_press(online);
    else if (orc::tunable(state.dashboard) || orc::channel_dashboard(state.dashboard))
      focus = orc::next_focus(state.dashboard, focus);
    else act(orc::press(state.dashboard, state.view), online);
  }
  const bool touching = M5Dial.Touch.getCount() > 0;
  if (touching && !touch_down) { touch_swallow = display_asleep; note_activity(); }
  if (touching && !touch_down && !touch_swallow) {
    const auto t = M5Dial.Touch.getDetail();
    if (view == orc::View::keypad) {
      const char key = orc::keypad_hit(t.x, t.y);
      if (key == 'C') view = orc::View::home;
      else if (key == 'T') keypad_tune(online);
      else if (key) keypad_key(key);
    } else if (view == orc::View::connection) {
      if(t.y>=130 && t.y<212){device_selection=(t.y-132)/27;if(device_selection>2)device_selection=2;device_activate();}
      else if(t.y>=212){forget_confirmation=false;open_settings_menu();}
    } else if (view == orc::View::settings_menu) {
      if (t.y < 88) menu_index = (menu_index + orc::page_count - 1) % orc::page_count;
      else if (t.y > 152) menu_index = (menu_index + 1) % orc::page_count;
      else open_page(orc::Page(menu_index));
    } else if (view == orc::View::page) {
      if (t.y >= 214) open_settings_menu();
      else if (current_page == orc::Page::display || current_page == orc::Page::knob) {
        const int rows = orc::page_rows(current_page);
        const int first = current_page == orc::Page::display ? 88 : 78, pitch = current_page == orc::Page::display ? 40 : 34;
        const int row = (t.y - (first - pitch / 2)) / pitch;
        if (row >= 0 && row < rows) { page_row = row; settings_adjust(1); }
      } else settings_press(online);
    } else if (view == orc::View::home) {
      if (home_tune && t.y >= 40 && t.y < 70) change(orc::Type::set_mode, state.mode >= 5 ? 1 : state.mode + 1);
      else if (home_tune && t.y >= 88 && t.y < 142) { hold_armed = true; hold_start_ms = millis(); }
      else if (home_tune && t.y <= 175) focus = orc::next_focus(state.dashboard, focus);
      else { view = orc::View::carousel; selected_index = orc::carousel_index(state.dashboard); }
    } else if (view == orc::View::carousel) {
      if (t.y > 177) view = orc::View::home;
      else if (t.x < 70) selected_index = (selected_index + orc::carousel_count - 1) % orc::carousel_count;
      else if (t.x > 170) selected_index = (selected_index + 1) % orc::carousel_count;
      else select_dashboard(state, online);
    } else if (t.y < 76) {
      if (orc::tunable(state.dashboard)) change(orc::Type::set_mode, state.mode >= 3 ? 1 : state.mode + 1);
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
  if (!touching) touch_swallow = false;
  touch_down = touching;
  // A long press on the Home frequency opens the keypad; a short tap there still moves focus.
  if (hold_armed) {
    if (view != orc::View::home) hold_armed = false;
    else if (!touching) { hold_armed = false; focus = orc::next_focus(state.dashboard, focus); }
    else if (millis() - hold_start_ms >= 700) { hold_armed = false; keypad_open(); }
  }
  if (millis() - last_draw_ms > 75) {
    orc::devices_state(radio_link.security_status(),device_selection,forget_confirmation);
    orc::keypad_state(keypad_entry, keypad_out_of_range);
    if (view == orc::View::settings_menu || view == orc::View::page) {
      orc::SettingsView v;
      v.menu = menu_index; v.page = current_page; v.row = page_row; v.reset_armed = reset_armed;
      v.linked = online; v.settings = dial_settings; v.link = radio_link.diagnostics(); v.security = radio_link.security_status();
      uint8_t mac[6]{};
      if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK)
        snprintf(v.mac, sizeof v.mac, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      orc::settings_state(v);
    }
    orc::draw(state, focus, online, !ORCDIAL_DEMO && radio_link.pairing(),
              ORCDIAL_DEMO, view, orc::carousel[selected_index], pending_delta || radio_link.pending(),
              millis() - last_turn_ms < 700, reel_position, tune_style);
    last_draw_ms = millis();
  }
  if (millis() - last_status_ms >= 5000) {
    const auto security = radio_link.security_status();
    Serial.printf("ORCDIAL_STATUS link=%s freq=%lu sec=%s failure=%s locked=%d ch=%u\n", online ? "LINKED" : "OFFLINE",
                  (unsigned long)state.frequency_hz, orc::secure::state_name(security.state),
                  orc::secure::failure_name(security.failure), security.channel_locked ? 1 : 0,
                  unsigned(radio_link.channel()));
    last_status_ms = millis();
  }
  delay(5);
}
