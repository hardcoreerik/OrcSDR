#include "ui.hpp"
#include <cstdio>

namespace orc {
extern const uint8_t badge_start[] asm("_binary_assets_orc_badge_104_png_start");
extern const uint8_t badge_end[] asm("_binary_assets_orc_badge_104_png_end");
static constexpr uint32_t ink = 0xe8eee9, dim = 0x74918b, green = 0x43f0b2;
static constexpr uint32_t bg = 0x07100f, amber = 0xffbc66;
static M5Canvas frame(&M5Dial.Display);
static bool frame_attempted = false, frame_ready = false;
static void present() { if (frame_ready) frame.pushSprite(0, 0); }
static void frequency(lgfx::LGFXBase& d, uint32_t hz, int y, int size, uint32_t color) {
  char text[20];
  std::snprintf(text, sizeof text, "%lu.%06lu", (unsigned long)(hz / 1000000),
                (unsigned long)(hz % 1000000));
  d.setTextColor(color, bg); d.setTextSize(size); d.drawString(text, 120, y);
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
  if (view == View::connection) {
    d.drawCircle(120, 120, 112, amber);
    d.setTextColor(green, bg); d.setTextSize(3); d.drawString("ORCDIAL", 120, 56);
    d.setTextColor(ink, bg); d.setTextSize(3); d.drawString(connected ? "CONNECTED" : "CONNECT", 120, 104);
    d.setTextSize(2); d.drawString(pairing ? "SEARCHING" : "TAP TO PAIR", 120, 145);
    d.setTextColor(dim, bg); d.setTextSize(1); d.drawString("BOTTOM: BACK", 120, 190);
    present();
    return;
  }
  if (view == View::home) {
    d.drawCircle(120, 120, 115, green);
    d.drawCircle(120, 120, 110, 0x245047);
    d.drawPng(badge_start, badge_end - badge_start, 68, 29);
    d.setTextColor(ink, bg); d.setTextSize(3); d.drawString("OrcSDR", 120, 151);
    d.setTextColor(connected ? green : amber, bg); d.setTextSize(2);
    d.drawString(connected ? dashboard_name(state.dashboard) : "OFFLINE", 120, 181);
    d.setTextColor(dim, bg); d.setTextSize(1);
    d.drawString("PRESS OR TAP: DASHBOARDS", 120, 209);
    present(); return;
  }
  if (view == View::carousel) {
    const int index = carousel_index(selected);
    const auto prev = carousel[(index + carousel_count - 1) % carousel_count];
    const auto next = carousel[(index + 1) % carousel_count];
    d.drawCircle(120, 120, 115, green);
    d.setTextColor(dim, bg); d.setTextSize(2); d.drawString("DASHBOARDS", 120, 35);
    d.drawRoundRect(56, 76, 128, 92, 15, green);
    d.drawRoundRect(2, 91, 48, 62, 8, dim);
    d.drawRoundRect(190, 91, 48, 62, 8, dim);
    d.setTextColor(ink, bg); d.setTextSize(2); d.drawString(dashboard_name(selected), 120, 120);
    d.setTextColor(dim, bg); d.setTextSize(1);
    char side[5]; std::snprintf(side, sizeof side, "%.4s", dashboard_name(prev)); d.drawString(side, 26, 121);
    std::snprintf(side, sizeof side, "%.4s", dashboard_name(next)); d.drawString(side, 214, 121);
    char count[20]; std::snprintf(count, sizeof count, "%d / %d", index + 1, carousel_count);
    d.drawString(count, 120, 190);
    d.drawString(connected ? "PRESS TO OPEN" : "OFFLINE PREVIEW", 120, 214);
    present(); return;
  }
  const auto dashboard = state.dashboard;
  const bool can_tune = tunable(dashboard);
  const uint32_t accent = !connected ? amber :
    dashboard == Dashboard::am || dashboard == Dashboard::cb || dashboard == Dashboard::satellite ? 0xffb34e :
    dashboard == Dashboard::lora ? 0x55e997 :
    dashboard == Dashboard::weather || dashboard == Dashboard::airband || dashboard == Dashboard::marine ? 0x54d4ff : green;
  d.drawCircle(120, 120, 116, accent);
  d.drawCircle(120, 120, 112, 0x245047);
  d.setTextColor(accent, bg); d.setTextSize(2);
  d.drawString(dashboard_name(dashboard), 120, 27);
  d.setTextColor(connected ? green : amber, bg); d.setTextSize(1);
  d.drawString(pairing ? "PAIRING" : connected ? "LINKED" : demo ? "OFFLINE" : "SEARCHING", 120, 48);
  static const char* modes[] = {"--", "NFM", "AM", "WFM"};
  d.setTextColor(dim, bg); d.setTextSize(3);
  d.drawString(modes[state.mode < 4 ? state.mode : 0], 120, 67);
  char line[32];
  if (!can_tune) {
    d.drawArc(120, 122, 93, 89, 30, 330, accent);
    d.setTextColor(ink, bg); d.setTextSize(3); d.drawString(dashboard_name(dashboard), 120, 115);
    d.setTextColor(dim, bg); d.setTextSize(2); d.drawString("NO LIVE DATA", 120, 151);
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
  if (pending_delta) { d.setTextColor(amber, bg); d.setTextSize(1); d.drawString("TUNING...", 120, 205); }
  static const char* styles[] = {"REEL", "DIAL", "ODOM", "TAPE", "SPLIT"};
  d.setTextColor(dim, bg); d.setTextSize(2);
  std::snprintf(line, sizeof line, "%s %u/5 >", styles[unsigned(style)], unsigned(style) + 1);
  d.drawString(can_tune ? line : "HOME     DASHBOARDS >", 120, 218);
  present();
}
} // namespace orc
