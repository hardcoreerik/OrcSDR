#include "ui.hpp"
#include "controller.hpp"
#include <cmath>
#include <cstdio>

namespace orc {
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
  {am_start, am_end}
};
static_assert(sizeof(dashboard_images) / sizeof(dashboard_images[0]) ==
              static_cast<unsigned>(Dashboard::am) + 1);
static constexpr uint32_t ink = 0xe8f5f6, dim = 0x88a4ad, green = 0x70f847;
static constexpr uint32_t bg = 0x050f16, cyan = 0x38d9ff, blue = 0x319bff;
static constexpr uint32_t panel = 0x0c202b, trace = 0x163747, trace_bright = 0x286174;
static M5Canvas frame(&M5Dial.Display);
static bool frame_attempted = false, frame_ready = false;
static void present() { if (frame_ready) frame.pushSprite(0, 0); }
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
  std::snprintf(text, sizeof text, "%lu.%06lu", (unsigned long)(hz / 1000000),
                (unsigned long)(hz % 1000000));
  d.setTextColor(color, bg); d.setTextSize(size); d.drawString(text, 120, y);
}

static void fm_screen(lgfx::LGFXBase& d, const RadioState& state, Focus focus,
                      bool connected, bool pairing, bool demo, bool pending,
                      int32_t reel_position) {
  d.drawCircle(120, 120, 115, green);
  d.drawCircle(120, 120, 108, trace_bright);
  for (int i = 0; i < 24; ++i) {
    const float a = i * 6.2831853f / 24 - 1.5707963f;
    const int inner = i % 3 ? 100 : 96;
    d.drawLine(120 + int(inner * cosf(a)), 120 + int(inner * sinf(a)),
               120 + int(105 * cosf(a)), 120 + int(105 * sinf(a)),
               i == (reel_position % 24 + 24) % 24 ? green : trace_bright);
  }
  d.setTextColor(cyan, bg); d.setTextSize(2); d.drawString("FM", 120, 28);
  d.setTextColor(connected ? green : dim, bg); d.setTextSize(1);
  d.drawString(pairing ? "PAIRING" : connected ? "LINKED" : demo ? "DEMO" : "OFFLINE", 120, 48);

  char line[32];
  if (state.step_hz >= 1000 && state.frequency_hz % 1000 == 0)
    std::snprintf(line, sizeof line, "%lu.%03lu", (unsigned long)(state.frequency_hz / 1000000),
                  (unsigned long)(state.frequency_hz / 1000 % 1000));
  else
    std::snprintf(line, sizeof line, "%lu.%06lu", (unsigned long)(state.frequency_hz / 1000000),
                  (unsigned long)(state.frequency_hz % 1000000));
  d.setTextColor(ink, bg); d.setTextSize(4);
  if (d.textWidth(line) > 190) d.setTextSize(3);
  d.drawString(line, 120, 106);
  d.setTextColor(cyan, bg); d.setTextSize(2); d.drawString("MHz", 120, 136);

  const char* names[] = {"TUNE", "STEP", "VOL"};
  const Focus choices[] = {Focus::vfo, Focus::step, Focus::volume};
  for (int i = 0; i < 3; ++i) {
    const int x = 31 + i * 61;
    const bool active = focus == choices[i];
    d.fillRoundRect(x, 157, 56, 24, 5, active ? panel : bg);
    d.drawRoundRect(x, 157, 56, 24, 5, active ? green : trace_bright);
    d.setTextColor(active ? green : dim, active ? panel : bg);
    d.setTextSize(1); d.drawString(names[i], x + 28, 169);
  }
  d.setTextColor(dim, bg); d.setTextSize(1);
  if (state.step_hz % 1000 == 0)
    std::snprintf(line, sizeof line, "STEP %lu kHz", (unsigned long)(state.step_hz / 1000));
  else
    std::snprintf(line, sizeof line, "STEP %lu Hz", (unsigned long)state.step_hz);
  d.drawString(line, 77, 199);
  std::snprintf(line, sizeof line, "VOL %u", unsigned(state.volume));
  d.drawString(line, 169, 199);
  d.setTextColor(pending ? cyan : dim, bg); d.setTextSize(1);
  d.drawString(pending ? "TUNING..." : "PRESS: NEXT  /  HOLD: HOME", 120, 220);
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
      d.drawLine(164, 151, 164, 139, c); d.drawLine(157, 151, 164, 139, c);
      d.drawLine(164, 139, 171, 151, c); d.drawArc(164, 143, 16, 15, 210, 330, c);
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
      d.setTextColor(c, bg); d.setTextSize(1); d.drawString("CB", 120, 78);
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
    default: break;
  }
}
static void selector_icon(lgfx::LGFXBase& d, Dashboard id, int x, int y,
                          bool small = false) {
  const unsigned index = static_cast<unsigned>(id);
  if (index >= sizeof(dashboard_images) / sizeof(dashboard_images[0])) return;
  const auto& image = dashboard_images[index];
  if (!image.start) return;
  const int size = small ? 30 : 64;
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

void splash() {
  auto& d = M5Dial.Display;
  const uint32_t started = millis();
  d.fillScreen(bg); d.setTextDatum(middle_center);
  d.drawPng(badge_start, badge_end - badge_start, 68, 23);
  d.setTextColor(ink, bg); d.setTextSize(3); d.drawString("ORCDIAL", 120, 153);
  d.setTextSize(1); d.setTextColor(dim, bg);
  d.drawString("ORCSDR WIRELESS CONTROL", 120, 179);
  for (int frame = 0; frame < 18; ++frame) {
    d.drawArc(120, 75, 61, 58, -90 + frame * 20, -70 + frame * 20, 0x12332e);
    delay(24);
  }
  while (millis() - started < 5000) delay(10);
}

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
    d.drawCircle(120, 120, 112, cyan);
    d.setTextColor(green, bg); d.setTextSize(3); d.drawString("ORCDIAL", 120, 56);
    d.setTextColor(ink, bg); d.setTextSize(3); d.drawString(connected ? "CONNECTED" : "CONNECT", 120, 104);
    d.setTextSize(2); d.drawString(pairing ? "SEARCHING" : "TAP TO PAIR", 120, 145);
    d.setTextColor(dim, bg); d.setTextSize(1); d.drawString("BOTTOM: BACK", 120, 190);
    present();
    return;
  }
  if (view == View::home) {
    d.drawCircle(120, 120, 115, green);
    d.drawCircle(120, 120, 110, 0x1b4e60);
    d.drawPng(badge_start, badge_end - badge_start, 68, 29);
    d.setTextColor(ink, bg); d.setTextSize(3); d.drawString("OrcSDR", 120, 151);
    d.setTextColor(connected ? green : cyan, bg); d.setTextSize(2);
    d.drawString(connected ? dashboard_name(state.dashboard) : demo ? "DEMO" : "OFFLINE", 120, 181);
    d.setTextColor(dim, bg); d.setTextSize(1);
    d.drawString("PRESS OR TAP: DASHBOARDS", 120, 209);
    present(); return;
  }
  if (view == View::carousel) {
    const int index = carousel_index(selected);
    const auto prev = carousel[(index + carousel_count - 1) % carousel_count];
    const auto next = carousel[(index + 1) % carousel_count];
    const uint32_t accent = accent_for(selected, connected);
    d.drawCircle(120, 120, 115, accent);
    for (int i = 0; i < 12; ++i) {
      const float a = (i * 30 - 90) * 0.017453293f;
      d.drawLine(120 + int(108 * cosf(a)), 120 + int(108 * sinf(a)),
                 120 + int(113 * cosf(a)), 120 + int(113 * sinf(a)), trace_bright);
    }
    d.setTextColor(ink, bg); d.setTextSize(2); d.drawString("DASHBOARDS", 120, 32);
    d.fillRoundRect(43, 61, 154, 119, 16, panel);
    d.drawRoundRect(43, 61, 154, 119, 16, accent);
    d.fillRoundRect(3, 91, 38, 64, 8, panel);
    d.fillRoundRect(199, 91, 38, 64, 8, panel);
    d.drawRoundRect(3, 91, 38, 64, 8, trace_bright);
    d.drawRoundRect(199, 91, 38, 64, 8, trace_bright);
    selector_icon(d, selected, 120, 103);
    selector_icon(d, prev, 22, 120, true);
    selector_icon(d, next, 218, 120, true);
    d.setTextColor(ink, panel);
    d.setTextSize(3);
    if (d.textWidth(dashboard_name(selected)) > 146) d.setTextSize(2);
    d.drawString(dashboard_name(selected), 120, 151);
    d.setTextColor(dim, panel); d.setTextSize(1);
    d.drawString("<", 22, 145); d.drawString(">", 218, 145);
    for (int i = -2; i <= 2; ++i)
      d.fillRoundRect(93 + (i + 2) * 12, 184, 8, 3, 1, i == 0 ? accent : trace_bright);
    char count[20]; std::snprintf(count, sizeof count, "%d / %d", index + 1, carousel_count);
    d.setTextColor(dim, bg); d.setTextSize(2); d.drawString(count, 120, 202);
    d.setTextSize(1);
    d.drawString(connected ? "PRESS TO OPEN" : demo ? "DEMO PREVIEW" : "OFFLINE PREVIEW", 120, 222);
    present(); return;
  }
  if (state.dashboard == Dashboard::fm) {
    fm_screen(d, state, focus, connected, pairing, demo, pending_delta, reel_position);
    present(); return;
  }
  const auto dashboard = state.dashboard;
  const bool can_tune = tunable(dashboard);
  const uint32_t accent = accent_for(dashboard, connected);
  d.drawCircle(120, 120, 116, accent);
  d.drawCircle(120, 120, 112, 0x1b4e60);
  d.setTextColor(accent, bg); d.setTextSize(2);
  d.drawString(dashboard_name(dashboard), 120, 27);
  d.setTextColor(connected ? green : cyan, bg); d.setTextSize(1);
  d.drawString(pairing ? "PAIRING" : connected ? "LINKED" : demo ? "DEMO" : "OFFLINE", 120, 48);
  static const char* modes[] = {"--", "NFM", "AM", "WFM"};
  if (can_tune || channel_dashboard(dashboard)) {
    d.setTextColor(dim, bg); d.setTextSize(1);
    d.drawString(modes[state.mode < 4 ? state.mode : 0], 120, 64);
  }
  artwork(d, dashboard, accent);
  char line[32];
  if (channel_dashboard(dashboard)) {
    d.setTextColor(ink, bg); d.setTextSize(4);
    if (connected && state.selected > 0) std::snprintf(line, sizeof line, "CH %ld", long(state.selected));
    else std::snprintf(line, sizeof line, "CH --");
    d.drawString(line, 120, 113);
    frequency(d, state.frequency_hz, 140, 2, dim);
    d.setTextColor(dim, bg); d.setTextSize(1);
    d.drawString(connected && state.selected > 0 ? "CHANNEL" : "CHANNEL DATA --", 120, 195);
  } else if (!can_tune) {
    d.setTextColor(dim, bg); d.setTextSize(2);
    d.drawString(content_view(dashboard, state.view), 120, 176);
    if (dashboard == Dashboard::adsb && connected && state.selected > 0 && state.view == 0)
      std::snprintf(line, sizeof line, "RANGE %ld NM", long(state.selected));
    else if (dashboard == Dashboard::p25 && connected && state.item_count)
      std::snprintf(line, sizeof line, "CANDIDATE %ld/%lu", long(state.selected), (unsigned long)state.item_count);
    else std::snprintf(line, sizeof line, "%s", dashboard == Dashboard::settings ? "DEVICE SETTINGS" : "NO LIVE DATA");
    d.setTextSize(1); d.drawString(line, 120, 198);
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
        } else { d.setTextColor(dim, bg); d.setTextSize(2); d.drawString("MHz", 120, 140); }
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
        d.setTextColor(dim, bg); d.setTextSize(2); d.drawString("MHz", 120, 139);
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
          d.drawString(line, x + 32, 111);
          d.setTextColor(dim, bg); d.setTextSize(1); d.drawString(labels[i], x + 32, 149);
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
        std::snprintf(line, sizeof line, "-%lu", (unsigned long)state.step_hz); d.drawString(line, 56, 151);
        std::snprintf(line, sizeof line, "+%lu", (unsigned long)state.step_hz); d.drawString(line, 184, 151);
        break;
      }
      case TuneStyle::split: {
        d.drawArc(120, 113, 101, 98, 205, 335, accent);
        d.drawArc(120, 113, 93, 91, 25, 155, dim);
        std::snprintf(line, sizeof line, "%lu", (unsigned long)(state.frequency_hz / 1000000));
        d.setTextColor(ink, bg); d.setTextSize(state.frequency_hz >= 1000000000 ? 3 : 4);
        d.drawString(line, 120, 90);
        std::snprintf(line, sizeof line, ".%06lu", (unsigned long)(state.frequency_hz % 1000000));
        d.setTextColor(green, bg); d.setTextSize(3); d.drawString(line, 120, 128);
        d.setTextColor(dim, bg); d.setTextSize(1); d.drawString("MHz", 120, 151);
        break;
      }
      default: break;
    }
  } else {
    const char* label = focus == Focus::gain ? "GAIN" : focus == Focus::squelch ? "SQUELCH" : "VOLUME";
    int value = focus == Focus::gain ? state.gain_tenth_db : focus == Focus::squelch ? state.squelch : state.volume;
    std::snprintf(line, sizeof line, "%s  %d", label, value);
    d.setTextColor(ink, bg); d.setTextSize(3); d.drawString(line, 120, 108);
  }
  if (can_tune) {
    if (state.signal_valid) std::snprintf(line, sizeof line, "%d dBm", state.signal_dbm);
    else std::snprintf(line, sizeof line, "-- dB");
    d.setTextColor(state.signal_valid ? green : dim, bg); d.setTextSize(2); d.drawString(line, 120, 164);
    std::snprintf(line, sizeof line, "STEP %lu Hz", (unsigned long)state.step_hz);
    d.setTextColor(focus == Focus::step ? green : ink, bg); d.setTextSize(2); d.drawString(line, 120, 188);
  }
  if (pending_delta) { d.setTextColor(cyan, bg); d.setTextSize(1); d.drawString("TUNING...", 120, 205); }
  static const char* styles[] = {"REEL", "DIAL", "ODOM", "TAPE", "SPLIT"};
  d.setTextColor(dim, bg); d.setTextSize(2);
  std::snprintf(line, sizeof line, "%s %u/5 >", styles[unsigned(style)], unsigned(style) + 1);
  d.drawString(can_tune ? line : channel_dashboard(dashboard) ? "CH  VOL   HOME >" :
               "HOME   DASHBOARDS >", 120, 218);
  present();
}
} // namespace orc
