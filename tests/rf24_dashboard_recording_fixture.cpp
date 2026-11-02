#include <M5Unified.h>

#include "../apps/orcsdr-tab5/ui/rf24_dashboard.cpp"

namespace orcsdr::audio_header {
bool set_viewport(HeaderRect) { return true; }
void draw_brand(const char*) {}
void draw_home_button() {}
void draw_battery(int32_t) {}
void draw_mute_button(bool) {}
void draw_visualizer_button(bool) {}
void draw_settings_button() {}
bool home_hit(int32_t, int32_t) { return false; }
bool mute_hit(int32_t, int32_t) { return false; }
bool visualizer_hit(int32_t, int32_t) { return false; }
bool settings_hit(int32_t, int32_t) { return false; }
}  // namespace orcsdr::audio_header

int main() {
  using namespace orcsdr::rf24;
  Snapshot snapshot{};
  snapshot.ready = true;
  snapshot.revision = 1;
  snapshot.access_point_count = 1;
  snapshot.total_access_point_count = 1;
  auto& ap = snapshot.access_points[0];
  std::memcpy(ap.ssid, "0123456789ABCDEFGHIJKLMNOPQRSTUV", 32);
  ap.ssid[32] = '\0';
  std::memcpy(ap.security, "WPA2", 5);
  std::memcpy(ap.phy, "802.11n", 8);
  ap.channel = 6;
  ap.rssi = -54;
  ap.secure = true;

  if (!set_viewport({72, 72, 424, 1088})) return 1;
  enter(snapshot);
  if (select_page(static_cast<uint8_t>(Page::devices), snapshot) == false) return 2;
  const int clear_count = lgfx::screen_clears;
  const int before = static_cast<int>(lgfx::paints.size());
  snapshot.revision = 2;
  snapshot.access_points[0].rssi = -61;
  update(snapshot);
  if (lgfx::screen_clears != clear_count) return 3;
  if (static_cast<int>(lgfx::paints.size()) <= before) return 4;
  bool full_ssid_seen = false;
  for (int i = before; i < static_cast<int>(lgfx::paints.size()); ++i)
    if (lgfx::paints[i].text == "0123456789ABCDEFGHIJKLMNOPQRSTUV") full_ssid_seen = true;
  if (!full_ssid_seen) return 5;
  // Long explanatory text must continue beyond the old 33-character scratch limit.
  for(auto viewport:{orcsdr::audio_header::HeaderRect{72,72,424,1088},orcsdr::audio_header::HeaderRect{72,72,1088,424}}) {
    if(!orcsdr::rf24::set_viewport(viewport)) return 6;
    lgfx::paints.clear();
    const char* paragraph="First complete sentence. Second complete sentence. Third complete sentence.";
    if(draw_wrapped(paragraph,viewport.x+8,viewport.y+200,viewport.w-16,2,0xffff,24)<1) return 7;
    std::string restored;
    for(const auto& paint:lgfx::paints) restored+=paint.text;
    auto compact=[](const std::string& text) {std::string out;for(char c:text)if(c!=' ')out+=c;return out;};
    if(compact(restored)!=compact(paragraph)) return 8;
    lgfx::paints.clear();
    draw_wrapped("Manage Wi-Fi in Connectivity.",viewport.x,viewport.y,384,3,0xffff,30);
    bool intact=false;for(const auto& paint:lgfx::paints) intact|=paint.text.find("Connectivity.")!=std::string::npos;
    if(!intact) return 9;
  }
  leave();
  return 0;
}
