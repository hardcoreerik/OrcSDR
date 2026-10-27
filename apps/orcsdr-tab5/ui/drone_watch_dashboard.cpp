#include "drone_watch_dashboard.hpp"

#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "dashboard_audio_control.hpp"
#include "screen_controller.hpp"

namespace orcsdr::drone_watch {
namespace {
constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kEdge = 0x2945;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kAmber = 0xfde0;

Hooks g_hooks{};
SourceSnapshot g_source{};
bool g_active = false;
bool g_detail = false;
bool g_help = false;
uint32_t g_now = 0;
const char* g_headline = nullptr;
uint32_t g_count = 0;

bool compact() { return M5.Display.width() < 1000; }
int width() { return M5.Display.width(); }
bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && y >= by && x < bx + bw && y < by + bh;
}
void label(const char* text, int x, int y, uint16_t color, uint8_t scale = 2) {
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(color, kPanel);
  M5.Display.setTextSize(scale);
  M5.Display.drawString(text, x, y);
}
void on_bg(const char* text, int x, int y, uint16_t color, uint8_t scale = 2) {
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(color, kBg);
  M5.Display.setTextSize(scale);
  M5.Display.drawString(text, x, y);
}
void panel(int x, int y, int w, int h) {
  M5.Display.fillRoundRect(x, y, w, h, 10, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 10, kEdge);
}
void button(int x, int y, int w, const char* caption, bool selected = false) {
  const int h = 54;
  const uint16_t edge = selected ? kGreen : kCyan;
  M5.Display.fillRoundRect(x, y, w, h, 8, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 8, edge);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(selected ? kGreen : TFT_WHITE, kPanel);
  M5.Display.drawString(caption, x + w / 2, y + h / 2);
}
void header() {
  if (!compact()) {
    audio_header::draw_brand("DRONE WATCH");
    audio_header::draw_battery(g_hooks.battery_percent ? g_hooks.battery_percent() : -1);
    audio_header::draw_home_button();
    audio_header::draw_mute_button(g_hooks.sound_enabled ? g_hooks.sound_enabled() : false);
    audio_header::draw_visualizer_button(false);
    audio_header::draw_settings_button();
  } else {
    on_bg("OrcSDR  /  DRONE WATCH", 22, 44, kCyan, 3);
    // Canonical 1280px header's Home position is outside square screens.
    button(width() - 142, 18, 116, "HOME");
  }
  M5.Display.drawFastHLine(20, 109, width() - 40, kEdge);
  on_bg("W01   LIVE WATCH", 24, 147, kCyan, compact() ? 2 : 3);
  if (!compact()) on_bg("PASSIVE REMOTE ID  /  RECEIVE ONLY", 635, 146, kMuted);
}
void footer() {
  constexpr const char* categories[5] = {"WATCH", "AIRCRAFT", "RF LAB", "MEDIA", "SYSTEM"};
  const int tab_gap = 12;
  const int tab_w = (width() - 40 - tab_gap * 4) / 5;
  for (int i = 0; i < 5; ++i) {
    const int x = 20 + i * (tab_w + tab_gap);
    M5.Display.fillRoundRect(x, 649, tab_w, 54, 8, kPanel);
    M5.Display.drawRoundRect(x, 649, tab_w, 54, 8, i == 0 ? kGreen : kEdge);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(i == 0 ? kGreen : kMuted, kPanel);
    M5.Display.drawString(categories[i], x + tab_w / 2, 676);
  }
}
void headline() {
  const Message m = describe(g_source, g_now);
  const bool small = compact();
  const int bw = small ? width() - 40 : 802;
  const int by = small ? 187 : 191;
  const int bh = small ? 198 : 302;
  panel(20, by, bw, bh);
  label("RECEPTION STATUS", 42, by + 38, kCyan, 2);
  label(m.headline, 42, by + (small ? 91 : 112),
        m.monitoring ? kGreen : kAmber, small ? 3 : 4);
  if (small) {
    label(m.monitoring ? "Qualified capture source listening." :
          "No verified receiver is active.", 42, by + 148, kMuted, 2);
  } else {
    label(m.explanation, 42, by + 178, kMuted, 2);
    label("A receiver's silence does not mean no aircraft nearby.", 42, by + 236, kMuted, 2);
  }
  g_headline = m.headline;
  g_count = g_source.packets_received;
}
void source_panel() {
  const bool small = compact();
  const int bx = small ? 20 : 840, by = small ? 399 : 191;
  const int bw = small ? width() - 40 : width() - 860;
  const int bh = small ? 163 : 302;
  panel(bx, by, bw, bh);
  label(g_help ? "HOW IT WORKS" : g_detail ? "SOURCE DETAILS" : "RECEIVER CAPABILITY",
        bx + 20, by + 32, kCyan);
  if (g_help) {
    label("REMOTE ID is broadcast data.", bx + 20, by + 77, TFT_WHITE, 2);
    label("No RF signal proves identity.", bx + 20, by + 112, kMuted, 2);
    if (!small) label("Silence is not an all-clear.", bx + 20, by + 156, kMuted);
  } else if (g_detail && g_source.capture_qualified) {
    label("QUALIFIED SOURCE ADAPTER", bx + 20, by + 79, kGreen, 2);
    label(describe(g_source, g_now).headline, bx + 20, by + 122, kCyan, 2);
    if (!small) label("Source measurement, not aircraft proof.", bx + 20, by + 167, kMuted);
  } else if (g_detail) {
    label("ONBOARD C6: NOT VERIFIED", bx + 20, by + 79, kAmber, 2);
    label("BLE / WIFI RID: UNAVAILABLE", bx + 20, by + 122, kMuted, 2);
    if (!small) {
      label("OrcNode link: NOT PAIRED", bx + 20, by + 167, kMuted, 2);
      label("No qualified RID adapter.", bx + 20, by + 213, kMuted);
    }
  } else {
    label("REMOTE ID SOURCE", bx + 20, by + 86, kMuted);
    label(g_source.capture_qualified ? "QUALIFIED" : "NOT IMPLEMENTED",
          bx + 20, by + 127, g_source.capture_qualified ? kGreen : kAmber, 2);
    if (!small) {
      label("C6 BLE/WIFI: UNVERIFIED", bx + 20, by + 179, kMuted);
      label("RTL V4: NO 2.4 / 5.8 GHZ", bx + 20, by + 216, kMuted);
    }
  }
}
void activity() {
  if (compact()) {
    button(20, 576, (width() - 52) / 2, g_detail ? "CLOSE SOURCES" : "SOURCES");
    button(32 + (width() - 52) / 2, 576, (width() - 52) / 2,
           g_help ? "CLOSE HELP" : "HELP");
    return;
  }
  panel(20, 512, 802, 121);
  label("RECENT ACTIVITY", 42, 543, kCyan);
  const Message m = describe(g_source, g_now);
  char received[52]{};
  if (m.observed) {
    std::snprintf(received, sizeof(received), "VALID RID MESSAGES: %lu",
                  static_cast<unsigned long>(g_source.packets_received));
  }
  label(g_source.test_data ? "TEST DATA - NOT LIVE" :
        m.observed ? received : m.monitoring ? "NO RECENT REMOTE ID OBSERVED" :
        "NO OBSERVATIONS AVAILABLE",
        42, 589, g_source.test_data ? kAmber : m.observed ? kGreen : kMuted);
  button(840, 520, 195, g_detail ? "CLOSE" : "SOURCES");
  button(1047, 520, width() - 1067, g_help ? "CLOSE" : "HELP");
  on_bg("ADDITIONAL VIEWS IN DEVELOPMENT", 846, 604, kMuted, 2);
}
void redraw_panels() {
  headline();
  source_panel();
  activity();
}
}  // namespace

void install(const Hooks& hooks) { g_hooks = hooks; }
void open() {
  // Never start BLE, Wi-Fi monitor or RTL tuning on entry. P1/P2 acquisition
  // must be separately qualified before any live status can be published.
  g_source = {};
  g_detail = false;
  g_help = false;
  g_now = millis();
  g_active = true;
  screens::begin_transition(screens::Id::drone_watch, g_now);
  draw(true);
  screens::finish_transition();
}
void close() { g_active = false; g_headline = nullptr; }
bool active() { return g_active; }
void draw(bool structural) {
  if (!g_active || !screens::is_active(screens::Id::drone_watch)) return;
  if (!structural && !screens::owns(screens::Id::drone_watch)) return;
  if (structural) {
    M5.Display.fillScreen(kBg); // Only page entry and deliberate structural recovery.
    M5.Display.setFont(nullptr);
    header();
    footer();
    redraw_panels();
    return;
  }
  const Message m = describe(g_source, g_now);
  if (g_headline == nullptr || std::strcmp(g_headline, m.headline) != 0 ||
      g_count != g_source.packets_received) {
    headline();
    activity();
    screens::note_visible_update(screens::Id::drone_watch);
  }
}
void tick(uint32_t now_ms) {
  if (!g_active || !screens::owns(screens::Id::drone_watch)) return;
  g_now = now_ms;
  draw(false);
}
void publish_source(const SourceSnapshot& source) {
  // UI task only. Kept separate so the future verified receiver adapter can
  // publish actual status without giving radio/decoder tasks display access.
  g_source = source;
  if (!g_active || !screens::owns(screens::Id::drone_watch)) return;
  redraw_panels();
  screens::note_visible_update(screens::Id::drone_watch);
}
Action touch(int32_t x, int32_t y) {
  if (!g_active || !screens::owns(screens::Id::drone_watch)) return Action::none;
  if (compact() && hit(x, y, width() - 142, 18, 116, 54)) return Action::close;
  const int source_x = compact() ? 20 : 840;
  const int source_y = compact() ? 576 : 520;
  const int source_w = compact() ? (width() - 52) / 2 : 195;
  if (hit(x, y, source_x, source_y, source_w, 54)) {
    g_detail = !g_detail; g_help = false;
    source_panel(); activity();
    screens::note_visible_update(screens::Id::drone_watch);
  } else {
    const int help_x = compact() ? 32 + (width() - 52) / 2 : 1047;
    const int help_w = compact() ? (width() - 52) / 2 : width() - 1067;
    if (hit(x, y, help_x, source_y, help_w, 54)) {
      g_help = !g_help; g_detail = false;
      source_panel(); activity();
      screens::note_visible_update(screens::Id::drone_watch);
    }
  }
  return Action::none;
}
}  // namespace orcsdr::drone_watch
