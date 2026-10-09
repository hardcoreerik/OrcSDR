#include "ui.hpp"
#include "controller.hpp"
#include "band_names.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orc {
static secure::Status device_status;
static int device_selection=0;
static bool device_forget_confirmation=false;
void devices_state(const secure::Status& status,int selection,bool confirmation) {
  device_status=status;device_selection=selection;device_forget_confirmation=confirmation;
}
extern const uint8_t badge_start[] asm("_binary_assets_orc_badge_104_png_start");
extern const uint8_t badge_end[] asm("_binary_assets_orc_badge_104_png_end");
#define DECLARE_DASHBOARD_IMAGE(name) \
  extern const uint8_t name##_start[] asm("_binary_assets_dashboard_" #name "_png_start"); \
  extern const uint8_t name##_end[] asm("_binary_assets_dashboard_" #name "_png_end");
DECLARE_DASHBOARD_IMAGE(fm)
DECLARE_DASHBOARD_IMAGE(am)
DECLARE_DASHBOARD_IMAGE(weather)
DECLARE_DASHBOARD_IMAGE(airband)
DECLARE_DASHBOARD_IMAGE(marine)
DECLARE_DASHBOARD_IMAGE(cb)
DECLARE_DASHBOARD_IMAGE(adsb)
DECLARE_DASHBOARD_IMAGE(satellite)
DECLARE_DASHBOARD_IMAGE(lora)
DECLARE_DASHBOARD_IMAGE(rf_lab)
DECLARE_DASHBOARD_IMAGE(p25)
DECLARE_DASHBOARD_IMAGE(shortwave)
DECLARE_DASHBOARD_IMAGE(pocsag)
DECLARE_DASHBOARD_IMAGE(wifi_analysis)
DECLARE_DASHBOARD_IMAGE(settings)
#undef DECLARE_DASHBOARD_IMAGE
struct DashboardImage { const uint8_t* start; const uint8_t* end; };
static const DashboardImage dashboard_images[] = {
  {badge_start, badge_end}, {fm_start, fm_end}, {p25_start, p25_end},
  {adsb_start, adsb_end}, {shortwave_start, shortwave_end},
  {weather_start, weather_end}, {cb_start, cb_end}, {lora_start, lora_end},
  {airband_start, airband_end}, {marine_start, marine_end},
  {satellite_start, satellite_end}, {nullptr, nullptr},
  {settings_start, settings_end}, {rf_lab_start, rf_lab_end},
  {wifi_analysis_start, wifi_analysis_end}, {pocsag_start, pocsag_end},
  {am_start, am_end}, {nullptr, nullptr}
};
static_assert(sizeof(dashboard_images) / sizeof(dashboard_images[0]) ==
              static_cast<unsigned>(Dashboard::ft8) + 1);
static constexpr uint32_t ink = 0xe8f5f6, dim = 0x88a4ad, green = 0x70f847;
static constexpr uint32_t bg = 0x050f16, cyan = 0x38d9ff, blue = 0x319bff, red = 0xff4a4a;
static constexpr uint32_t panel = 0x0c202b, trace = 0x163747, trace_bright = 0x286174;
// Text must fit the circle at both its top and bottom, not just the square LCD.
// Optional width also keeps digits inside their individual control boxes.
static void draw_text(lgfx::LGFXBase& d, const char* text, int x, int y, int width = 240) {
  const float requested = d.getTextStyle().size_x;
  float size = requested;
  for (int pass = 0; pass < 4; ++pass) {
    d.setTextSize(size);
    const float edge = std::abs(y - 120) + (d.fontHeight() + 1) / 2.0f;
    const float half = std::sqrt(std::fmax(0.0f, 112.0f * 112.0f - edge * edge));
    const int available = std::max(1, std::min(width, int(2 * (half - std::abs(x - 120))) - 4));
    const int measured = d.textWidth(text);
    if (measured <= available) break;
    size *= float(available) / measured;
  }
  d.drawString(text, x, y);
  d.setTextSize(requested);
}
static M5Canvas frame(&M5Dial.Display);
static bool frame_attempted = false, frame_ready = false;
static void present() { if (frame_ready) frame.pushSprite(0, 0); }
// The outermost ring shows link state on every screen: green when linked, red when offline.
static uint32_t link_color(bool connected) { return connected ? green : red; }
static uint32_t accent_for(Dashboard id, bool connected) {
  if (!connected) return cyan;
  if (id == Dashboard::am || id == Dashboard::cb || id == Dashboard::satellite) return blue;
  if (id == Dashboard::weather || id == Dashboard::airband || id == Dashboard::marine) return cyan;
  return green;
}
// Decorative motion only: these traces do not represent RF samples or network activity.
static void ambient(lgfx::LGFXBase& d, Dashboard id, uint32_t now) {
  const int phase = (now / 90 + uint8_t(id) * 19) % 330;
  d.drawCircle(120, 120, 96, trace);
  d.drawCircle(120, 120, 103, trace);
  d.drawArc(120, 120, 106, 104, phase, phase + 26, trace_bright);
  const float a = phase * 0.017453293f;
  d.fillCircle(120 + int(100 * cosf(a)), 120 + int(100 * sinf(a)), 2, trace_bright);
}
static void frequency(lgfx::LGFXBase& d, uint32_t hz, int y, int size, uint32_t color) {
  char text[20];
  if (hz % 1000 == 0)
    std::snprintf(text, sizeof text, "%lu.%03lu", (unsigned long)(hz / 1000000),
                  (unsigned long)(hz / 1000 % 1000));
  else
    std::snprintf(text, sizeof text, "%lu.%06lu", (unsigned long)(hz / 1000000),
                  (unsigned long)(hz % 1000000));
  d.setTextColor(color, bg); d.setTextSize(size); draw_text(d, text, 120, y);
}

static void back_button(lgfx::LGFXBase& d);

// Home as a general-purpose VFO. The Tab5 resolves the band, mode and step for the frequency; this only shows them.
static void home_screen(lgfx::LGFXBase& d, const RadioState& state, Focus focus, bool pending, uint32_t now) {
  d.drawCircle(120, 120, 115, link_color(true));
  d.drawCircle(120, 120, 110, 0x1b4e60);
  // Decorative waveform behind the readout; it does not represent the received signal.
  for (int k = 0; k < 2; ++k) {
    int previous = 80;
    for (int x = 24; x <= 216; x += 4) {
      const float envelope = sinf((x - 24) * 3.14159f / 192.0f);
      const float phase = x * (k ? 0.13f : 0.09f) + now * (k ? -0.0021f : 0.0016f);
      const int y = 80 + int(13 * envelope * sinf(phase));
      if (x > 24) d.drawLine(x - 4, previous, x, y, k ? 0x1b5e78 : 0x2b8fb0);
      previous = y;
    }
  }
  static const char* modes[] = {"--", "NFM", "AM", "WFM", "USB", "LSB"};
  d.setTextColor(cyan); d.setTextSize(2); draw_text(d, band_name(state.band), 120, 32);
  d.setTextColor(green); d.setTextSize(2);
  draw_text(d, modes[state.mode < 6 ? state.mode : 0], 120, 54);
  char line[32];
  const uint32_t hz = state.frequency_hz;
  if (hz % 1000 == 0)
    std::snprintf(line, sizeof line, "%lu.%03lu", (unsigned long)(hz / 1000000), (unsigned long)(hz / 1000 % 1000));
  else
    std::snprintf(line, sizeof line, "%lu.%06lu", (unsigned long)(hz / 1000000), (unsigned long)(hz % 1000000));
  d.setTextColor(ink); d.setTextSize(4); draw_text(d, line, 120, 112);
  d.setTextColor(dim); d.setTextSize(1); draw_text(d, "MHz", 120, 140);
  if (focus == Focus::vfo) d.fillRect(52, 131, 136, 2, green);
  if (pending) d.fillCircle(206, 112, 3, cyan);
  // One line shows whichever of step, span or filter is focused (step when the frequency or volume is).
  auto width = [](const char* label, uint32_t hz, char* out, size_t size) {
    if (!hz) std::snprintf(out, size, "%s --", label);
    else if (hz >= 1000000) std::snprintf(out, size, "%s %.1f MHz", label, hz / 1000000.0);
    else if (hz % 1000 == 0) std::snprintf(out, size, "%s %lu kHz", label, (unsigned long)(hz / 1000));
    else std::snprintf(out, size, "%s %.1f kHz", label, hz / 1000.0);
  };
  if (focus == Focus::span) width("SPAN", state.selected > 0 ? uint32_t(state.selected) : 0, line, sizeof line);
  else if (focus == Focus::filter) width("FILTER", state.item_count, line, sizeof line);
  else width("STEP", state.step_hz, line, sizeof line);
  d.setTextColor(focus == Focus::step || focus == Focus::span || focus == Focus::filter ? green : dim);
  d.setTextSize(2); draw_text(d, line, 120, 160);
  const uint32_t bar = focus == Focus::volume ? green : trace_bright;
  d.drawRoundRect(70, 176, 100, 9, 3, bar);
  d.fillRoundRect(72, 178, std::min<int>(state.volume, 100) * 96 / 100, 5, 2, bar);
  back_button(d);
}

// Keypad geometry for the 240 px round screen: every key sits inside the circle.
static constexpr char keypad_keys[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '0', '\b'};
static constexpr int kp_x0 = 42, kp_y0 = 54, kp_w = 50, kp_h = 30, kp_px = 54, kp_py = 34;
static constexpr int kp_cancel_x = 50, kp_tune_x = 122, kp_button_y = 192, kp_button_w = 68, kp_button_h = 22;
static char keypad_entry[12];
static bool keypad_out_of_range = false;
static char keypad_range_text[20] = "";
void keypad_state(const char* entry, bool out_of_range, const char* range_text) {
  std::snprintf(keypad_entry, sizeof keypad_entry, "%s", entry ? entry : "");
  keypad_out_of_range = out_of_range;
  std::snprintf(keypad_range_text, sizeof keypad_range_text, "%s", range_text ? range_text : "");
}
static bool kp_inside(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx - 2 && x < bx + bw + 2 && y >= by - 2 && y < by + bh + 2;
}
char keypad_hit(int x, int y) {
  for (int i = 0; i < 12; ++i)
    if (kp_inside(x, y, kp_x0 + (i % 3) * kp_px, kp_y0 + (i / 3) * kp_py, kp_w, kp_h)) return keypad_keys[i];
  if (kp_inside(x, y, kp_cancel_x, kp_button_y, kp_button_w, kp_button_h)) return 'C';
  if (kp_inside(x, y, kp_tune_x, kp_button_y, kp_button_w, kp_button_h)) return 'T';
  return 0;
}
static void keypad_screen(lgfx::LGFXBase& d, bool connected) {
  d.drawCircle(120, 120, 115, link_color(connected));
  d.fillRoundRect(56, 20, 128, 28, 6, panel);
  d.drawRoundRect(56, 20, 128, 28, 6, keypad_out_of_range ? red : cyan);
  char line[16];
  if (keypad_entry[0]) std::snprintf(line, sizeof line, "%s", keypad_entry);
  else std::snprintf(line, sizeof line, "MHz");
  d.setTextColor(keypad_entry[0] ? ink : dim); d.setTextSize(2); draw_text(d, line, 120, 34);
  for (int i = 0; i < 12; ++i) {
    const int x = kp_x0 + (i % 3) * kp_px, y = kp_y0 + (i / 3) * kp_py;
    d.fillRoundRect(x, y, kp_w, kp_h, 6, panel);
    d.drawRoundRect(x, y, kp_w, kp_h, 6, trace_bright);
    const char label[2] = {keypad_keys[i] == '\b' ? '<' : keypad_keys[i], 0};
    d.setTextColor(ink); d.setTextSize(2); d.drawString(label, x + kp_w / 2, y + kp_h / 2);
  }
  d.fillRoundRect(kp_cancel_x, kp_button_y, kp_button_w, kp_button_h, 6, panel);
  d.drawRoundRect(kp_cancel_x, kp_button_y, kp_button_w, kp_button_h, 6, red);
  d.setTextColor(red); d.setTextSize(1); d.drawString("CANCEL", kp_cancel_x + kp_button_w / 2, kp_button_y + kp_button_h / 2);
  d.fillRoundRect(kp_tune_x, kp_button_y, kp_button_w, kp_button_h, 6, green);
  d.setTextColor(bg); d.setTextSize(1); d.drawString("TUNE", kp_tune_x + kp_button_w / 2, kp_button_y + kp_button_h / 2);
  if (keypad_out_of_range) { d.setTextColor(red); d.setTextSize(1); draw_text(d, keypad_range_text, 120, 222); }
}

// ---- Dial Settings --------------------------------------------------------------------------
static SettingsView settings_view;
void settings_state(const SettingsView& view) { settings_view = view; }

static void settings_row(lgfx::LGFXBase& d, int y, const char* label, const char* value, bool focused) {
  if (focused) d.fillRoundRect(22, y - 15, 196, 30, 8, panel);
  d.setTextSize(1.5f);
  d.setTextDatum(middle_left); d.setTextColor(focused ? cyan : dim); d.drawString(label, 34, y);
  d.setTextDatum(middle_right); d.setTextColor(focused ? ink : dim); d.drawString(value, 206, y);
  d.setTextDatum(middle_center);
}

static void settings_menu_screen(lgfx::LGFXBase& d, bool connected) {
  const auto& v = settings_view;
  d.drawCircle(120, 120, 115, link_color(connected));
  d.setTextColor(dim); d.setTextSize(1); draw_text(d, "DIAL SETTINGS", 120, 24);
  const int previous = (v.menu + page_count - 1) % page_count, next = (v.menu + 1) % page_count;
  d.setTextColor(dim); d.setTextSize(2);
  draw_text(d, page_names[previous], 120, 66); draw_text(d, page_names[next], 120, 176);
  d.fillRoundRect(30, 92, 180, 56, 12, panel); d.drawRoundRect(30, 92, 180, 56, 12, cyan);
  d.setTextColor(ink); d.setTextSize(3); draw_text(d, page_names[v.menu], 120, 120, 164);
  for (int i = 0; i < page_count; ++i) d.fillCircle(214, 78 + i * 14, 3, i == v.menu ? cyan : trace_bright);
}

static void settings_page_screen(lgfx::LGFXBase& d, bool connected) {
  const auto& v = settings_view;
  char a[40], b[40];
  d.drawCircle(120, 120, 115, link_color(connected));
  d.setTextColor(cyan); d.setTextSize(2); draw_text(d, page_names[int(v.page)], 120, 28);
  d.setTextSize(1.5f);
  auto line = [&](int y, uint32_t color, const char* text) { d.setTextColor(color); draw_text(d, text, 120, y); };
  switch (v.page) {
    case Page::link: {
      const auto& l = v.link;
      line(58, v.linked ? green : red, v.linked ? "LINKED" : "OFFLINE");
      std::snprintf(a, sizeof a, "CHANNEL %u  %s", unsigned(l.channel), v.security.channel_locked ? "LOCKED" : "SCANNING");
      line(82, ink, a);
      if (l.last_rx_age_ms == UINT32_MAX) std::snprintf(b, sizeof b, "NEVER");
      else if (l.last_rx_age_ms < 1000) std::snprintf(b, sizeof b, "%lu ms ago", (unsigned long)l.last_rx_age_ms);
      else if (l.last_rx_age_ms < 60000) std::snprintf(b, sizeof b, "%.1f s ago", l.last_rx_age_ms / 1000.0);
      else std::snprintf(b, sizeof b, "over a minute ago");
      std::snprintf(a, sizeof a, "HEARD %s", b);
      line(104, ink, a);
      std::snprintf(a, sizeof a, "SENT %lu  ACKED %lu", (unsigned long)l.tx_accepted, (unsigned long)l.ack_ok);
      line(126, dim, a);
      std::snprintf(a, sizeof a, "FAILED %lu  REFUSED %lu", (unsigned long)l.ack_failed, (unsigned long)l.tx_refused);
      line(146, dim, a);
      std::snprintf(a, sizeof a, "RECEIVED %lu", (unsigned long)l.rx_frames);
      line(166, dim, a);
      line(184, cyan, v.linked ? "PRESS: DISCONNECT" : v.security.trusted ? "PRESS: RECONNECT" : "PRESS: PAIR");
      break;
    }
    case Page::display: {
      std::snprintf(a, sizeof a, "%u%%", unsigned(v.settings.brightness * 100 / 255));
      settings_row(d, 88, "BRIGHTNESS", a, v.row == 0);
      settings_row(d, 128, "SLEEP", sleep_names[v.settings.sleep % 4], v.row == 1);
      line(170, dim, "TURN: CHANGE   PRESS: NEXT");
      break;
    }
    case Page::knob: {
      settings_row(d, 78, "ACCELERATION", accel_names[v.settings.accel % 3], v.row == 0);
      settings_row(d, 112, "REVERSE", v.settings.invert ? "ON" : "OFF", v.row == 1);
      settings_row(d, 146, "CLICK", v.settings.click ? "ON" : "OFF", v.row == 2);
      line(176, dim, "TURN: CHANGE   PRESS: NEXT");
      break;
    }
    case Page::about: {
      line(56, ink, "OrcDial for OrcSDR");
      std::snprintf(a, sizeof a, "BUILD %s", __DATE__);
      line(78, dim, a);
      line(98, dim, "PROTOCOL 4 (ENCRYPTED)");
      std::snprintf(a, sizeof a, "THIS DIAL %s", v.mac[0] ? v.mac : "--");
      line(120, dim, a);
      const uint8_t* peer = v.security.peer_identity;
      if (v.security.trusted) std::snprintf(a, sizeof a, "TAB5 ID %02X%02X-%02X%02X", peer[0], peer[1], peer[14], peer[15]);
      else std::snprintf(a, sizeof a, "NO TAB5 PAIRED");
      line(142, dim, a);
      line(166, cyan, "theorc.dev");
      break;
    }
    case Page::reset: {
      line(70, red, "ERASE THE PAIRING");
      line(90, red, "AND DIAL SETTINGS");
      line(130, dim, "THE TAB5 WILL FORGET THIS DIAL");
      line(168, v.reset_armed ? red : cyan, v.reset_armed ? "PRESS AGAIN TO ERASE" : "PRESS TO ARM");
      break;
    }
    default: break;
  }
  // A real button: tap it, or hold the knob, to go back to the menu.
  d.fillRoundRect(72, 198, 96, 26, 8, panel); d.drawRoundRect(72, 198, 96, 26, 8, cyan);
  d.setTextColor(cyan); d.setTextSize(2); d.drawString("BACK", 120, 211);
}

// The way back to the dashboards carousel, at the bottom of every dashboard screen (tap it, or hold the knob for Home).
static void back_button(lgfx::LGFXBase& d) {
  d.fillRoundRect(80, 203, 80, 22, 8, panel); d.drawRoundRect(80, 203, 80, 22, 8, cyan);
  d.setTextColor(cyan); d.setTextSize(2); d.drawString("BACK", 120, 214);
}

static void fm_screen(lgfx::LGFXBase& d, const RadioState& state, Focus focus,
                      bool connected, bool pairing, bool demo, bool pending,
                      int32_t reel_position) {
  d.drawCircle(120, 120, 115, link_color(connected));
  d.drawCircle(120, 120, 108, trace_bright);
  for (int i = 0; i < 24; ++i) {
    const float a = i * 6.2831853f / 24 - 1.5707963f;
    const int inner = i % 3 ? 100 : 96;
    d.drawLine(120 + int(inner * cosf(a)), 120 + int(inner * sinf(a)),
               120 + int(105 * cosf(a)), 120 + int(105 * sinf(a)),
               i == (reel_position % 24 + 24) % 24 ? green : trace_bright);
  }
  d.setTextColor(cyan, bg); d.setTextSize(2); draw_text(d, "FM", 120, 28);
  d.setTextColor(connected ? green : dim, bg); d.setTextSize(1);
  draw_text(d, pairing ? "PAIRING" : connected ? "LINKED" : demo ? "DEMO" : "OFFLINE", 120, 48);

  char line[32];
  if (state.step_hz >= 1000 && state.frequency_hz % 1000 == 0)
    std::snprintf(line, sizeof line, "%lu.%03lu", (unsigned long)(state.frequency_hz / 1000000),
                  (unsigned long)(state.frequency_hz / 1000 % 1000));
  else
    std::snprintf(line, sizeof line, "%lu.%06lu", (unsigned long)(state.frequency_hz / 1000000),
                  (unsigned long)(state.frequency_hz % 1000000));
  d.setTextColor(ink, bg); d.setTextSize(4);
  if (d.textWidth(line) > 190) d.setTextSize(3);
  draw_text(d, line, 120, 106);
  d.setTextColor(cyan, bg); d.setTextSize(2); draw_text(d, "MHz", 120, 136);

  const char* names[] = {"TUNE", "STEP", "VOL"};
  const Focus choices[] = {Focus::vfo, Focus::step, Focus::volume};
  for (int i = 0; i < 3; ++i) {
    const int x = 31 + i * 61;
    const bool active = focus == choices[i];
    d.fillRoundRect(x, 157, 56, 24, 5, active ? panel : bg);
    d.drawRoundRect(x, 157, 56, 24, 5, active ? green : trace_bright);
    d.setTextColor(active ? green : dim, active ? panel : bg);
    d.setTextSize(1); draw_text(d, names[i], x + 28, 169);
  }
  d.setTextColor(dim, bg); d.setTextSize(1);
  if (state.step_hz % 1000 == 0)
    std::snprintf(line, sizeof line, "STEP %lu kHz", (unsigned long)(state.step_hz / 1000));
  else
    std::snprintf(line, sizeof line, "STEP %lu Hz", (unsigned long)state.step_hz);
  draw_text(d, line, 80, 191);
  std::snprintf(line, sizeof line, "VOL %u", unsigned(state.volume));
  draw_text(d, line, 160, 191);
  if (pending) d.fillCircle(206, 106, 3, cyan);
  back_button(d);
}

// Small, code-drawn illustrations stay sharp on the Dial's 240-pixel round screen.
// None of these marks represents received RF data.
static void artwork(lgfx::LGFXBase& d, Dashboard id, uint32_t c) {
  switch (id) {
    case Dashboard::fm: { // broadcast dial and stereo waves
      for (int x=35; x<=205; x+=10) d.drawFastVLine(x, 78, x%20 ? 5 : 10, c);
      d.drawLine(72, 151, 80, 144, c); d.drawLine(80, 144, 88, 151, c);
      d.drawLine(152, 151, 160, 144, c); d.drawLine(160, 144, 168, 151, c);
      d.drawCircle(120, 151, 5, c); break;
    }
    case Dashboard::am: { // medium-wave ruler
      d.drawFastHLine(35, 82, 170, c);
      for (int x=40; x<=200; x+=10) d.drawFastVLine(x, 82, x%20 ? 5 : 10, c);
      for (int x=55; x<185; x+=22) {
        d.drawLine(x, 152, x+6, 145, c); d.drawLine(x+6, 145, x+12, 152, c);
      }
      break;
    }
    case Dashboard::weather: { // cloud and radio alert
      d.drawCircle(92, 77, 7, c); d.drawCircle(104, 72, 10, c);
      d.drawCircle(116, 77, 7, c); d.drawFastHLine(88, 84, 33, c);
      d.drawLine(164, 177, 164, 165, c); d.drawLine(157, 177, 164, 165, c);
      d.drawLine(164, 165, 171, 177, c); d.drawArc(164, 169, 16, 15, 210, 330, c);
      break;
    }
    case Dashboard::airband: { // aircraft over runway
      d.fillTriangle(120, 69, 114, 86, 126, 86, c);
      d.drawLine(95, 82, 145, 82, c); d.drawLine(112, 89, 128, 89, c);
      d.drawFastHLine(52, 151, 136, c);
      for (int x=63; x<185; x+=24) d.drawFastHLine(x, 155, 12, c);
      break;
    }
    case Dashboard::marine: { // ship and sea
      d.drawLine(91, 77, 149, 77, c); d.drawLine(91, 77, 100, 87, c);
      d.drawLine(149, 77, 140, 87, c); d.drawFastHLine(100, 87, 40, c);
      d.drawFastVLine(120, 67, 10, c); d.drawFastHLine(113, 69, 14, c);
      for (int x=55; x<180; x+=28) {
        d.drawArc(x, 151, 15, 6, 0, 180, c);
      }
      break;
    }
    case Dashboard::cb: { // channel badge and S-meter ticks
      d.drawRoundRect(97, 68, 46, 20, 5, c);
      d.setTextColor(c, bg); d.setTextSize(1); draw_text(d, "CB", 120, 78);
      for (int x=60; x<=180; x+=12) d.drawFastVLine(x, 148, x>144 ? 9 : 5, c);
      break;
    }
    case Dashboard::satellite: { // orbital pass
      d.drawArc(120, 119, 91, 60, 195, 345, c);
      d.fillRect(177, 73, 12, 7, c); d.fillRect(163, 73, 8, 7, c);
      d.drawLine(171, 76, 177, 76, c);
      d.drawArc(120, 177, 64, 30, 190, 350, c); break;
    }
    case Dashboard::rf_lab: { // graticule only; no synthetic spectrum
      for (int x=45; x<=195; x+=25) d.drawFastVLine(x, 76, 75, 0x254d48);
      for (int y=78; y<=153; y+=25) d.drawFastHLine(45, y, 150, 0x254d48);
      d.drawFastVLine(120, 75, 80, c); break;
    }
    case Dashboard::p25: { // trunked-radio motif
      d.drawRoundRect(102, 68, 36, 21, 4, c);
      d.drawFastVLine(120, 62, 7, c);
      for (int x=59; x<=181; x+=18) d.drawFastVLine(x, 150, x%36 ? 5 : 10, c);
      break;
    }
    case Dashboard::shortwave: { // latitude lines and HF wave
      d.drawArc(120, 102, 72, 28, 190, 350, c);
      d.drawArc(120, 102, 54, 20, 190, 350, c);
      for (int x=52; x<180; x+=20) {
        d.drawLine(x, 151, x+5, 145, c); d.drawLine(x+5, 145, x+10, 151, c);
      }
      break;
    }
    case Dashboard::pocsag: { // pager enclosure and message lines
      d.drawRoundRect(97, 66, 46, 25, 4, c);
      d.drawFastHLine(104, 75, 32, c); d.drawFastHLine(104, 81, 24, c);
      d.drawFastHLine(66, 149, 108, c); d.drawFastHLine(78, 154, 84, c);
      break;
    }
    case Dashboard::adsb: { // radar with aircraft, no target count
      for (int r=28; r<=72; r+=22) d.drawCircle(120, 117, r, c);
      d.drawFastHLine(48, 117, 144, c); d.drawFastVLine(120, 45, 144, c);
      d.fillTriangle(120, 99, 115, 122, 125, 122, ink);
      d.drawLine(103, 117, 137, 117, ink); break;
    }
    case Dashboard::lora: { // mesh graph, no synthetic nodes
      constexpr int x[] = {73, 115, 160, 94, 149};
      constexpr int y[] = {96, 74, 102, 147, 148};
      constexpr uint8_t edge[][2] = {{0,1},{1,2},{0,3},{1,3},{2,4},{3,4}};
      for (auto& e: edge) d.drawLine(x[e[0]], y[e[0]], x[e[1]], y[e[1]], c);
      for (int i=0; i<5; ++i) d.fillCircle(x[i], y[i], 5, ink);
      break;
    }
    case Dashboard::wifi_analysis: { // access-point scan icon, no invented networks
      d.drawArc(120, 151, 70, 70, 205, 335, c);
      d.drawArc(120, 151, 50, 50, 210, 330, c);
      d.drawArc(120, 151, 30, 30, 215, 325, c);
      d.fillCircle(120, 151, 5, c); break;
    }
    case Dashboard::settings: { // gear
      d.drawCircle(120, 119, 37, c); d.drawCircle(120, 119, 15, c);
      for (int i=0; i<8; ++i) {
        const float a = i * 0.78539816f;
        d.drawLine(120+int(39*cosf(a)), 119+int(39*sinf(a)),
                   120+int(52*cosf(a)), 119+int(52*sinf(a)), c);
      }
      break;
    }
    case Dashboard::ft8: { // eight FSK tones and three Costas-sync groups; protocol icon only
      for (int i=0; i<8; ++i) {
        const int x=72+i*14;
        const int h=12+((i*11)%5)*5;
        d.drawFastVLine(x, 91-h/2, h, c);
      }
      for (int group=0; group<3; ++group) {
        const int x=68+group*49;
        d.drawRoundRect(x, 145, 36, 15, 3, c);
        d.drawFastVLine(x+9, 149, 7, c);
        d.drawFastVLine(x+18, 147, 11, c);
        d.drawFastVLine(x+27, 151, 5, c);
      }
      break;
    }
    default: break;
  }
}
static void selector_icon(lgfx::LGFXBase& d, Dashboard id, int x, int y,
                          bool small = false) {
  if(id==devices_entry)id=Dashboard::settings;
  const unsigned index = static_cast<unsigned>(id);
  if (index >= sizeof(dashboard_images) / sizeof(dashboard_images[0])) return;
  const auto& image = dashboard_images[index];
  const int size = small ? 30 : 64;
  if (!image.start) {
    if (id != Dashboard::ft8) return;
    const uint32_t c = green;
    const int left = x - size / 2;
    const int top = y - size / 2;
    d.drawRoundRect(left, top, size, size, small ? 5 : 9, c);
    const int base = top + size * 2 / 3;
    for (int i=0; i<8; ++i) {
      const int bx = left + 4 + i * (size - 8) / 8;
      const int h = 4 + (i * 7 % 5) * (small ? 1 : 2);
      d.drawFastVLine(bx, base - h, h, c);
    }
    if (!small) {
      d.setTextColor(ink, bg); d.setTextSize(2);
      draw_text(d, "FT8", x, top + 19, size - 8);
    }
    return;
  }
  const float scale = size / (id == Dashboard::home ? 104.0f : 64.0f);
  d.drawPng(image.start, image.end - image.start, x - size / 2, y - size / 2,
            0, 0, 0, 0, scale);
}
static const char* content_view(Dashboard id, uint8_t view) {
  static const char* adsb[] = {"RADAR", "AIRCRAFT", "TARGET", "STATS", "SETTINGS"};
  static const char* lora[] = {"OVERVIEW", "NODES", "TRAFFIC", "MAP", "RF HEALTH"};
  static const char* p25[] = {"MONITOR", "SPECTRUM", "TALKGROUPS", "PROGRAM", "RF HEALTH"};
  static const char* pocsag[] = {"LIVE", "IDS", "SIGNAL", "ACTIVITY", "ARCHIVE"};
  static const char* wifi[] = {"OVERVIEW", "CHANNELS", "DEVICES", "CSI", "SETTINGS"};
  static const char* ft8[] = {"LIVE", "DECODES", "MAP", "HUNTER", "HEARD", "SETUP"};
  if (id == Dashboard::ft8) return view < 6 ? ft8[view] : "VIEW --";
  if (view >= 5) return "VIEW --";
  switch (id) {
    case Dashboard::adsb: return adsb[view];
    case Dashboard::lora: return lora[view];
    case Dashboard::p25: return p25[view];
    case Dashboard::pocsag: return pocsag[view];
    case Dashboard::wifi_analysis: return wifi[view];
    default: return "";
  }
}

// The OrcSDR wordmark gradient, sampled from the OrcSDR splash art: pale cyan, cyan, mint, green, lime.
static uint32_t wordmark_color(float t) {
  static const uint8_t stops[][3] = {{0x77, 0xF4, 0xF8}, {0x0E, 0xF8, 0xFA}, {0x6D, 0xFB, 0x6B}, {0x5E, 0xFB, 0x04}, {0xAD, 0xFA, 0x0F}};
  constexpr int count = sizeof stops / sizeof stops[0];
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const float position = t * (count - 1);
  const int index = position >= count - 1 ? count - 2 : int(position);
  const float u = position - index;
  auto mix = [&](int channel) { return uint8_t(stops[index][channel] + (stops[index + 1][channel] - stops[index][channel]) * u); };
  return lgfx::color565(mix(0), mix(1), mix(2));
}

// Text coloured letter by letter along the wordmark gradient, centred on `cx`.
static void gradient_text(lgfx::LGFXBase& d, const char* text, int cx, int y, float size) {
  d.setTextSize(size);
  d.setTextDatum(middle_left);
  const int total = d.textWidth(text);
  int x = cx - total / 2;
  for (const char* c = text; *c; ++c) {
    const char glyph[2] = {*c, 0};
    const int width = d.textWidth(glyph);
    d.setTextColor(wordmark_color((x - (cx - total / 2) + width / 2) / float(total ? total : 1)), bg);
    d.drawString(glyph, x, y);
    x += width;
  }
  d.setTextDatum(middle_center);
}

static const char* ft8_band_label(int32_t selected) {
  static const char* labels[] = {
      "160m","80m","60m","40m","30m","20m","17m","15m","12m","10m","6m","2m"};
  const int index = static_cast<int>(selected) - 1;
  return index >= 0 && index < static_cast<int>(sizeof(labels)/sizeof(labels[0]))
             ? labels[index] : "--";
}

static void ft8_screen(lgfx::LGFXBase& d, const RadioState& state,
                       bool connected, bool pairing, bool demo, bool pending) {
  const uint32_t accent = connected ? green : cyan;
  const uint8_t view = state.view;
  const bool live_or_hunter =
      view == static_cast<uint8_t>(ft8_control::View::live) ||
      view == static_cast<uint8_t>(ft8_control::View::hunter);
  const bool hunter_view = view == static_cast<uint8_t>(ft8_control::View::hunter);

  d.drawCircle(120, 120, 116, link_color(connected));
  d.drawCircle(120, 120, 111, trace_bright);
  d.setTextColor(accent, bg); d.setTextSize(2);
  draw_text(d, "FT8 RX", 120, 27);
  d.setTextColor(connected ? green : cyan, bg); d.setTextSize(1);
  draw_text(d, pairing ? "PAIRING" : connected ? "LINKED" : demo ? "DEMO" : "OFFLINE", 120, 43);

  d.fillRoundRect(71, 51, 98, 22, 5, panel);
  d.drawRoundRect(71, 51, 98, 22, 5, cyan);
  d.setTextColor(ink, panel); d.setTextSize(1.5f);
  draw_text(d, ft8_control::view_name(view), 120, 62, 90);

  char line[40];
  const bool clock_ready = state.capabilities & ft8_control::kClockReady;
  const bool decoder_ready = state.capabilities & ft8_control::kDecoderReady;
  std::snprintf(line, sizeof line, "UTC %s   DEC %s",
                clock_ready ? "OK" : "--", decoder_ready ? "OK" : "--");
  d.setTextColor(clock_ready && decoder_ready ? green : dim, bg); d.setTextSize(1);
  draw_text(d, line, 120, 82);

  const bool tuning = (state.capabilities & ft8_control::kExpertTuning) && live_or_hunter && !hunter_view;
  if (tuning) {
    // Expert tuning: the frequency is the thing being changed, so it is large; the band is only context, so it is small.
    d.setTextColor(dim, bg); d.setTextSize(2);
    draw_text(d, connected ? ft8_band_label(state.selected) : "--", 120, 99);
    if (connected) {
      char digits[20];
      const uint32_t hz = state.frequency_hz;
      if (hz % 1000 == 0) std::snprintf(digits, sizeof digits, "%lu.%03lu", (unsigned long)(hz / 1000000), (unsigned long)(hz / 1000 % 1000));
      else std::snprintf(digits, sizeof digits, "%lu.%06lu", (unsigned long)(hz / 1000000), (unsigned long)(hz % 1000000));
      d.setTextColor(cyan, bg); d.setTextSize(std::strlen(digits) <= 8 ? 4 : 3);
      draw_text(d, digits, 120, 133);
    } else {
      d.setTextColor(dim, bg); d.setTextSize(4); draw_text(d, "--.---", 120, 133);
    }
    d.setTextColor(dim, bg); d.setTextSize(1); draw_text(d, "MHz", 120, 156);
    std::snprintf(line, sizeof line, "STEP %s", ft8_control::step_label(ft8_control::step_index(state.capabilities)));
    d.setTextColor(green, bg); d.setTextSize(1.5f); draw_text(d, line, 120, 172);
    d.setTextColor(dim, bg); d.setTextSize(1); draw_text(d, "TAP: BAND", 120, 188);
  } else if (live_or_hunter) {
    d.setTextColor(ink, bg); d.setTextSize(4);
    draw_text(d, connected ? ft8_band_label(state.selected) : "--", 120, 108);
    if (connected) frequency(d, state.frequency_hz, 135, 2, cyan);
    else {
      d.setTextColor(dim, bg); d.setTextSize(2); draw_text(d, "--.---", 120, 135);
    }
    d.setTextColor(dim, bg); d.setTextSize(1); draw_text(d, "MHz", 120, 151);
    if (connected && !hunter_view && (state.capabilities & ft8_control::kExpertEnabled)) {   // expert tuning is on: a centre tap switches to fine tuning
      d.setTextColor(green, bg); d.setTextSize(1); draw_text(d, "TAP: FINE TUNE", 120, 166);
    }
  } else {
    if (connected && state.selected > 0 && state.item_count) {
      std::snprintf(line, sizeof line, "%ld / %lu", long(state.selected),
                    (unsigned long)state.item_count);
      d.setTextColor(ink, bg); d.setTextSize(4); draw_text(d, line, 120, 116);
    } else {
      d.setTextColor(dim, bg); d.setTextSize(4); draw_text(d, "-- / --", 120, 116);
    }
    if (connected) frequency(d, state.frequency_hz, 145, 2, dim);
  }

  if (hunter_view) {
    d.setTextColor((state.capabilities & ft8_control::kHunterActive) ? cyan :
                   (state.capabilities & ft8_control::kHunterComplete) ? green : dim,
                   bg);
    d.setTextSize(1.5f);
    draw_text(d, ft8_control::hunter_state_name(state.capabilities), 120, 166);
    if ((state.capabilities & ft8_control::kHunterSupported) &&
        !(state.capabilities & (ft8_control::kHunterActive |
                               ft8_control::kHunterComplete))) {
      d.fillRoundRect(70, 174, 100, 20, 5, panel);
      d.drawRoundRect(70, 174, 100, 20, 5, cyan);
      d.setTextColor(cyan, panel); d.setTextSize(1);
      draw_text(d, "TOUCH: DEEP", 120, 184, 92);
    }
  }

  // The BACK button owns the bottom of the screen (y >= 200), so the hints stay above it.
  d.setTextSize(1);
  if (!hunter_view && !tuning) {
    d.setTextColor(dim, bg);
    if (view == static_cast<uint8_t>(ft8_control::View::live)) draw_text(d, "TURN: BAND", 120, 170);
    else if (view == static_cast<uint8_t>(ft8_control::View::setup)) draw_text(d, "SETUP: TAB5", 120, 170);
    else draw_text(d, "TURN: SELECT", 120, 170);
    d.setTextColor(pending ? cyan : ink, bg);
    if (view == static_cast<uint8_t>(ft8_control::View::live))
      draw_text(d, state.capabilities & ft8_control::kHunterSupported ? "PRESS: HUNT" : "HUNT --", 120, 186);
    else if (view == static_cast<uint8_t>(ft8_control::View::setup)) draw_text(d, "PRESS: --", 120, 186);
    else draw_text(d, "PRESS: OPEN", 120, 186);
  } else if (hunter_view) {
    d.setTextColor(pending ? cyan : ink, bg);
    draw_text(d, ft8_control::hunter_press_name(state.capabilities), 120, 198);
  }
}

static void draw_splash(lgfx::LGFXBase& d, bool wait) {
  const uint32_t started = millis();
  d.fillScreen(bg); d.setTextDatum(middle_center);
  d.drawPng(badge_start, badge_end - badge_start, 68, 23);
  gradient_text(d, "OrcDial", 120, 153, 4);
  d.setTextSize(1); d.setTextColor(dim, bg);
  draw_text(d, "WIRELESS CONTROL FOR ORCSDR", 120, 181);
  for (int frame = 0; frame < 18; ++frame) {
    d.drawArc(120, 75, 61, 58, -90 + frame * 20, -70 + frame * 20, 0x12332e);
    if (wait) delay(24);
  }
  while (wait && millis() - started < 5000) delay(10);
}

void splash() { draw_splash(M5Dial.Display, true); }

#ifdef ORCDIAL_DOC_CAPTURE
static void export_pixels(lgfx::LGFXBase& canvas) {
  uint8_t row[240 * 3];
  Serial.setTxTimeoutMs(1000);
  Serial.println("ORCDIAL_CAPTURE_BEGIN width=240 height=240 format=RGB888 bytes=172800 ack=row");
  for (int y = 0; y < 240; ++y) {
    canvas.readRectRGB(0, y, 240, 1, row);
    size_t sent = 0;
    const uint32_t write_started = millis();
    while (sent < sizeof(row) && millis() - write_started < 3000) {
      const size_t remaining = sizeof(row) - sent;
      sent += Serial.write(row + sent, remaining > 64 ? 64 : remaining);
      delay(1);
    }
    if (sent != sizeof(row)) return;
    // Let the host drain each row before sending another over USB CDC.
    const uint32_t started = millis();
    while (!Serial.available() && millis() - started < 5000) delay(1);
    if (Serial.read() != 'K') {
      Serial.println("\nORCDIAL_CAPTURE_ERROR row_ack");
      return;
    }
  }
  Serial.println("\nORCDIAL_CAPTURE_END");
}

void capture_frame(bool mark_demo) {
  if (!frame_ready) { Serial.println("ORCDIAL_CAPTURE_ERROR no_framebuffer"); return; }
  if (mark_demo) {
    frame.setTextDatum(middle_center);
    frame.setTextSize(1); frame.setTextColor(cyan, bg);
    frame.drawString("DEMO", 120, 214);
    present();
  }
  export_pixels(frame);
}

void capture_splash() {
  M5Canvas boot(&M5Dial.Display);
  boot.setColorDepth(16);
  if (!boot.createSprite(240, 240)) { Serial.println("ORCDIAL_CAPTURE_ERROR no_framebuffer"); return; }
  draw_splash(boot, false);
  boot.pushSprite(0, 0);
  export_pixels(boot);
}
#endif

void draw(const RadioState& state, Focus focus, bool connected, bool pairing,
          bool demo, View view, Dashboard selected, bool pending_delta,
          bool reel_active, int32_t reel_position, TuneStyle style) {
  if (!frame_attempted) {
    frame_attempted = true;
    frame.setPsram(false);
    frame.setColorDepth(8);
    frame_ready = frame.createSprite(240, 240) != nullptr;
    if (!frame_ready) Serial.println("ORCDIAL_FRAMEBUFFER_UNAVAILABLE");
  }
  lgfx::LGFXBase& d = frame_ready ? static_cast<lgfx::LGFXBase&>(frame)
                                  : static_cast<lgfx::LGFXBase&>(M5Dial.Display);
  d.fillScreen(bg); d.setTextDatum(middle_center);
  const uint32_t now = millis();
  ambient(d, view == View::carousel ? selected : state.dashboard, now);
  if (view == View::connection) {
    const auto& s=device_status;
    d.drawCircle(120,120,112,link_color(connected));d.setTextColor(ink,bg);d.setTextSize(3);draw_text(d,"PAIRING",120,37);
    d.setTextSize(2);draw_text(d,s.trusted?"OrcSDR Tab5":"No trusted tablet",120,62);
    char id[32];const auto* fingerprint=s.trusted?s.peer_identity:s.identity;
    std::snprintf(id,sizeof id,"ID %02X%02X-%02X%02X",fingerprint[0],fingerprint[1],fingerprint[14],fingerprint[15]);
    d.setTextColor(dim,bg);d.setTextSize(1.5);draw_text(d,id,120,83);
    const char* labels[3]{};
    if(device_forget_confirmation) {
      d.setTextColor(ink,bg);d.setTextSize(2);draw_text(d,"Forget tablet?",120,110);
      labels[0]="CANCEL";labels[1]="FORGET & PAIR";
    }else if(s.state==secure::State::verify) {
      char code[8];std::snprintf(code,sizeof code,"%06lu",(unsigned long)s.code);
      d.setTextColor(cyan,bg);d.setTextSize(4);draw_text(d,code,120,111);
      labels[0]="CODES MATCH";labels[1]="CANCEL";
    }else {
      d.setTextColor(s.trusted?green:cyan,bg);d.setTextSize(1.5);
      draw_text(d,s.upgrade?"Pairing upgrade required":s.trusted?"TRUSTED":"NOT PAIRED",120,101);
      d.setTextColor(s.failure!=secure::Failure::none?red:ink,bg);draw_text(d,s.failure!=secure::Failure::none?secure::failure_name(s.failure):secure::state_name(s.state),120,119);
      labels[0]=pairing?"CANCEL PAIRING":s.state==secure::State::connected?"DISCONNECT":s.trusted?"CONNECT":"PAIR";
      labels[1]=s.trusted?"FORGET & RE-PAIR":"BACK";
      labels[2]=s.trusted?(s.boot_connect?"BOOT CONNECT: ON":"BOOT CONNECT: OFF"):nullptr;
    }
    for(int i=0;i<3;++i)if(labels[i]) {
      const int y=141+i*24;
      if(i==device_selection)d.fillRoundRect(30,y-11,180,24,6,panel);
      d.setTextColor(i==device_selection?cyan:dim,bg);d.setTextSize(2);draw_text(d,labels[i],120,y);
    }
    d.fillRoundRect(80,202,80,22,8,panel);d.drawRoundRect(80,202,80,22,8,cyan);
    d.setTextColor(cyan,panel);d.setTextSize(2);d.drawString("BACK",120,213);
    if(demo){d.setTextColor(cyan,bg);d.setTextSize(1);draw_text(d,"DEMO",120,196);}
    present();
    return;
  }
  if (view == View::keypad) {
    keypad_screen(d, connected);
    present(); return;
  }
  if (view == View::settings_menu) {
    settings_menu_screen(d, connected);
    present(); return;
  }
  if (view == View::page) {
    settings_page_screen(d, connected);
    present(); return;
  }
  if (view == View::home && connected && state.dashboard == Dashboard::home) {
    home_screen(d, state, focus, pending_delta, millis());
    present(); return;
  }
  if (view == View::home) {
    d.drawCircle(120, 120, 115, link_color(connected));
    d.drawCircle(120, 120, 110, 0x1b4e60);
    d.drawPng(badge_start, badge_end - badge_start, 68, 29);
    gradient_text(d, "OrcDial", 120, 151, 3);
    d.setTextColor(connected ? green : cyan, bg); d.setTextSize(2);
    draw_text(d, connected ? dashboard_name(state.dashboard) : demo ? "DEMO" : "OFFLINE", 120, 181);
    back_button(d);
    present(); return;
  }
  if (view == View::carousel) {
    const int index = carousel_index(selected);
    const auto prev = carousel[(index + carousel_count - 1) % carousel_count];
    const auto next = carousel[(index + 1) % carousel_count];
    const uint32_t accent = accent_for(selected, connected);
    d.drawCircle(120, 120, 115, link_color(connected));
    for (int i = 0; i < 12; ++i) {
      const float a = (i * 30 - 90) * 0.017453293f;
      d.drawLine(120 + int(108 * cosf(a)), 120 + int(108 * sinf(a)),
                 120 + int(113 * cosf(a)), 120 + int(113 * sinf(a)), trace_bright);
    }
    d.setTextColor(ink, bg); d.setTextSize(2); draw_text(d, "DASHBOARDS", 120, 32);
    d.fillRoundRect(43, 61, 154, 119, 16, panel);
    d.drawRoundRect(43, 61, 154, 119, 16, accent);
    d.fillRoundRect(3, 91, 38, 64, 8, panel);
    d.fillRoundRect(199, 91, 38, 64, 8, panel);
    d.drawRoundRect(3, 91, 38, 64, 8, trace_bright);
    d.drawRoundRect(199, 91, 38, 64, 8, trace_bright);
    selector_icon(d, selected==devices_entry?Dashboard::settings:selected, 120, 103);
    selector_icon(d, prev, 22, 120, true);
    selector_icon(d, next, 218, 120, true);
    d.setTextColor(ink, panel);
    d.setTextSize(3);
    if (d.textWidth(dashboard_name(selected)) > 146) d.setTextSize(2);
    draw_text(d, dashboard_name(selected), 120, 151);
    d.setTextColor(dim, panel); d.setTextSize(1);
    draw_text(d, "<", 22, 145); draw_text(d, ">", 218, 145);
    for (int i = -2; i <= 2; ++i)
      d.fillRoundRect(93 + (i + 2) * 12, 184, 8, 3, 1, i == 0 ? accent : trace_bright);
    char count[20]; std::snprintf(count, sizeof count, "%d / %d", index + 1, carousel_count);
    d.setTextColor(dim, bg); d.setTextSize(2); draw_text(d, count, 120, 202);
    d.setTextSize(1);
    draw_text(d, connected ? "PRESS TO OPEN" : demo ? "DEMO PREVIEW" : "OFFLINE PREVIEW", 120, 217);
    present(); return;
  }
  if (state.dashboard == Dashboard::fm) {
    fm_screen(d, state, focus, connected, pairing, demo, pending_delta, reel_position);
    present(); return;
  }
  if (state.dashboard == Dashboard::ft8) {
    ft8_screen(d, state, connected, pairing, demo, pending_delta);
    present(); return;
  }
  const auto dashboard = state.dashboard;
  const bool can_tune = tunable(dashboard);
  const uint32_t accent = accent_for(dashboard, connected);
  d.drawCircle(120, 120, 116, link_color(connected));
  d.drawCircle(120, 120, 112, 0x1b4e60);
  d.setTextColor(accent, bg); d.setTextSize(2);
  draw_text(d, dashboard_name(dashboard), 120, 34);
  d.setTextColor(connected ? green : cyan, bg); d.setTextSize(1);
  draw_text(d, pairing ? "PAIRING" : connected ? "LINKED" : demo ? "DEMO" : "OFFLINE", 120, 48);
  static const char* modes[] = {"--", "NFM", "AM", "WFM"};
  static const char* styles[] = {"REEL", "DIAL", "ODOM", "TAPE", "SPLIT"};
  if (can_tune || channel_dashboard(dashboard)) {
    char tag[32];
    if (can_tune) std::snprintf(tag, sizeof tag, "%s   %s", modes[state.mode < 4 ? state.mode : 0], styles[unsigned(style)]);
    else std::snprintf(tag, sizeof tag, "%s", modes[state.mode < 4 ? state.mode : 0]);
    d.setTextColor(dim, bg); d.setTextSize(1);
    draw_text(d, tag, 120, 64);
  }
  artwork(d, dashboard, accent);
  char line[32];
  if (channel_dashboard(dashboard)) {
    d.setTextColor(ink, bg); d.setTextSize(4);
    if (connected && state.selected > 0) std::snprintf(line, sizeof line, "CH %ld", long(state.selected));
    else std::snprintf(line, sizeof line, "CH --");
    draw_text(d, line, 120, 113);
    frequency(d, state.frequency_hz, 140, 2, dim);
    d.setTextColor(dim, bg); d.setTextSize(1);
    draw_text(d, connected && state.selected > 0 ? "CHANNEL" : "CHANNEL DATA --", 120, 190);
  } else if (!can_tune) {
    d.setTextColor(dim, bg); d.setTextSize(2);
    draw_text(d, content_view(dashboard, state.view), 120, 176);
    if (dashboard == Dashboard::adsb && connected && state.selected > 0 && state.view == 0)
      std::snprintf(line, sizeof line, "RANGE %ld NM", long(state.selected));
    else if (dashboard == Dashboard::p25 && connected && state.item_count)
      std::snprintf(line, sizeof line, "CANDIDATE %ld/%lu", long(state.selected), (unsigned long)state.item_count);
    else std::snprintf(line, sizeof line, "%s", dashboard == Dashboard::settings ? "DEVICE SETTINGS" : "NO LIVE DATA");
    d.setTextSize(1); draw_text(d, line, 120, 192);
  } else if (focus == Focus::vfo || focus == Focus::step) {
    switch (style) {
      case TuneStyle::reel: {
        for (int i = 0; i < 24; ++i) {
          const float a = i * 6.2831853f / 24;
          d.drawLine(120 + int(104 * cosf(a)), 120 + int(104 * sinf(a)),
                     120 + int(109 * cosf(a)), 120 + int(109 * sinf(a)), i < 18 ? accent : dim);
        }
        if (reel_active && focus == Focus::vfo) {
          const float a = (reel_position % 24) * 6.2831853f / 24 - 1.5707963f;
          d.fillCircle(120 + int(113 * cosf(a)), 120 + int(113 * sinf(a)), 3, ink);
          frequency(d, clamp_frequency(int64_t(state.frequency_hz) - state.step_hz), 79, 2, dim);
          frequency(d, clamp_frequency(int64_t(state.frequency_hz) + state.step_hz), 140, 2, dim);
        } else { d.setTextColor(dim, bg); d.setTextSize(2); draw_text(d, "MHz", 120, 140); }
        frequency(d, state.frequency_hz, 110, 3, ink);
        break;
      }
      case TuneStyle::dial: {
        d.drawArc(120, 115, 101, 97, 40, 320, dim);
        for (int i = 0; i < 13; ++i) {
          const float a = (140 + i * 20) * 0.017453293f;
          d.drawLine(120 + int(92 * cosf(a)), 115 + int(92 * sinf(a)),
                     120 + int(101 * cosf(a)), 115 + int(101 * sinf(a)), accent);
        }
        const float a = (140 + (reel_position % 13) * 20) * 0.017453293f;
        d.drawLine(120 + int(82 * cosf(a)), 115 + int(82 * sinf(a)),
                   120 + int(101 * cosf(a)), 115 + int(101 * sinf(a)), ink);
        frequency(d, state.frequency_hz, 108, 3, ink);
        d.setTextColor(dim, bg); d.setTextSize(2); draw_text(d, "MHz", 120, 139);
        break;
      }
      case TuneStyle::odometer: {
        const uint32_t parts[] = {state.frequency_hz / 1000000,
                                  state.frequency_hz / 1000 % 1000, state.frequency_hz % 1000};
        const char* labels[] = {"MHz", "kHz", "Hz"};
        for (int i = 0; i < 3; ++i) {
          const int x = 18 + 69 * i;
          d.drawRoundRect(x, 85, 65, 52, 7, i == 1 ? accent : dim);
          std::snprintf(line, sizeof line, i == 0 ? "%lu" : "%03lu", (unsigned long)parts[i]);
          d.setTextColor(ink, bg); d.setTextSize(parts[i] > 999 ? 2 : 3);
          draw_text(d, line, x + 32, 111, 57);
          d.setTextColor(dim, bg); d.setTextSize(1); draw_text(d, labels[i], x + 32, 149);
        }
        break;
      }
      case TuneStyle::tape: {
        frequency(d, state.frequency_hz, 92, 3, ink);
        d.drawFastHLine(21, 132, 198, dim);
        for (int i = -4; i <= 4; ++i) {
          const int x = 120 + i * 23;
          d.drawFastVLine(x, 126, i == 0 ? 22 : (i % 2 ? 9 : 15), i == 0 ? accent : dim);
        }
        d.fillTriangle(115, 119, 125, 119, 120, 126, accent);
        d.setTextColor(dim, bg); d.setTextSize(1);
        std::snprintf(line, sizeof line, "-%lu", (unsigned long)state.step_hz); draw_text(d, line, 56, 151);
        std::snprintf(line, sizeof line, "+%lu", (unsigned long)state.step_hz); draw_text(d, line, 184, 151);
        break;
      }
      case TuneStyle::split: {
        d.drawArc(120, 113, 101, 98, 205, 335, accent);
        d.drawArc(120, 113, 93, 91, 25, 155, dim);
        std::snprintf(line, sizeof line, "%lu", (unsigned long)(state.frequency_hz / 1000000));
        d.setTextColor(ink, bg); d.setTextSize(state.frequency_hz >= 1000000000 ? 3 : 4);
        draw_text(d, line, 120, 90);
        std::snprintf(line, sizeof line, ".%06lu", (unsigned long)(state.frequency_hz % 1000000));
        d.setTextColor(green, bg); d.setTextSize(3); draw_text(d, line, 120, 128);
        d.setTextColor(dim, bg); d.setTextSize(1); draw_text(d, "MHz", 120, 151);
        break;
      }
      default: break;
    }
  } else {
    const char* label = focus == Focus::gain ? "GAIN" : focus == Focus::squelch ? "SQUELCH" : "VOLUME";
    int value = focus == Focus::gain ? state.gain_tenth_db : focus == Focus::squelch ? state.squelch : state.volume;
    std::snprintf(line, sizeof line, "%s  %d", label, value);
    d.setTextColor(ink, bg); d.setTextSize(3); draw_text(d, line, 120, 108);
  }
  if (can_tune) {
    if (state.signal_valid) std::snprintf(line, sizeof line, "%d dBm", state.signal_dbm);
    else std::snprintf(line, sizeof line, "-- dB");
    d.setTextColor(state.signal_valid ? green : dim, bg); d.setTextSize(2); draw_text(d, line, 120, 164);
    if (state.step_hz % 1000 == 0)
      std::snprintf(line, sizeof line, "STEP %lu kHz", (unsigned long)(state.step_hz / 1000));
    else
      std::snprintf(line, sizeof line, "STEP %lu Hz", (unsigned long)state.step_hz);
    d.setTextColor(focus == Focus::step ? green : ink, bg); d.setTextSize(2); draw_text(d, line, 120, 188);
  }
  if (pending_delta) d.fillCircle(206, 110, 3, cyan);
  back_button(d);
  present();
}
} // namespace orc
