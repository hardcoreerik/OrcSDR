#include "focus_nav.hpp"
#include "settings_app.hpp"
#include "settings_storage_layout.hpp"
#include "map_catalog_paging.hpp"

#include "clock_settings.hpp"
#include "dashboard_audio_control.hpp"
#include "text_editor.hpp"

#include <M5Unified.h>
#include <esp_attr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::settings {
namespace {

constexpr uint16_t kBg = 0x0841;
constexpr uint16_t kPanel = 0x1082;
constexpr uint16_t kBlue = 0x04ff;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kMuted = 0x9cf3;
constexpr int kHeaderH = 100;
constexpr int kRailW = 286;
constexpr int kRailY = 106;
constexpr int kRailRowH = 64;
constexpr uint8_t kSettingsMinTextSize = 2;
constexpr uint16_t kTimeouts[] = {0, 30, 60, 120, 300};
constexpr const char* kLabels[] = {
    "CONNECTIVITY", "FIRMWARE & UPDATES", "RECEIVER LOCATION", "DATA & MAPS", "DISPLAY & AUDIO",
    "RADIO DEFAULTS", "STORAGE", "ACCESSORIES & COMPANION", "SYSTEM"};

static_assert(static_cast<uint8_t>(Section::count) == std::size(kLabels));

enum class EditField : uint8_t { none, latitude, longitude };
enum class WifiEdit : uint8_t { none, ssid, password };

EXT_RAM_BSS_ATTR State g_state;
Section g_section = Section::connectivity;
EditField g_edit = EditField::none;
bool g_active = false;
bool g_rom_action_pending = false;
bool g_latitude_set = false;
bool g_longitude_set = false;
char g_entry[20]{};
WifiEdit g_wifi_edit = WifiEdit::none;
bool g_wifi_shift = false;
bool g_wifi_symbols = false;
bool g_wifi_show_password = false;
char g_wifi_edit_ssid[33]{};
char g_wifi_edit_password[64]{};
char g_wifi_request_ssid[33]{};
char g_wifi_request_password[64]{};
bool g_wifi_request_pending = false;
bool g_location_edit = false;
// The clock editor (Settings > System > SET CLOCK): local date and time fields plus the UTC offset.
bool g_clock_edit = false;
bool g_clock_dirty = false;                 // a date or time field was touched, so re-derive nothing from the RTC
clock_settings::LocalTime g_clock_local;
int32_t g_clock_offset = 0;
bool g_ntp_tapped = false;                  // SYNC NTP was tapped and the new state has not arrived yet
bool g_location_request_pending = false;
char g_location_query[64]{};
map_catalog_paging::Cursor g_catalog_cursor;
SettingsLayout g_layout = make_settings_layout({0,0,1280,720});
std::size_t g_content_pages[static_cast<std::size_t>(Section::count)]{};

std::size_t display_row_count() { return g_state.orientation_profile ? 6 : 5; }
std::size_t& page_index() { return g_content_pages[static_cast<std::size_t>(g_section)]; }
std::size_t page_count() {
  if (!g_layout.legacy && g_section==Section::location_adsb) return 3;
  if (!g_layout.legacy && g_section==Section::data_maps) return 1+map_catalog_paging::entry_count(g_state.catalog_packs);
  if (!g_layout.legacy && g_section==Section::connectivity) return g_state.wifi_hosted_update_required ? 1 : 3;
  if (!g_layout.legacy && g_section==Section::firmware_updates) return g_state.wifi_c6_rom_recovery?1:2;
#if ORCSDR_ORCDIAL
  if (!g_layout.legacy && g_section==Section::companion && g_layout.content.h<320) return 2;
#endif
  if (!g_layout.legacy && g_section==Section::system && g_layout.content.h<320) return 2;
  return g_section == Section::display_audio ? settings_page_count(display_row_count(),g_layout) : 1;
}

bool hit(int x, int y, Rect r) {
  return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h;
}

bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color, uint8_t size = 2,
          textdatum_t datum = middle_left) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(std::max(size, kSettingsMinTextSize));
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void button(const char* label, int x, int y, int w, int h, uint16_t fill) {
  orcsdr::focus_nav::note(x, y, w, h);
  M5.Display.fillRoundRect(x, y, w, h, 8, fill);
  M5.Display.drawRoundRect(x, y, w, h, 8, TFT_LIGHTGREY);
  M5.Display.setTextDatum(middle_center);
  const uint8_t size = label && std::strlen(label) * 18 <= static_cast<size_t>(w - (!g_layout.legacy && w<64?8:16)) ? 3 : 2;
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(TFT_WHITE, fill);
  M5.Display.drawString(label, x + w / 2, y + h / 2);
}

void button(const char* label, Rect r, uint16_t fill, bool enabled=true) {
  if (!enabled) {
    M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,8,TFT_DARKGREY);
    text(label,r.x+r.w/2,r.y+r.h/2,kMuted,!g_layout.legacy && strlen(label)*18<=size_t(r.w-8)?3:2,middle_center);
    return;
  }
  button(label,r.x,r.y,r.w,r.h,fill);
}

// Wrap within an explicitly reserved region; never reduce below readable size.
std::size_t wrapped_count(const char* value,int capacity) {
  const auto logical=std::strcspn(value,"\n");
  std::size_t n=std::min<std::size_t>(logical,std::min(capacity,127));
  if(n<logical && value[n]!=' ') {
    auto split=n;while(split && value[split]!=' ') --split;
    if(split) n=split;
  }
  return n;
}
void wrapped_advance(const char*& value,std::size_t n) {
  value+=n;while(*value==' ') ++value;if(*value=='\n') ++value;
}
int wrapped_lines(const char* value,int capacity) {
  int lines=0;
  while(value && *value) {const auto n=wrapped_count(value,capacity);wrapped_advance(value,n);++lines;}
  return lines;
}
int wrapped_text(const char* value, Rect r, uint16_t color) {
  if(!value || !*value || r.w<=0 || r.h<=0) return 0;
  M5.Display.setTextSize(3);
  int capacity=std::max(1,r.w/std::max(1,int(M5.Display.textWidth("M"))));
  const int large_lines=wrapped_lines(value,capacity);
  int size=3,pitch=large_lines==1 && r.h<32?24:32;
  if(large_lines*pitch>r.h) {
    size=2;pitch=24;M5.Display.setTextSize(2);
    capacity=std::max(1,r.w/std::max(1,int(M5.Display.textWidth("M"))));
  }
  const int needed=wrapped_lines(value,capacity)*pitch;
  if(needed>r.h) std::printf("RTL_SETTINGS_TEXT_OVERFLOW section=%u rect=%d,%d,%d,%d needed_h=%d\n",
    unsigned(g_section),r.x,r.y,r.w,r.h,needed);
  int y=r.y+pitch/2;
  while(*value && y+pitch/2<=r.y+r.h) {
    const auto n=wrapped_count(value,capacity);char line[128];
    std::memcpy(line,value,n);line[n]='\0';
    if(n) text(line,r.x,y,color,size);
    wrapped_advance(value,n);y+=pitch;
  }
  return y-r.y-pitch/2;
}

void value_row(const char* label, const char* value, int y,
               uint16_t color = TFT_WHITE) {
  text(label, 330, y, kMuted, 2);
  text(value, 1218, y, color, 2, middle_right);
  M5.Display.drawFastHLine(330, y + 31, 888, 0x2945);
}

void draw_header() {
  if (!g_layout.legacy) {
    const auto& h=audio_header::layout();
    M5.Display.fillRect(g_layout.header.x,g_layout.header.y,g_layout.header.w,g_layout.header.h,TFT_BLACK);
    audio_header::draw_brand("SETTINGS");audio_header::draw_battery(g_state.battery_level);
    audio_header::draw_mute_button(g_state.sound_default);audio_header::draw_settings_button();
    button("X",g_layout.close,TFT_MAROON,!g_state.wifi_c6_rom_busy);
    wrapped_text(g_state.wifi_connected ? g_state.wifi_ssid : "Wi-Fi offline",h.status,kMuted);
    return;
  }
  M5.Display.fillRect(0, 0, 1280, kHeaderH, TFT_BLACK);
  audio_header::draw_brand("SETTINGS");
  char status[96];
  if (g_state.wifi_connected)
    snprintf(status, sizeof(status), "%s  %s", g_state.wifi_ssid, g_state.wifi_ip);
  else
    strlcpy(status, !g_state.wifi_power_enabled ? "Wi-Fi powered off"
                    : g_state.wifi_connecting ? "Wi-Fi connecting"
                    : g_state.wifi_scanning ? "Wi-Fi scanning" : "Wi-Fi offline",
            sizeof(status));
  text(status, 690, 36, g_state.wifi_connected ? kGreen : kMuted, 2, middle_right);
  button("CLOSE", 720, 13, 126, 46, TFT_MAROON);
  audio_header::draw_battery(g_state.battery_level);
  audio_header::draw_mute_button(g_state.sound_default);
  audio_header::draw_visualizer_button(false);
  audio_header::draw_settings_button();
}

void draw_rail() {
  if (!g_layout.legacy) {
    const auto r=g_layout.navigation;
    M5.Display.fillRect(r.x,r.y,r.w,r.h,TFT_BLACK);
    button("<",g_layout.previous_section,TFT_NAVY);
    button(">",g_layout.next_section,TFT_NAVY);
    wrapped_text(kLabels[static_cast<uint8_t>(g_section)],g_layout.section_title,kBlue);
    return;
  }
  M5.Display.fillRect(0, kHeaderH, kRailW, 720 - kHeaderH, TFT_BLACK);
  for (uint8_t i = 0; i < static_cast<uint8_t>(Section::count); ++i) {
    const int y = kRailY + i * kRailRowH;
    const bool selected = static_cast<uint8_t>(g_section) == i;
    const uint16_t fill = selected ? TFT_DARKCYAN : 0x1082;
    M5.Display.fillRoundRect(12, y, kRailW - 24, kRailRowH - 8, 7, fill);
    M5.Display.setTextDatum(middle_left);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(selected ? TFT_WHITE : kMuted, fill);
    M5.Display.drawString(kLabels[i], 30, y + (kRailRowH - 8) / 2);
  }
}

void draw_display_footer();
void wifi_button(const char* label, Rect r, uint16_t color, bool enabled=true) {
  char visible[100]{};
  strlcpy(visible,label,sizeof(visible));
  M5.Display.setTextSize(2);
  size_t n=strlen(visible);
  while(n && M5.Display.textWidth(visible)>r.w-12) visible[--n]='\0';
  if(n<strlen(label) && n>=3) {
    while(n>=3) {
      memcpy(visible+n-3,"...",3);visible[n]='\0';
      if(M5.Display.textWidth(visible)<=r.w-12) break;
      --n;
    }
  }
  button(visible,r,color,enabled);
}
void draw_wifi_status() {
  const auto l=make_connectivity_layout(g_layout.content);
  M5.Display.fillRect(l.status.x,l.status.y,l.status.w,l.status.h,kBg);
  M5.Display.fillRect(l.message.x,l.message.y,l.message.w,l.message.h,kBg);
  char value[100];
  snprintf(value,sizeof(value),"%s  %.32s  %.15s",
    !g_state.wifi_power_enabled?"POWER OFF":g_state.wifi_connected?"CONNECTED":g_state.wifi_connecting?"CONNECTING":"OFFLINE",
    g_state.wifi_ssid,g_state.wifi_connected?g_state.wifi_ip:"");
  wrapped_text(value,l.status,g_state.wifi_connected?kGreen:TFT_ORANGE);
  wrapped_text(g_state.wifi_message,l.message,kMuted);
}
void draw_wifi_primary() {
  const auto l=make_connectivity_layout(g_layout.content);
  wifi_button(g_state.wifi_scanning?"SCANNING":"SCAN",l.scan,TFT_DARKCYAN,g_state.wifi_power_enabled&&!g_state.wifi_scanning);
  wifi_button("ADD HIDDEN",l.hidden,TFT_NAVY,g_state.wifi_power_enabled);
  wifi_button(g_state.wifi_power_enabled?"POWER OFF":"POWER ON",l.power,TFT_MAROON);
  wifi_button(g_state.wifi_start_at_boot?"AT BOOT: ON":"AT BOOT: OFF",l.boot,TFT_DARKGREEN);
  wifi_button(g_state.wifi_external_antenna?"ANT: EXTERNAL":"ANT: INTERNAL",l.antenna,TFT_NAVY);
}
void draw_wifi_saved(int i) {
  const auto l=make_connectivity_layout(g_layout.content);const auto r=l.saved[i];
  for(auto rect:{r.connect,r.up,r.down,r.forget}) M5.Display.fillRect(rect.x,rect.y,rect.w,rect.h,kBg);
  if(i>=std::min<int>(4,g_state.saved_network_count)) return;
  button("",r.connect,g_state.profiles[i].connected?TFT_DARKGREEN:TFT_DARKCYAN,g_state.wifi_power_enabled);
  wrapped_text(g_state.profiles[i].ssid,{r.connect.x+8,r.connect.y+2,r.connect.w-16,24},TFT_WHITE);
  char label[48];snprintf(label,sizeof(label),"%d - %s",i+1,g_state.profiles[i].connected?"CONNECTED":"SAVED");
  text(label,r.connect.x+8,r.connect.y+40,kMuted,2);
  wifi_button("UP",r.up,TFT_NAVY,i>0&&g_state.wifi_power_enabled);
  wifi_button("DN",r.down,TFT_NAVY,i+1<g_state.saved_network_count&&g_state.wifi_power_enabled);
  wifi_button("FORGET",r.forget,TFT_MAROON,g_state.wifi_power_enabled);
}
void draw_wifi_network(int i) {
  const auto r=make_connectivity_layout(g_layout.content).networks[i];
  M5.Display.fillRect(r.x,r.y,r.w,r.h,kBg);
  if(i>=std::min<int>(6,g_state.network_count)) return;
  button("",r,TFT_DARKCYAN,g_state.wifi_power_enabled);
  wrapped_text(g_state.networks[i].ssid,{r.x+8,r.y+4,r.w-16,24},TFT_WHITE);
  char detail[64];snprintf(detail,sizeof(detail),"%d dBm / %s%s",g_state.networks[i].rssi,
    g_state.networks[i].secure?"LOCK":"OPEN",g_state.networks[i].saved?" / SAVED":"");
  wrapped_text(detail,{r.x+8,r.y+34,r.w-16,24},kMuted);

}
void draw_compact_connectivity() {
  const auto l=make_connectivity_layout(g_layout.content);
  page_index()=std::min(page_index(),page_count()-1);
  if(g_state.wifi_hosted_update_required) {
    wrapped_text("C6 NEEDS ATTENTION",l.title,TFT_ORANGE);
    char note[240];snprintf(note,sizeof(note),"Radio reception remains available. P4: %s; C6: %s. Open Firmware & Updates. An unreachable C6 needs recovery.",g_state.wifi_hosted_host_version,g_state.wifi_hosted_c6_version);
    wrapped_text(note,l.notice,kMuted);draw_display_footer();return;
  }
  const auto page=page_index();
  wrapped_text(page==0?"WI-FI":page==1?"SAVED NETWORKS / PRIORITY":"AVAILABLE NETWORKS",l.title,kBlue);
  if(page==0) {draw_wifi_status();draw_wifi_primary();}
  else if(page==1) {
    for(int i=0;i<4;++i) draw_wifi_saved(i);
    if(!g_state.saved_network_count) wrapped_text("NO SAVED NETWORKS",l.notice,kMuted);
  } else {
    for(int i=0;i<6;++i) draw_wifi_network(i);
    if(!g_state.network_count) wrapped_text(g_state.wifi_scanning?"SCANNING...":"NO NETWORKS FOUND",l.notice,kMuted);
  }
  draw_display_footer();
}

void draw_connectivity() {
  if(!g_layout.legacy) {draw_compact_connectivity();return;}
  text("CONNECTIVITY", 330, 115, kBlue, 3);
  if (g_state.wifi_hosted_update_required) {
    M5.Display.fillRoundRect(330, 145, 888, 448, 12, 0x3104);
    M5.Display.drawRoundRect(330, 145, 888, 448, 12, TFT_ORANGE);
    text("WIRELESS COPROCESSOR NEEDS ATTENTION", 370, 195, TFT_ORANGE, 3);
    text("OrcSDR radio reception remains available.", 370, 242, TFT_WHITE, 2);
    text("Wi-Fi could not start with this C6 firmware. Tested: 2.12.6 and 3.0.6.", 370, 278, kMuted, 2);
    char versions[80];
    snprintf(versions, sizeof(versions), "P4: %s     C6: %s",
             g_state.wifi_hosted_host_version, g_state.wifi_hosted_c6_version);
    text(versions, 370, 333, TFT_WHITE, 2);
    text("Open Firmware & Updates to update the reachable C6.", 370, 395, kBlue, 2);
    text("SDR reception remains available while Wi-Fi is unavailable.", 370, 442, kMuted, 2);
    text("An unreachable C6 requires the recovery procedure.", 370, 486, kMuted, 2);
    return;
  }
  char value[96];
  const char* status = !g_state.wifi_power_enabled ? "POWERED OFF"
                       : g_state.wifi_connected ? "CONNECTED"
                       : g_state.wifi_connecting ? "CONNECTING"
                                                : "OFFLINE";
  const uint16_t status_color = !g_state.wifi_power_enabled ? TFT_ORANGE
                                : g_state.wifi_connected ? kGreen
                                : g_state.wifi_connecting ? TFT_YELLOW
                                                         : TFT_ORANGE;
  snprintf(value, sizeof(value), "%s  %s  %s", status,
           g_state.wifi_ssid[0] ? g_state.wifi_ssid : "--",
           g_state.wifi_connected ? g_state.wifi_ip : "");
  text(value, 330, 150, status_color,
       std::strlen(value) * 18 <= 888 ? 3 : 2);
  if (g_state.wifi_message[0]) text(g_state.wifi_message, 330, 178, kMuted, 2);
  button(g_state.wifi_scanning ? "SCANNING..." : "SCAN", 330, 198, 280, 48,
         g_state.wifi_scanning ? TFT_DARKGREY : TFT_DARKCYAN);
  button("ADD HIDDEN", 634, 198, 280, 48, TFT_NAVY);
  button(g_state.wifi_power_enabled ? "POWER OFF" : "POWER ON", 938, 198, 280, 48,
         g_state.wifi_power_enabled ? TFT_MAROON : TFT_DARKGREEN);
  text("CONNECT ON BOOT", 330, 286, kMuted, 3);
  button(g_state.wifi_start_at_boot ? "ON" : "OFF", 930, 262, 288, 48,
         g_state.wifi_start_at_boot ? TFT_DARKGREEN : TFT_DARKGREY);
  text("WI-FI ANTENNA", 330, 350, kMuted, 3);
  button(g_state.wifi_external_antenna ? "EXTERNAL (MMCX)" : "INTERNAL",
         930, 326, 288, 48,
         g_state.wifi_external_antenna ? TFT_DARKGREEN : TFT_NAVY);

  text("SAVED NETWORKS (PRIORITY ORDER)", 330, 390, kBlue, 2);
  for (uint8_t i = 0; i < g_state.saved_network_count && i < 4; ++i) {
    const int y = 410 + i * 44;
    snprintf(value, sizeof(value), "%u  %.24s%s", i + 1, g_state.profiles[i].ssid,
             g_state.profiles[i].connected ? "  CONNECTED" : "");
    text(value, 340, y + 20, g_state.profiles[i].connected ? kGreen : TFT_WHITE, 2);
    button("CONNECT", 674, y, 144, 36, TFT_DARKCYAN);
    button("UP", 830, y, 88, 36, i ? TFT_NAVY : TFT_DARKGREY);
    button("DOWN", 930, y, 106, 36,
           i + 1 < g_state.saved_network_count ? TFT_NAVY : TFT_DARKGREY);
    button("FORGET", 1048, y, 170, 36, TFT_MAROON);
  }
  if (g_state.saved_network_count == 0)
    text("NO SAVED NETWORKS", 340, 414, kMuted, 2);

  text("AVAILABLE NETWORKS", 330, 602, kBlue, 2);
  const uint8_t shown = std::min<uint8_t>(g_state.network_count, 6);
  for (uint8_t i = 0; i < shown; ++i) {
    const int x = 330 + (i % 2) * 428;
    const int y = 624 + (i / 2) * 30;
    snprintf(value, sizeof(value), "%.18s  %d%s%s", g_state.networks[i].ssid,
             g_state.networks[i].rssi, g_state.networks[i].secure ? "  LOCK" : "  OPEN",
             g_state.networks[i].saved ? "  SAVED" : "");
    button(value, x, y, 418, 28,
           g_state.networks[i].saved ? 0x2945 : TFT_DARKCYAN);
  }
}

void draw_display_footer();
bool rom_firmware_current(const State& s) {
  return s.wifi_c6_rom_recovery && !s.wifi_c6_rom_busy && s.wifi_c6_image_embedded &&
    s.wifi_hosted_transport_ready && s.wifi_c6_rom_target_version[0] &&
    strcmp(s.wifi_hosted_c6_version,s.wifi_c6_rom_target_version)==0;
}
void draw_rom_status() {
  const Rect r{g_layout.content.x+8,g_layout.content.y+40,g_layout.content.w-16,48};
  M5.Display.fillRect(r.x,r.y,r.w,r.h,kBg);
  char status[128];
  char detail[48];
  if(g_state.wifi_c6_rom_result) snprintf(detail,sizeof(detail),"Error %d",g_state.wifi_c6_rom_result);
  else if(g_state.wifi_c6_rom_busy) snprintf(detail,sizeof(detail),"Progress: %u%%",unsigned(g_state.wifi_c6_update_percent));
  else if(g_state.wifi_c6_rom_phase==c6_recovery_ui::Phase::complete) snprintf(detail,sizeof(detail),"Installed and verified");
  else if(rom_firmware_current(g_state)) snprintf(detail,sizeof(detail),"No update needed");
  else snprintf(detail,sizeof(detail),"%s",g_state.wifi_c6_image_embedded?"Embedded firmware ready":"Image not included");
  snprintf(status,sizeof(status),"C6: %.15s / image %s\n%s",g_state.wifi_hosted_c6_version,
    g_state.wifi_c6_image_embedded?"3.0.6":"unavailable",detail);
  wrapped_text(status,r,TFT_WHITE);
}
void draw_rom_update() {
  page_index()=0;
  const auto c=g_layout.content;
  M5.Display.fillRect(c.x,c.y,c.w,c.h,kBg);
  const auto v=c6_recovery_ui::view(g_state.wifi_c6_rom_phase,g_state.wifi_c6_rom_busy,g_state.wifi_c6_image_embedded,rom_firmware_current(g_state));
  wrapped_text(v.title,{c.x+8,c.y+8,c.w-16,24},kBlue);
  draw_rom_status();
  wrapped_text(v.instructions,{c.x+8,c.y+96,c.w-16,c.h-156},TFT_WHITE);
  if(v.button[0]) wifi_button(v.button,g_layout.firmware_action(v.cancel),TFT_DARKGREEN,v.request!=c6_recovery_ui::Request::none);
  if(v.cancel) wifi_button("CANCEL",g_layout.firmware_cancel(),TFT_MAROON);
}

void draw_compact_firmware_status() {
  const auto r=g_layout.firmware_status();
  M5.Display.fillRect(r.x,r.y,r.w,r.h,kBg);
  char status[112];
  snprintf(status,sizeof(status),"%s  %u%%  %s",g_state.wifi_c6_update_state,
           unsigned(g_state.wifi_c6_update_percent),g_state.wifi_c6_update_stage);
  wrapped_text(status,{r.x,r.y,r.w,64},TFT_WHITE);
  if (strcmp(g_state.wifi_c6_update_state,"updating")==0) {
    const int y=r.y+72;
    M5.Display.drawRect(r.x,y,r.w,24,kMuted);
    const int percent=std::clamp<int>(g_state.wifi_c6_update_percent,0,100);
    M5.Display.fillRect(r.x+2,y+2,(r.w-4)*percent/100,20,kBlue);
  }
}
void draw_firmware_updates() {
  if(g_state.wifi_c6_rom_recovery) {draw_rom_update();return;}
  if (!g_layout.legacy) {
    page_index()=std::min<std::size_t>(page_index(),1);
    if (page_index()==0) {
      char versions[160];
      snprintf(versions,sizeof(versions),"P4 host: %s  C6: %s\nUpdate image: %s",
               g_state.wifi_hosted_host_version,g_state.wifi_hosted_c6_version,
               g_state.wifi_c6_image_embedded?"included":"not included");
      wrapped_text(versions,{g_layout.content.x+8,g_layout.content.y+8,g_layout.content.w-16,80},TFT_WHITE);
      draw_compact_firmware_status();
    } else {
      const bool ready=strcmp(g_state.wifi_c6_update_state,"ready")==0;
      const bool optional=strcmp(g_state.wifi_c6_update_state,"optional")==0;
      const bool current=strcmp(g_state.wifi_c6_update_state,"current")==0;
      const char* help=ready?"Install the embedded C6 image over the internal link. Keep OrcSDR powered on. OrcSDR restarts afterward.":
          optional?"Updating is optional. M5 Launcher Wi-Fi and OTA require a 2.x C6. Update only if you do not use Launcher.":
          current?"C6 firmware is supported. The embedded image can add OrcDial wireless support.":
          "C6 update is unavailable. Check the status page. If transport is unreachable, use the documented recovery procedure.";
      wrapped_text(help,g_layout.firmware_help(),TFT_WHITE);
      button(current?"INSTALL ORCDIAL":"UPDATE C6",g_layout.firmware_action(),TFT_DARKGREEN,
             g_state.wifi_c6_image_embedded && (ready||optional||current));
    }
    draw_display_footer();
    return;
  }
  text("FIRMWARE & UPDATES", 330, 115, kBlue, 3);
  char versions[96];
  snprintf(versions, sizeof(versions), "P4 HOST: %s", g_state.wifi_hosted_host_version);
  value_row("HOSTED HOST", versions, 165);
  snprintf(versions, sizeof(versions), "C6: %s   TESTED: 2.12.6, 3.0.6", g_state.wifi_hosted_c6_version);
  value_row("WIRELESS COPROCESSOR", versions, 220,
            (strcmp(g_state.wifi_c6_update_state, "current") == 0 ||
             strcmp(g_state.wifi_c6_update_state, "optional") == 0) ? kGreen : TFT_ORANGE);
  value_row("UPDATE IMAGE", g_state.wifi_c6_image_embedded ? "EMBEDDED IN THIS ORCSDR BUILD" : "NOT INCLUDED", 275,
            g_state.wifi_c6_image_embedded ? kGreen : TFT_ORANGE);
  char status[96];
  snprintf(status, sizeof(status), "%s  %u%%  %s", g_state.wifi_c6_update_state,
           static_cast<unsigned>(g_state.wifi_c6_update_percent), g_state.wifi_c6_update_stage);
  value_row("STATUS", status, 330,
            strcmp(g_state.wifi_c6_update_state, "failed") == 0 ? TFT_ORANGE : TFT_WHITE);
  if (strcmp(g_state.wifi_c6_update_state, "updating") == 0) {
    M5.Display.drawRect(330, 385, 760, 32, TFT_LIGHTGREY);
    M5.Display.fillRect(332, 387, 756 * g_state.wifi_c6_update_percent / 100, 28, kBlue);
    text("Keep OrcSDR powered on. SDR and audio remain active.", 330, 455, kMuted, 2);
  } else if (strcmp(g_state.wifi_c6_update_state, "ready") == 0) {
    text("The C6 is reachable but does not match this OrcSDR build.", 330, 405, TFT_WHITE, 2);
    button("UPDATE C6 TO 3.0.6", 330, 445, 360, 58, TFT_DARKGREEN);
    text("This sends the embedded image over the internal SDIO link and restarts OrcSDR.",
         330, 545, kMuted, 1);
  } else if (strcmp(g_state.wifi_c6_update_state, "optional") == 0) {
    text("No update is needed. Updating to 3.0.6 is optional.", 330, 385, kGreen, 2);
    text("M5 Launcher's Wi-Fi and OTA need a 2.x C6. Update only if you do not use Launcher.", 330, 412, TFT_ORANGE, 2);
    button("UPDATE C6 TO 3.0.6 (OPTIONAL)", 330, 445, 440, 58, TFT_DARKGREEN);
    text("This sends the embedded image over the internal SDIO link and restarts OrcSDR.",
         330, 545, kMuted, 1);
  } else if (strcmp(g_state.wifi_c6_update_state, "current") == 0) {
    text("C6 firmware is supported. OrcDial wireless support can be installed here.", 330, 410, kGreen, 2);
    if (g_state.wifi_c6_image_embedded)
      button("INSTALL ORCDIAL SUPPORT", 330, 445, 390, 58, TFT_DARKGREEN);
  } else if (strcmp(g_state.wifi_c6_update_state, "unreachable") == 0) {
    text("The C6 cannot establish Hosted transport.", 330, 405, TFT_ORANGE, 2);
    text("Use the documented recovery procedure; do not retry automatically.", 330, 450, kMuted, 2);
  } else {
    text("Update is unavailable in this build or after a failed attempt.", 330, 405, TFT_ORANGE, 2);
    text("Restart OrcSDR before retrying. Use recovery only if the C6 is unreachable.", 330, 450, kMuted, 2);
  }
}

void set_wifi_editor_viewport();
void draw_compact_location() {
  const SettingsFormLayout l{g_layout.content};char value[320];
  wrapped_text("RECEIVER LOCATION",l.line(0),kBlue);
  if(page_index()==0) {
    wrapped_text(g_state.location_configured?(g_state.location_label[0]?g_state.location_label:"SAVED LOCATION"):"NOT SET",l.location_name(),kMuted);
    snprintf(value,sizeof(value),"LAT: %.7f",g_state.latitude_e7/10000000.0);
    wifi_button(value,l.coordinate(false),TFT_NAVY);
    snprintf(value,sizeof(value),"LON: %.7f",g_state.longitude_e7/10000000.0);
    wifi_button(value,l.coordinate(true),TFT_NAVY);
    wifi_button("CHOOSE ON MAP",l.location_map(),TFT_DARKGREEN);
  } else if(page_index()==1) {
    snprintf(value,sizeof(value),"MAP PACK: %s",g_state.map_pack[0]?g_state.map_pack:"NOT INSTALLED");
    const int used=wrapped_text(value,l.line(28,g_layout.content.w<800?240:112),kMuted);
    const int y=28+used+8;
    wrapped_text("RF GAIN: AUTO (READ ONLY)",l.line(y),kMuted);
    wrapped_text("Location lookup: next page.",l.line(y+32),kMuted);
    wrapped_text("(c) OpenStreetMap contributors",l.line(y+64,g_layout.content.w<800?64:32),kMuted);
  } else {
    wifi_button(g_state.ip_location_busy?"LOOKING UP":"ZIP / ADDRESS",l.half(28),TFT_DARKCYAN,!g_state.ip_location_busy);
    wifi_button("IP AREA",l.half(28,true),TFT_NAVY,!g_state.ip_location_busy);
    wrapped_text(g_state.ip_location_message,l.lookup_message(),kMuted);
    if(g_state.ip_location_ready) wifi_button("USE RESULT",l.lookup_result(),TFT_DARKGREEN);
    wrapped_text("(c) OpenStreetMap contributors",l.lookup_credit(),kMuted);
  }
  draw_display_footer();
}

int compact_pack_index() {
  if(!page_index()) return -1;
  const auto rank=page_index()-1;
  return map_catalog_paging::index_for_row(g_state.catalog_packs,rank/5,rank%5);
}
void draw_compact_catalog_status() {
  const SettingsFormLayout l{g_layout.content};
  M5.Display.fillRect(l.catalog_status().x,l.catalog_status().y,l.catalog_status().w,l.catalog_status().h,kBg);
  char value[160];snprintf(value,sizeof(value),"%s %.19s  %u%%\n%.80s",g_state.catalog_ready?"SIGNED CATALOG":"NO CATALOG",g_state.catalog_date,unsigned(g_state.catalog_progress_percent),g_state.catalog_message);
  wrapped_text(value,l.catalog_status(),kMuted);
  wifi_button(g_state.catalog_busy?"WORKING...":"CHECK FOR UPDATES",l.catalog_check(),TFT_DARKCYAN,!g_state.catalog_busy);
}
void draw_compact_data_maps() {
  page_index()=std::min(page_index(),page_count()-1);
  const SettingsFormLayout l{g_layout.content};const int index=compact_pack_index();
  if(index<0) {
    wrapped_text("DATA & MAPS",l.line(0),kBlue);draw_compact_catalog_status();
    wifi_button("LOCAL MAPS",l.catalog_local(),TFT_NAVY);
    wrapped_text("Downloads pause reception.",l.catalog_notice(),kMuted);
  } else {
    const auto& p=g_state.catalog_packs[index];char detail[160];
    wrapped_text(p.title[0]?p.title:p.id,l.line(0,48),kBlue);
    snprintf(detail,sizeof(detail),"v%s / %s\n%.1f + %.1f MB",p.version,p.source_date,p.runtime_bytes/1048576.0,p.archive_bytes/1048576.0);
    wrapped_text(detail,l.line(52,48),TFT_WHITE);wrapped_text(p.status,l.line(108),p.installed?kGreen:kMuted);
    wifi_button(p.installed?(p.update_available?"UPDATE":"REINSTALL"):"INSTALL",l.half(144),TFT_DARKCYAN,g_state.catalog_ready&&!g_state.catalog_busy);
    wifi_button(g_catalog_cursor.armed==index?"CONFIRM":"REMOVE",l.half(144,true),TFT_MAROON,p.installed&&!g_state.catalog_busy);
  }
  draw_display_footer();
}

void draw_location() {
  if(!g_layout.legacy) {draw_compact_location();return;}
  text("RECEIVER LOCATION", 330, 115, kBlue, 3);
  char value[48];
  // A name is optional. An unnamed saved coordinate is still a configured
  // location; never substitute the independently selected map-pack identity.
  value_row("LOCATION", !g_state.location_configured ? "NOT SET" :
            g_state.location_label[0] ? g_state.location_label : "SAVED LOCATION",
            165);
  snprintf(value, sizeof(value), g_state.location_configured ? "%.7f" : "NOT SET",
           g_state.latitude_e7 / 10000000.0);
  text("RECEIVER LATITUDE", 330, 230, kMuted, 2);
  button(value, 820, 204, 398, 54, TFT_NAVY);
  snprintf(value, sizeof(value), g_state.location_configured ? "%.7f" : "NOT SET",
           g_state.longitude_e7 / 10000000.0);
  text("RECEIVER LONGITUDE", 330, 300, kMuted, 2);
  button(value, 820, 274, 398, 54, TFT_NAVY);
  button("CHOOSE LOCATION ON MAP",330,350,888,68,TFT_DARKGREEN);
  // Stored identities can be full manifest paths. Keep the 24 px display value
  // inside the content column without altering the selected pack identity.
  char pack_summary[43];
  const char* pack=g_state.map_pack[0] ? g_state.map_pack : "NOT INSTALLED";
  if(std::strlen(pack)>42) {
    size_t prefix=39;
    while(prefix && (static_cast<unsigned char>(pack[prefix])&0xc0)==0x80)--prefix;
    std::memcpy(pack_summary,pack,prefix);
    std::memcpy(pack_summary+prefix,"...",4);
  } else std::snprintf(pack_summary,sizeof(pack_summary),"%s",pack);
  value_row("MAP PACK", pack_summary, 445);
  value_row("RF GAIN", "AUTO (READ ONLY)", 495, kMuted);
  text("INTERNET LOCATION LOOKUP", 330, 550, kBlue, 2);
  button(g_state.ip_location_busy ? "LOOKING UP..." : "ZIP / ADDRESS", 330, 575, 250, 46,
         g_state.ip_location_busy ? TFT_DARKGREY : TFT_DARKCYAN);
  button("IP AREA", 600, 575, 180, 46,
         g_state.ip_location_busy ? TFT_DARKGREY : TFT_NAVY);
  if (g_state.ip_location_ready) button("USE RESULT", 800, 575, 240, 46, TFT_DARKGREEN);
  if (g_state.ip_location_message[0]) text(g_state.ip_location_message, 330, 645, kMuted, 2, middle_left);
  text("Address search data (c) OpenStreetMap contributors", 330, 682, kMuted, 1);
}

void draw_data_maps() {
  if(!g_layout.legacy) {draw_compact_data_maps();return;}
  text("DATA & MAPS", 330, 115, kBlue, 3);
  char catalog[112], catalog_action[32];
  snprintf(catalog, sizeof(catalog), "%s%s", g_state.catalog_ready ? "SIGNED CATALOG " : "NO CATALOG ",
           g_state.catalog_date[0] ? g_state.catalog_date : "CHECK MANUALLY");
  text(catalog, 330, 153, g_state.catalog_ready ? kGreen : kMuted, 2);
  snprintf(catalog_action, sizeof(catalog_action), g_state.catalog_busy ? "WORKING %u%%" : "CHECK FOR UPDATES",
           static_cast<unsigned>(g_state.catalog_progress_percent));
  button(catalog_action, 940, 126, 278, 48,
         g_state.catalog_busy ? TFT_DARKGREY : TFT_DARKCYAN);
  if (g_state.catalog_message[0]) text(g_state.catalog_message, 330, 180, kMuted, 2);

  // The signed catalog may contain more than the five historical built-ins.
  // Page by nonempty pack IDs; touch actions retain the catalog's original
  // slot index (including gaps), never a page-relative row index.
  const size_t pages = orcsdr::map_catalog_paging::page_count(g_state.catalog_packs);
  if (g_catalog_cursor.page >= pages) g_catalog_cursor.page = static_cast<uint8_t>(pages - 1);
  char page_title[32];
  snprintf(page_title, sizeof(page_title), "PAGE %u / %u",
           static_cast<unsigned>(g_catalog_cursor.page + 1), static_cast<unsigned>(pages));
  text(page_title, 650, 115, kMuted, 2);
  button("PREV", 650, 137, 120, 42, g_catalog_cursor.page > 0 ? TFT_NAVY : TFT_DARKGREY);
  button("NEXT", 787, 137, 120, 42,
         g_catalog_cursor.page + 1 < pages ? TFT_NAVY : TFT_DARKGREY);

  const size_t pack_count = orcsdr::map_catalog_paging::entry_count(g_state.catalog_packs);
  if (pack_count == 0) text("NO PACK ENTRIES. CHECK CATALOG.", 350, 290, kMuted, 2);
  for (size_t row = 0; row < orcsdr::map_catalog_paging::kRowsPerPage; ++row) {
    const int pack_index = orcsdr::map_catalog_paging::index_for_row(
        g_state.catalog_packs, g_catalog_cursor.page, row);
    if (pack_index < 0) continue;
    const auto& pack = g_state.catalog_packs[pack_index];
    const int y = 185 + static_cast<int>(row) * 96;
    M5.Display.fillRoundRect(330, y, 888, 88, 8, kPanel);
    M5.Display.drawRoundRect(330, y, 888, 88, 8, kBlue);
    text(pack.title[0] ? pack.title : "DATA PACK", 350, y + 24, kBlue, 3);
    char detail[96];
    snprintf(detail, sizeof(detail), "v%s  source %s  %.1f + %.1f MB",
             pack.version[0] ? pack.version : "--", pack.source_date[0] ? pack.source_date : "--",
             pack.runtime_bytes / 1048576.0, pack.archive_bytes / 1048576.0);
    text(detail, 350, y + 47, TFT_WHITE, 1);
    text(pack.status[0] ? pack.status : "CHECK CATALOG", 350, y + 70,
         pack.installed ? kGreen : kMuted, 1);
    const char* install = pack.installed ? (pack.update_available ? "UPDATE" : "REINSTALL") : "INSTALL";
    button(install, 930, y + 14, 132, 42,
           g_state.catalog_busy || !g_state.catalog_ready ? TFT_DARKGREY : TFT_DARKCYAN);
    button(g_catalog_cursor.armed == pack_index ? "CONFIRM" : "REMOVE", 1072, y + 14, 126, 42,
           pack.installed && !g_state.catalog_busy ? TFT_MAROON : TFT_DARKGREY);
  }
  text("Manual only. Reception pauses and resumes after downloads.", 330, 688, TFT_LIGHTGREY, 1);
  button("LOCAL MAPS",1000,665,218,36,TFT_NAVY);
}

template <typename T, size_t N>
T next_value(T current, const T (&values)[N]);

void draw_display_row(std::size_t index, std::size_t visible_row) {
  const auto r=g_layout.row(visible_row);
  M5.Display.fillRect(r.bounds.x,r.bounds.y,r.bounds.w,r.bounds.h,kBg);
  const bool wide=g_layout.legacy;
  const uint8_t size=3;
  char value[48]{};
  const char* label="";
  switch(index) {
    case 0:label="BRIGHTNESS";snprintf(value,sizeof(value),"%u / 255",g_state.brightness);break;
    case 1:label="SCREEN TIMEOUT";snprintf(value,sizeof(value),g_state.screen_timeout_sec?"%u SEC":"NEVER",g_state.screen_timeout_sec);break;
    case 2:label="MASTER VOLUME";snprintf(value,sizeof(value),"%u / 255",g_state.volume);break;
    case 3:label="DEFAULT SOUND";strlcpy(value,g_state.sound_default?"ON":"OFF",sizeof(value));break;
    case 4:
      label="SCREEN ORIENTATION";
      if (g_state.orientation_profile)
        snprintf(value,sizeof(value),"%u DEG%s",g_state.orientation_degrees,g_state.orientation_locked?" (LOCKED)":"");
      else strlcpy(value,g_state.rotation==3?"LANDSCAPE 180":"LANDSCAPE",sizeof(value));
      break;
    case 5:
      label="AUTO ROTATION";
      strlcpy(value,!g_state.orientation_auto_available?"UNAVAILABLE":g_state.orientation_locked?"LOCKED":g_state.orientation_auto_enabled?"ON":"OFF",sizeof(value));
      break;
    default:return;
  }
  if (index==4 && g_state.orientation_profile) {
    char heading[64];snprintf(heading,sizeof(heading),"ORIENTATION %s",value);
    text(heading,r.label.x,r.label.y,kMuted,2);
    for (int i=0;i<4;++i) {
      char angle[8];snprintf(angle,sizeof(angle),"%d",i*90);
      const bool enabled=(g_state.orientation_manual_angles&(1u<<i)) && !g_state.orientation_locked;
      button(angle,r.angles[i],g_state.orientation_degrees==i*90?TFT_DARKGREEN:TFT_NAVY,enabled);
    }
  } else {
    text(label,r.label.x,r.label.y,kMuted,wide?size:(strlen(label)*18<=size_t(r.label.w)?3:2));
    text(value,r.value.x,r.value.y,TFT_WHITE,wide?size:(strlen(value)*18<=size_t(r.value.w)?3:2),wide?middle_right:middle_left);
    if (index==0 || index==2) {
      button("-",r.minus,TFT_DARKGREY);button("+",r.plus,TFT_DARKCYAN);
    } else if(index==1) button("CHANGE",r.action,TFT_DARKCYAN);
    else if(index==3) button(g_state.sound_default?"ON":"OFF",r.action,g_state.sound_default?TFT_DARKGREEN:TFT_DARKGREY);
    else if(index==4) button("ROTATE",r.action,TFT_DARKCYAN);
    else button(g_state.orientation_auto_enabled?"ON":"OFF",r.action,TFT_DARKCYAN,
                g_state.orientation_auto_available && !g_state.orientation_locked);
  }
  M5.Display.drawFastHLine(r.bounds.x,r.divider_y,r.bounds.w,0x2945);
}

void draw_display_footer() {
  if (g_layout.legacy) return;
  const auto r=g_layout.footer;
  M5.Display.fillRect(r.x,r.y,r.w,r.h,kBg);
  const auto pages=page_count();
  button("PREV",g_layout.previous_page,TFT_NAVY,page_index()>0);
  button("NEXT",g_layout.next_page,TFT_NAVY,page_index()+1<pages);
  char label[32];snprintf(label,sizeof(label),"%u / %u",unsigned(page_index()+1),unsigned(pages));
  text(label,r.x+r.w/2,r.y+22,kMuted,2,middle_center);
}

void draw_display_audio() {
  if (g_layout.legacy) text("DISPLAY & AUDIO",330,115,kBlue,3);
  page_index()=std::min(page_index(),page_count()-1);
  const auto first=page_index()*g_layout.rows_per_page;
  for (std::size_t i=0;i<g_layout.rows_per_page && first+i<display_row_count();++i)
    draw_display_row(first+i,i);
  draw_display_footer();
}

void update_display_row(std::size_t index) {
  const auto first=page_index()*g_layout.rows_per_page;
  if (index>=first && index<first+g_layout.rows_per_page)
    draw_display_row(index,index-first);
}

Action handle_display_touch(int x,int y) {
  const auto first=page_index()*g_layout.rows_per_page;
  for (std::size_t i=0;i<g_layout.rows_per_page && first+i<display_row_count();++i) {
    const auto index=first+i;
    const auto r=g_layout.row(i);
    if(index==0 || index==2) {
      const bool minus=hit(x,y,r.minus),plus=hit(x,y,r.plus);
      if (!minus && !plus) continue;
      auto& value=index==0?g_state.brightness:g_state.volume;
      value=static_cast<uint8_t>(std::clamp<int>(value+(minus?-16:16),index==0?16:0,255));
      update_display_row(index);
      return {index==0?ActionKind::brightness_changed:ActionKind::volume_changed,value};
    }
    if(index==4 && g_state.orientation_profile) {
      if(g_state.orientation_locked) continue;
      for (int angle=0;angle<4;++angle)
        if ((g_state.orientation_manual_angles&(1u<<angle)) && hit(x,y,r.angles[angle]))
          return {ActionKind::orientation_angle_changed,angle*90};
      continue;
    }
    if (!hit(x,y,r.action)) continue;
    if(index==1) {
      g_state.screen_timeout_sec=next_value(g_state.screen_timeout_sec,kTimeouts);
      update_display_row(index);return {ActionKind::timeout_changed,g_state.screen_timeout_sec};
    }
    if(index==3) {
      g_state.sound_default=!g_state.sound_default;
      update_display_row(index);return {ActionKind::sound_changed,g_state.sound_default};
    }
    if(index==4) {
      g_state.rotation=g_state.rotation==3?1:3;
      return {ActionKind::rotation_changed,g_state.rotation};
    }
    if(index==5 && g_state.orientation_auto_available && !g_state.orientation_locked)
      return {ActionKind::orientation_auto_changed,g_state.orientation_auto_enabled?0:1};
  }
  return {};
}

void draw_radio_defaults() {
  if (!g_layout.legacy) {
    const auto layout = make_radio_defaults_layout(g_layout.content,
                                                   g_state.rtl_usb_safe_mode);
    text("RADIO DEFAULTS", layout.title.x, layout.title.y + 14, kBlue, 2);
    char frequency[40];
    snprintf(frequency, sizeof(frequency), "%.3f MHz",
             g_state.fm_frequency_hz / 1000000.0);
    const char* labels[] = {"STARTUP DESTINATION", "AUTO-START RECEPTION",
                            "LAST / DEFAULT BAND", "FM FREQUENCY",
                            "GRAPHICS DEFAULT",
                            g_state.rtl_usb_safe_mode ? "USB RECEIVER" : "GAIN / BIAS-TEE"};
    const char* values[] = {"LAST RADIO",
                            g_state.auto_start_reception ? "ON" : "OFF",
                            g_state.default_band, frequency,
                            g_state.graphics_default ? "ON" : "OFF",
                            g_state.rtl_usb_safe_mode ? "SAFE MODE" : "HOME, AM, FM / RF LAB"};
    const uint16_t value_colors[] = {TFT_WHITE,
                                     g_state.auto_start_reception ? kGreen : kMuted,
                                     TFT_WHITE, TFT_WHITE,
                                     g_state.graphics_default ? kGreen : kMuted,
                                     static_cast<uint16_t>(g_state.rtl_usb_safe_mode ? TFT_ORANGE : kMuted)};
    for (std::size_t i = 0; i < 6; ++i) {
      const auto& row = layout.rows[i];
      const bool action = row.action.w > 0;
      const int label_y = row.label.y + row.label.h / 2;
      text(labels[i], row.label.x, label_y, kMuted, 2);
      if (row.value.w > 0) {
        const int value_y = row.value.y + row.value.h / 2;
        text(values[i], layout.two_columns ? row.value.x + row.value.w
                                           : row.value.x + 4,
             value_y, value_colors[i], 2,
             layout.two_columns ? middle_right : middle_left);
      }
      if (action && (i == 1 || i == 4)) {
        const bool enabled = i == 1 ? g_state.auto_start_reception
                                    : g_state.graphics_default;
        button(enabled ? "ON" : "OFF", row.action,
               enabled ? TFT_DARKGREEN : TFT_DARKGREY);
      } else if (action && i == 5) {
        button("UNPLUGGED - RETRY", row.action, TFT_MAROON);
      }
    }
    if (g_state.rtl_usb_safe_mode)
      wrapped_text("Unplug the RTL-SDR before retrying.", layout.note, TFT_LIGHTGREY);
    return;
  }

  // Keep the original 1280x720 appearance and coordinates unchanged.
  text("RADIO DEFAULTS", 330, 115, kBlue, 3);
  char value[40];
  value_row("STARTUP DESTINATION", "LAST RADIO", 175);
  value_row("AUTO-START RECEPTION", g_state.auto_start_reception ? "ON" : "OFF", 225);
  value_row("LAST / DEFAULT BAND", g_state.default_band, 275);
  snprintf(value, sizeof(value), "%.3f MHz", g_state.fm_frequency_hz / 1000000.0);
  value_row("FM FREQUENCY", value, 325);
  value_row("GRAPHICS DEFAULT", g_state.graphics_default ? "ON" : "OFF", 375);
  if (g_state.rtl_usb_safe_mode) {
    value_row("USB RECEIVER", "SAFE MODE AFTER REPEATED CRASHES", 425, TFT_ORANGE);
    text("Unplug the RTL-SDR before retrying.", 330, 480, TFT_LIGHTGREY, 2);
    button("UNPLUGGED - RETRY", 330, 515, 280, 50, TFT_MAROON);
  } else {
    value_row("GAIN / BIAS-TEE", "HOME, AM, FM / RF LAB", 425, kMuted);
    button("TOGGLE AUTO-START", 330, 500, 260, 50, TFT_DARKCYAN);
    button("TOGGLE GRAPHICS", 620, 500, 240, 50, TFT_DARKCYAN);
  }
}

void draw_storage_status() {
  char value[48];
  if(g_state.sd_capacity_known)
    snprintf(value,sizeof(value),"%.1f / %.1f GB FREE",g_state.sd_free_bytes/1073741824.0,g_state.sd_total_bytes/1073741824.0);
  else snprintf(value,sizeof(value),"%.1f GB / FREE UNKNOWN",g_state.sd_total_bytes/1073741824.0);
  const char* values[]={g_state.sd_ready?"READY":"NOT MOUNTED",g_state.sd_ready?value:"--"};
  if(g_layout.legacy) {
    for(int i=0;i<2;++i) {
      M5.Display.fillRect(330,153+i*50,890,48,kBg);
      value_row(i?"CAPACITY":"SD HEALTH",values[i],175+i*50,
                i?TFT_WHITE:g_state.sd_ready?kGreen:TFT_ORANGE);
    }
  } else {
    const auto l=make_storage_layout(g_layout.content);
    for(int i=0;i<2;++i) {
      const auto r=l.items[i].value;
      M5.Display.fillRect(r.x,r.y,r.w,r.h,kBg);
      text(values[i],r.x,r.y+r.h/2,i?TFT_WHITE:g_state.sd_ready?kGreen:TFT_ORANGE,2);
    }
  }
}

void draw_storage() {
  if (!g_layout.legacy) {
    const Rect content = g_layout.content;
    const StorageLayout layout = make_storage_layout(content);
    text("STORAGE", layout.title.x, layout.title.y + 14, kBlue, 2);

    char value[48];
    snprintf(value, sizeof(value), "%.1f / %.1f GB FREE",
             g_state.sd_free_bytes / 1073741824.0,
             g_state.sd_total_bytes / 1073741824.0);
    if(!g_state.sd_capacity_known) snprintf(value,sizeof(value),"%.1f GB / FREE UNKNOWN",g_state.sd_total_bytes/1073741824.0);
    const char* health = g_state.sd_ready ? "READY" : "NOT MOUNTED";
    const uint16_t health_color = g_state.sd_ready ? kGreen : TFT_ORANGE;
    const char* labels[] = {"SD HEALTH", "CAPACITY", "DATABASES", "MAPS",
                            "RECORDINGS / LOGS"};
    const char* values[] = {health, g_state.sd_ready ? value : "--",
                            "CALCULATING ON DEMAND", "CALCULATING ON DEMAND",
                            "CALCULATING ON DEMAND"};
    const uint16_t colors[] = {health_color, TFT_WHITE, kMuted, kMuted, kMuted};
    for (std::size_t i = 0; i < std::size(labels); ++i) {
      const auto& item = layout.items[i];
      text(labels[i], item.label.x, item.label.y + item.label.h / 2, kMuted, 2);
      text(values[i], item.value.x, item.value.y + item.value.h / 2, colors[i], 2);
    }
    wrapped_text(kStorageNoteText, layout.note, TFT_LIGHTGREY);
    return;
  }

  // Keep the original 1280x720 appearance and coordinates unchanged.
  text("STORAGE", 330, 115, kBlue, 3);
  char value[48];
  value_row("SD HEALTH", g_state.sd_ready ? "READY" : "NOT MOUNTED", 175,
            g_state.sd_ready ? kGreen : TFT_ORANGE);
  snprintf(value, sizeof(value), "%.1f / %.1f GB FREE",
           g_state.sd_free_bytes / 1073741824.0, g_state.sd_total_bytes / 1073741824.0);
  if(!g_state.sd_capacity_known) snprintf(value,sizeof(value),"%.1f GB / FREE UNKNOWN",g_state.sd_total_bytes/1073741824.0);
  value_row("CAPACITY", g_state.sd_ready ? value : "--", 225);
  value_row("DATABASES", "CALCULATING ON DEMAND", 275, kMuted);
  value_row("MAPS", "CALCULATING ON DEMAND", 325, kMuted);
  value_row("RECORDINGS / LOGS", "CALCULATING ON DEMAND", 375, kMuted);
  text("Targeted deletion requires hold and confirmation; partitioning stays in M5Launcher.",
       330, 470, TFT_LIGHTGREY, 2);
}

static bool accessory_forget_confirmation=false;
#if ORCSDR_ORCDIAL
void draw_compact_orcdial(const CompanionLayout& layout, bool show_pairing_note) {
  text("ORCDIAL ACCESSORY", layout.orcdial_title.x,
       layout.orcdial_title.y + 14, kBlue, 2);
  const char* dial_state = g_state.orcdial_upgrade ? "UPGRADE REQUIRED"
                         : g_state.orcdial_verifying ? "VERIFYING"
                         : g_state.orcdial_paired ? "TRUSTED" : "NOT PAIRED";
  char info[112];
  snprintf(info, sizeof(info), "OrcDial %s | %s",
           g_state.orcdial_identity, dial_state);
  wrapped_text(info, layout.orcdial_status, TFT_LIGHTGREY);

  if (accessory_forget_confirmation) {
    wrapped_text("Forget this Dial and require new pairing?",
                 layout.orcdial_prompt, TFT_LIGHTGREY);
  } else if (g_state.orcdial_verifying) {
    snprintf(info, sizeof(info), "COMPARE ON BOTH DEVICES: %06lu",
             static_cast<unsigned long>(g_state.orcdial_code));
    wrapped_text(info, layout.orcdial_prompt, kBlue);
  } else {
    snprintf(info, sizeof(info), "Connection: %s", g_state.orcdial_connection);
    wrapped_text(info, layout.orcdial_prompt, TFT_LIGHTGREY);
  }

  if (accessory_forget_confirmation) {
    button("CANCEL", layout.primary_action, TFT_DARKCYAN);
    button("FORGET & RE-PAIR", layout.secondary_action, TFT_MAROON);
  } else if (g_state.orcdial_verifying) {
    button("CODES MATCH", layout.primary_action, TFT_DARKGREEN);
    button("CANCEL PAIRING", layout.secondary_action, TFT_MAROON);
  } else {
    const char* primary = !strcmp(g_state.orcdial_failure, "Storage failed")
                              ? "RETRY FORGET"
                          : g_state.orcdial_pairing ? "CANCEL PAIRING"
                          : g_state.orcdial_connected ? "DISCONNECT"
                          : g_state.orcdial_paired ? "CONNECT" : "PAIR";
    button(primary, layout.primary_action, TFT_DARKCYAN);
    if (g_state.orcdial_paired ||
        !strcmp(g_state.orcdial_failure, "Storage failed"))
      button("FORGET & RE-PAIR", layout.secondary_action, TFT_MAROON);
  }
  if (g_state.orcdial_paired)
    button(g_state.orcdial_boot_connect ? "BOOT CONNECT: ON"
                                        : "BOOT CONNECT: OFF",
           layout.boot_action, TFT_DARKCYAN);
  if (strcmp(g_state.orcdial_failure, "None")) {
    const char* failure = !strcmp(g_state.orcdial_failure, "Storage failed")
                              ? "STORAGE FAILED: trust may return after restart. Retry Forget."
                              : g_state.orcdial_failure;
    wrapped_text(failure, layout.orcdial_error, TFT_RED);
  }
  if (show_pairing_note) {
    const int note_y = layout.orcdial_error.y + layout.orcdial_error.h;
    wrapped_text("Pair once; trust survives updates. Confirm the same code on both devices.",
                 {layout.orcdial_title.x, note_y, layout.orcdial_error.w,
                  std::max(24, g_layout.content.y + g_layout.content.h - note_y)},
                 TFT_LIGHTGREY);
  }
}
#endif

void draw_companion() {
  if (!g_layout.legacy) {
    const bool shallow = g_layout.content.h < 320;
#if ORCSDR_ORCDIAL
    const bool include_dial = true;
    page_index() = std::min<std::size_t>(page_index(), page_count() - 1);
    const bool dial_page = shallow && page_index() == 1;
#else
    const bool include_dial = false;
    const bool dial_page = false;
#endif
    const auto layout = make_companion_layout(g_layout.content, dial_page, include_dial);
    if (!dial_page) {
      text("ACCESSORIES & COMPANION", layout.title.x, layout.title.y + 14, kBlue, 2);
      text("WEB CONSOLE", layout.web_status.x, layout.web_status.y + 14, kMuted, 2);
      text(g_state.web_console_enabled ? "ON" : "OFF",
           layout.web_status.x + 210, layout.web_status.y + 14,
           g_state.web_console_enabled ? kGreen : kMuted, 2);
      button(g_state.web_console_enabled ? "DISABLE" : "ENABLE", layout.web_action,
             g_state.web_console_enabled ? TFT_MAROON : TFT_DARKGREEN);

      char info[96];
      snprintf(info, sizeof(info), "URL: %s",
               g_state.web_console_listening && g_state.web_console_url[0]
                   ? g_state.web_console_url : "OFFLINE");
      wrapped_text(info, layout.url,
                   g_state.web_console_listening ? kGreen : kMuted);
      snprintf(info, sizeof(info), "LOCAL DISCOVERY: %s",
               g_state.web_console_listening ? "orcsdr.local" : "NOT ENABLED");
      text(info, layout.discovery.x, layout.discovery.y + layout.discovery.h / 2,
           g_state.web_console_listening ? kGreen : kMuted, 2);
      wrapped_text("Read-only LAN console; private data hidden.",
                   layout.note, kMuted);
      text(g_state.companion_supported ? "BLUETOOTH: AVAILABLE"
                                       : "BLUETOOTH: FEASIBILITY PENDING",
           layout.bluetooth.x, layout.bluetooth.y + layout.bluetooth.h / 2,
           kMuted, 2);

#if ORCSDR_ORCDIAL
      if (include_dial && !shallow) {
        draw_compact_orcdial(layout, true);
      }
#endif
    } else {
#if ORCSDR_ORCDIAL
      draw_compact_orcdial(layout, false);
#endif
    }
    if (shallow && page_count() > 1) draw_display_footer();
    return;
  }

  // Keep the original 1280x720 appearance and coordinates unchanged.
  text("ACCESSORIES & COMPANION", 330, 115, kBlue, 3);
  value_row("WEB CONSOLE", g_state.web_console_enabled ? "ON" : "OFF", 175,
            g_state.web_console_enabled ? kGreen : kMuted);
  button(g_state.web_console_enabled ? "DISABLE" : "ENABLE", 960, 150, 170, 48,
         g_state.web_console_enabled ? TFT_MAROON : TFT_DARKGREEN);
  value_row("URL",
            g_state.web_console_listening && g_state.web_console_url[0]
                ? g_state.web_console_url
                : "OFFLINE",
            225, g_state.web_console_listening ? kGreen : kMuted);
  value_row("LOCAL DISCOVERY",
            g_state.web_console_listening ? "orcsdr.local" : "NOT ENABLED", 275,
            g_state.web_console_listening ? kGreen : kMuted);
  text("LAN console is read-only; no passwords, location, or device control.",330,315,kMuted,2);
  value_row("BLUETOOTH", g_state.companion_supported ? "AVAILABLE" : "FEASIBILITY PENDING",
            375, kMuted);
#if ORCSDR_ORCDIAL
  text("ORCDIAL ACCESSORY",330,420,kBlue,3);
  char info[96];snprintf(info,sizeof info,"OrcDial  %s  |  %s",g_state.orcdial_identity,g_state.orcdial_upgrade?"PAIRING UPGRADE REQUIRED":g_state.orcdial_paired?"TRUSTED":"NOT PAIRED");
  text(info,330,460,g_state.orcdial_paired?kGreen:TFT_LIGHTGREY,2);
  if(accessory_forget_confirmation) {
    text("Forget this Dial and require new pairing?",330,505,TFT_LIGHTGREY,2);
    button("CANCEL",330,540,280,48,TFT_DARKCYAN);
    button("FORGET & RE-PAIR",640,540,330,48,TFT_MAROON);
  }else if(g_state.orcdial_verifying) {
    snprintf(info,sizeof info,"COMPARE ON BOTH DEVICES: %06lu",(unsigned long)g_state.orcdial_code);
    text(info,330,502,kBlue,3);
    button("CODES MATCH",330,540,280,48,TFT_DARKGREEN);
    button("CANCEL PAIRING",640,540,330,48,TFT_MAROON);
  }else {
    snprintf(info,sizeof info,"Connection: %s",g_state.orcdial_connection);text(info,330,505,TFT_LIGHTGREY,2);
    button(!strcmp(g_state.orcdial_failure,"Storage failed")?"RETRY FORGET":g_state.orcdial_pairing?"CANCEL PAIRING":g_state.orcdial_connected?"DISCONNECT":g_state.orcdial_paired?"CONNECT":"PAIR",330,540,280,48,TFT_DARKCYAN);
    if(g_state.orcdial_paired || !strcmp(g_state.orcdial_failure,"Storage failed"))button("FORGET & RE-PAIR",640,540,330,48,TFT_MAROON);
  }
  if(strcmp(g_state.orcdial_failure,"None")) {
    text(!strcmp(g_state.orcdial_failure,"Storage failed")?"STORAGE FAILED: trust may return after restart. Retry Forget.":g_state.orcdial_failure,330,660,TFT_RED,2);
  }
  if(g_state.orcdial_paired)button(g_state.orcdial_boot_connect?"BOOT CONNECT: ON":"BOOT CONNECT: OFF",640,606,330,48,TFT_DARKCYAN);
  text("Pair once; trust survives updates. Confirm the same code on both devices.",330,690,TFT_LIGHTGREY,2);
#endif

}

void compact_system_power_value(int index, char* value, size_t size) {
  switch (index) {
    case 0:
      if (g_state.battery_mv >= 0)
        snprintf(value, size, "%d mV  /  %ld%%", g_state.battery_mv,
                 static_cast<long>(g_state.battery_level));
      else strlcpy(value, "UNAVAILABLE", size);
      break;
    case 1:
      strlcpy(value, g_state.charging_state[0] ? g_state.charging_state : "UNKNOWN", size);
      break;
    case 2:
      snprintf(value, size, "%ld mA (RAW)", static_cast<long>(g_state.battery_current_ma));
      break;
    default:
      if (g_state.vbus_mv >= 0) snprintf(value, size, "%d mV", g_state.vbus_mv);
      else strlcpy(value, "NOT EXPOSED", size);
      break;
  }
}

void draw_compact_system_row(const SectionValueRow& row, const char* label,
                             const char* value, uint16_t color = TFT_WHITE) {
  M5.Display.fillRect(row.bounds.x, row.bounds.y, row.bounds.w, row.bounds.h, kBg);
  char clipped[80]{};
  strlcpy(clipped, value ? value : "", sizeof(clipped));
  M5.Display.setTextSize(2);
  const int max_width = std::max(1, row.value.w - 4);
  const size_t source_length = strlen(clipped);
  size_t length = source_length;
  const int dots_width = M5.Display.textWidth("...");
  while (length && M5.Display.textWidth(clipped) + dots_width > max_width)
    clipped[--length] = '\0';
  if (length < source_length && length + 4 < sizeof(clipped)) {
    clipped[length++] = '.';
    clipped[length++] = '.';
    clipped[length++] = '.';
    clipped[length] = '\0';
  }
  if (row.bounds.h <= 32) {
    text(label, row.label.x, row.label.y + row.label.h / 2, kMuted, 2);
    text(clipped, row.value.x + row.value.w, row.value.y + row.value.h / 2,
         color, 2, middle_right);
    return;
  }
  text(label, row.label.x, row.label.y + row.label.h / 2, kMuted, 2);
  text(clipped, row.value.x, row.value.y + row.value.h / 2, color, 2);
}

void draw_system_power() {
  if (!g_layout.legacy) {
    const auto layout = make_system_layout(g_layout.content, page_index());
    if (layout.shallow && layout.page != 1) return;
    M5.Display.fillRect(layout.power_bounds.x, layout.power_bounds.y,
                        layout.power_bounds.w, layout.power_bounds.h, kBg);
    static constexpr const char* labels[] = {
        "BATTERY RAIL", "CHARGE STATE", "BATTERY CURRENT", "USB / VBUS"};
    for (int i = 0; i < 4; ++i) {
      char value[48]{};
      compact_system_power_value(i, value, sizeof(value));
      const uint16_t color = i == 0 ? (g_state.battery_mv >= 0 ? kGreen : TFT_ORANGE)
                            : i == 3 ? (g_state.vbus_mv >= 0 ? kGreen : kMuted)
                                      : TFT_WHITE;
      draw_compact_system_row(layout.power[i], labels[i], value, color);
    }
    return;
  }
  M5.Display.fillRect(330, 160, 890, 200, kBg);
  char value[48];
  if (g_state.battery_mv >= 0) {
    snprintf(value, sizeof(value), "%d mV  /  %ld%%", g_state.battery_mv,
             static_cast<long>(g_state.battery_level));
  } else {
    strlcpy(value, "UNAVAILABLE", sizeof(value));
  }
  value_row("BATTERY RAIL", value, 175, g_state.battery_mv >= 0 ? kGreen : TFT_ORANGE);
  value_row("CHARGE STATE", g_state.charging_state[0] ? g_state.charging_state : "UNKNOWN", 225);
  snprintf(value, sizeof(value), "%ld mA (RAW)",
           static_cast<long>(g_state.battery_current_ma));
  value_row("BATTERY CURRENT", value, 275);
  if (g_state.vbus_mv >= 0)
    snprintf(value, sizeof(value), "%d mV", g_state.vbus_mv);
  else
    strlcpy(value, "NOT EXPOSED", sizeof(value));
  value_row("USB / VBUS", value, 325, g_state.vbus_mv >= 0 ? kGreen : kMuted);
}

const char* compact_clock_note() {
  return g_state.ntp_state == 1 ? "Contacting time server..."
       : g_state.ntp_state == 2 ? "Clock set from network."
       : g_state.ntp_state == 3 ? "Network time failed; set it by hand."
       : g_state.wifi_connected ? "Wi-Fi can set the clock with optional NTP sync."
                                : "No Wi-Fi needed; set the time by hand.";
}

const char* clock_storage_note() {
  return g_state.clock_hardware_backed
      ? "The hardware clock holds UTC; the time zone is only an offset."
      : "System UTC; Wi-Fi sets time after boot. The time zone is only an offset.";
}

void draw_compact_system() {
  page_index() = std::min(page_index(), page_count() - 1);
  const auto layout = make_system_layout(g_layout.content, page_index());
  text("SYSTEM", layout.title.x, layout.title.y+layout.title.h/2, kBlue, 3);
  button("SET CLOCK", layout.set_clock, TFT_DARKCYAN);
  button("RUN SETUP", layout.run_setup, TFT_NAVY);
  if (layout.shallow) {
    if (layout.page == 0) {
      const bool local_known = g_state.rtc_valid && g_state.rtc_local[0];
      const char* labels[] = {"LOCAL TIME", "UTC TIME", "TIME ZONE", "BUILD",
                              "UPTIME", "NETWORK", "SD"};
      char zone[24]{}, uptime[32]{};
      clock_settings::format_offset(zone, sizeof(zone), g_state.utc_offset_minutes);
      snprintf(uptime, sizeof(uptime), "%lu SEC", static_cast<unsigned long>(g_state.uptime_seconds));
      const char* values[] = {
          local_known ? g_state.rtc_local : "TIME NOT SET",
          g_state.rtc_valid ? g_state.rtc_utc : "TIME NOT SET", zone,
          g_state.build_identity, uptime,
          g_state.wifi_connected ? "CONNECTED" : "OFFLINE",
          g_state.sd_ready ? "READY" : "UNAVAILABLE"};
      for (int i = 0; i < 7; ++i)
        draw_compact_system_row(layout.status[i], labels[i], values[i],
                                (i == 0 && !local_known) || (i == 1 && !g_state.rtc_valid)
                                    ? TFT_ORANGE : TFT_WHITE);
    } else {
      draw_system_power();
      wrapped_text(g_state.rtc_valid ? clock_storage_note()
                                     : (g_state.clock_hardware_backed ? "Tap SET CLOCK, or use USB time synchronization."
                                      : "Tap SET CLOCK; sync over Wi-Fi or set system UTC."),
                   layout.note, g_state.rtc_valid ? TFT_LIGHTGREY : TFT_ORANGE);
    }
    draw_display_footer();
    return;
  }

  static constexpr const char* power_labels[] = {
      "BATTERY RAIL", "CHARGE STATE", "BATTERY CURRENT", "USB / VBUS"};
  for (int i = 0; i < 4; ++i) {
    char value[48]{};
    compact_system_power_value(i, value, sizeof(value));
    const uint16_t color = i == 0 ? (g_state.battery_mv >= 0 ? kGreen : TFT_ORANGE)
                          : i == 3 ? (g_state.vbus_mv >= 0 ? kGreen : kMuted)
                                    : TFT_WHITE;
    draw_compact_system_row(layout.power[i], power_labels[i], value, color);
  }
  const bool local_known = g_state.rtc_valid && g_state.rtc_local[0];
  static constexpr const char* status_labels[] = {
      "LOCAL TIME", "UTC TIME", "TIME ZONE", "BUILD", "UPTIME", "NETWORK", "SD"};
  char zone[24]{}, uptime[32]{};
  clock_settings::format_offset(zone, sizeof(zone), g_state.utc_offset_minutes);
  snprintf(uptime, sizeof(uptime), "%lu SEC", static_cast<unsigned long>(g_state.uptime_seconds));
  const char* status_values[] = {
      local_known ? g_state.rtc_local : "TIME NOT SET",
      g_state.rtc_valid ? g_state.rtc_utc : "TIME NOT SET", zone, g_state.build_identity, uptime,
      g_state.wifi_connected ? "CONNECTED" : "OFFLINE",
      g_state.sd_ready ? "READY" : "UNAVAILABLE"};
  for (int i = 0; i < 7; ++i)
    draw_compact_system_row(layout.status[i], status_labels[i], status_values[i],
                            (i == 0 && !local_known) || (i == 1 && !g_state.rtc_valid)
                                ? TFT_ORANGE : TFT_WHITE);
  wrapped_text(g_state.rtc_valid ? clock_storage_note()
                                 : (g_state.clock_hardware_backed ? "Tap SET CLOCK, or USB: python tools/sync_tab5_rtc.py COMx"
                                      : "Tap SET CLOCK; sync over Wi-Fi or set system UTC."),
               layout.note, g_state.rtc_valid ? TFT_LIGHTGREY : TFT_ORANGE);
}

void draw_compact_system_clock_values() {
  const auto layout = make_system_layout(g_layout.content, page_index());
  if (layout.shallow && layout.page == 1) {
    M5.Display.fillRect(layout.note.x,layout.note.y,layout.note.w,layout.note.h,kBg);
    wrapped_text(g_state.rtc_valid ? clock_storage_note()
                                   : (g_state.clock_hardware_backed ? "Tap SET CLOCK, or use USB time synchronization."
                                      : "Tap SET CLOCK; sync over Wi-Fi or set system UTC."),
                 layout.note, g_state.rtc_valid ? TFT_LIGHTGREY : TFT_ORANGE);
    return;
  }
  const bool local_known = g_state.rtc_valid && g_state.rtc_local[0];
  char zone[24]{};
  clock_settings::format_offset(zone, sizeof(zone), g_state.utc_offset_minutes);
  const char* labels[] = {"LOCAL TIME", "UTC TIME", "TIME ZONE"};
  const char* values[] = {local_known ? g_state.rtc_local : "TIME NOT SET",
                          g_state.rtc_valid ? g_state.rtc_utc : "TIME NOT SET", zone};
  for (int i = 0; i < 3; ++i)
    draw_compact_system_row(layout.status[i], labels[i], values[i],
                            (i == 0 && !local_known) || (i == 1 && !g_state.rtc_valid)
                                ? TFT_ORANGE : TFT_WHITE);
  if (!layout.shallow) {
    M5.Display.fillRect(layout.note.x,layout.note.y,layout.note.w,layout.note.h,kBg);
    wrapped_text(g_state.rtc_valid ? clock_storage_note()
                                   : (g_state.clock_hardware_backed ? "Tap SET CLOCK, or USB: python tools/sync_tab5_rtc.py COMx"
                                      : "Tap SET CLOCK; sync over Wi-Fi or set system UTC."),
                 layout.note, g_state.rtc_valid ? TFT_LIGHTGREY : TFT_ORANGE);
  }
}

void draw_system() {
  if (!g_layout.legacy) {
    draw_compact_system();
    return;
  }
  text("SYSTEM", 330, 115, kBlue, 3);
  button("SET CLOCK", 1000, 100, 218, 46, TFT_DARKCYAN);
  button("RUN SETUP",720,100,240,46,TFT_NAVY);
  char value[48];
  draw_system_power();
  const bool local_known = g_state.rtc_valid && g_state.rtc_local[0];
  value_row("LOCAL TIME", local_known ? g_state.rtc_local : "TIME NOT SET", 375, local_known ? kGreen : TFT_ORANGE);
  value_row("UTC TIME", g_state.rtc_valid ? g_state.rtc_utc : "TIME NOT SET",
            415, g_state.rtc_valid ? kGreen : TFT_ORANGE);
  clock_settings::format_offset(value, sizeof(value), g_state.utc_offset_minutes);
  value_row("TIME ZONE", value, 455);
  value_row("BUILD", g_state.build_identity, 495);
  snprintf(value, sizeof(value), "%lu SEC", static_cast<unsigned long>(g_state.uptime_seconds));
  value_row("UPTIME", value, 535);
  value_row("NETWORK", g_state.wifi_connected ? "CONNECTED" : "OFFLINE", 575);
  value_row("SD", g_state.sd_ready ? "READY" : "UNAVAILABLE", 615);
  text(g_state.rtc_valid ? clock_storage_note()
                         : (g_state.clock_hardware_backed ? "Tap SET CLOCK, or USB: python tools/sync_tab5_rtc.py COMx"
                                      : "Tap SET CLOCK; sync over Wi-Fi or set system UTC."),
       330, 660, g_state.rtc_valid ? TFT_LIGHTGREY : TFT_ORANGE, 2);
}

void draw_content();

// ---- Clock editor ---------------------------------------------------------------------------------------------
constexpr int kClockColumnX = 360, kClockColumnPitch = 172, kClockButtonW = 150, kClockButtonH = 54;
constexpr int kClockPlusY = 178, kClockMinusY = 290;
constexpr int kOffsetMinusX = 760, kOffsetPlusX = 1100, kOffsetValueX = 985, kOffsetY = 372, kOffsetButtonW = 110,
              kOffsetButtonH = 56;
constexpr int kClockActionY = 530, kClockActionW = 260, kClockActionH = 58;
constexpr int kNtpX = 360, kCancelX = 640, kSaveX = 920;

void clock_load_fields() {
  g_clock_offset = g_state.utc_offset_minutes;
  clock_settings::LocalTime local;
  if (g_state.rtc_valid && clock_settings::utc_to_local(g_state.rtc_epoch, g_clock_offset, &local))
    g_clock_local = local;
  else
    g_clock_local = clock_settings::LocalTime{};
}

const char* clock_field_name(int index) {
  static constexpr const char* names[] = {"YEAR", "MONTH", "DAY", "HOUR", "MIN"};
  return names[index];
}

int clock_field_value(int index) {
  const int values[] = {g_clock_local.year, g_clock_local.month, g_clock_local.day,
                        g_clock_local.hour, g_clock_local.minute};
  return values[index];
}

void draw_compact_clock_field_value(int index) {
  const auto layout = make_clock_editor_layout(g_layout.content, g_layout.footer);
  const Rect r = layout.field_value[index];
  M5.Display.fillRect(r.x, r.y, r.w, r.h, kBg);
  char value[12]{};
  snprintf(value, sizeof(value), index == 0 ? "%04d" : "%02d", clock_field_value(index));
  text(value, r.x + r.w / 2, r.y + r.h / 2, TFT_WHITE, 2, middle_center);
}

void draw_compact_clock_offset_value() {
  const auto layout = make_clock_editor_layout(g_layout.content, g_layout.footer);
  const Rect r = layout.offset_value;
  M5.Display.fillRect(r.x, r.y, r.w, r.h, kBg);
  char value[24]{};
  clock_settings::format_offset(value, sizeof(value), g_clock_offset);
  text(value, r.x + r.w / 2, r.y + r.h / 2, kGreen, 2, middle_center);
}

void draw_compact_clock_preview() {
  const auto layout = make_clock_editor_layout(g_layout.content, g_layout.footer);
  const Rect r = layout.preview;
  M5.Display.fillRect(r.x, r.y, r.w, r.h, kBg);
  uint32_t utc = 0;
  char value[48]{};
  if (clock_settings::local_to_utc(g_clock_local, g_clock_offset, &utc)) {
    char stamp[24]{};
    clock_settings::format_local(stamp, sizeof(stamp), utc, 0);
    snprintf(value, sizeof(value), "SETS UTC  %sZ", stamp);
    text(value, r.x, r.y + r.h / 2, TFT_LIGHTGREY, 2);
  } else {
    text("OUT OF RANGE (2024 - 2099 UTC)", r.x, r.y + r.h / 2, TFT_ORANGE, 2);
  }
}

void draw_compact_clock_note() {
  const auto layout = make_clock_editor_layout(g_layout.content, g_layout.footer);
  const Rect r = layout.note;
  M5.Display.fillRect(r.x, r.y, r.w, r.h, kBg);
  wrapped_text(compact_clock_note(), r, g_state.ntp_state == 3 ? TFT_ORANGE : kMuted);
}

void draw_compact_clock_ntp_button() {
  const auto layout = make_clock_editor_layout(g_layout.content, g_layout.footer);
  const bool ready = g_state.wifi_connected && g_state.ntp_state != 1 && !g_ntp_tapped;
  button("SYNC NTP", layout.ntp, ready ? TFT_DARKCYAN : TFT_DARKGREY, ready);
}

void draw_compact_clock_editor() {
  const auto layout = make_clock_editor_layout(g_layout.content, g_layout.footer);
  M5.Display.fillRect(layout.bounds.x, layout.bounds.y, layout.bounds.w, layout.bounds.h, kBg);
  text("SET CLOCK", layout.title.x, layout.title.y+layout.title.h/2, kBlue, 3);
  for (int i = 0; i < 5; ++i) {
    const Rect label = layout.field_label[i];
    text(clock_field_name(i), label.x + label.w / 2, label.y + label.h / 2,
         kMuted, 2, middle_center);
    draw_compact_clock_field_value(i);
    button("-", layout.field_minus[i], TFT_DARKGREY);
    button("+", layout.field_plus[i], TFT_DARKCYAN);
  }
  const Rect offset_label = layout.offset_label;
  text(g_layout.content.h < 320 ? "UTC OFFSET (TIME ZONE)" : "ZONE",
       offset_label.x, offset_label.y + offset_label.h / 2, kMuted, 2);
  button("-", layout.offset_minus, TFT_DARKGREY);
  draw_compact_clock_offset_value();
  button("+", layout.offset_plus, TFT_DARKCYAN);
  draw_compact_clock_preview();
  draw_compact_clock_note();
  draw_compact_clock_ntp_button();
  button("CANCEL", layout.cancel, TFT_MAROON);
  button("SAVE", layout.save, TFT_DARKGREEN);
}

void draw_clock_editor() {
  if (!g_layout.legacy) {
    draw_compact_clock_editor();
    return;
  }
  M5.Display.fillRect(kRailW, kHeaderH, 1280 - kRailW, 720 - kHeaderH, kBg);
  text("SET CLOCK", 330, 112, kBlue, 3);
  static constexpr const char* kNames[] = {"YEAR", "MONTH", "DAY", "HOUR", "MIN"};
  const int values[] = {g_clock_local.year, g_clock_local.month, g_clock_local.day, g_clock_local.hour,
                        g_clock_local.minute};
  char value[40];
  for (int i = 0; i < 5; ++i) {
    const int x = kClockColumnX + i * kClockColumnPitch;
    text(kNames[i], x + kClockButtonW / 2, 160, kMuted, 2, middle_center);
    button("+", x, kClockPlusY, kClockButtonW, kClockButtonH, TFT_DARKCYAN);
    snprintf(value, sizeof(value), i == 0 ? "%04d" : "%02d", values[i]);
    text(value, x + kClockButtonW / 2, 262, TFT_WHITE, 4, middle_center);
    button("-", x, kClockMinusY, kClockButtonW, kClockButtonH, TFT_DARKGREY);
  }
  text("UTC OFFSET (TIME ZONE)", 360, 400, kMuted, 2);
  button("-", kOffsetMinusX, kOffsetY, kOffsetButtonW, kOffsetButtonH, TFT_DARKGREY);
  clock_settings::format_offset(value, sizeof(value), g_clock_offset);
  text(value, kOffsetValueX, 400, kGreen, 3, middle_center);
  button("+", kOffsetPlusX, kOffsetY, kOffsetButtonW, kOffsetButtonH, TFT_DARKCYAN);
  uint32_t utc = 0;
  if (clock_settings::local_to_utc(g_clock_local, g_clock_offset, &utc)) {
    char stamp[24]{};
    clock_settings::format_local(stamp, sizeof(stamp), utc, 0);
    snprintf(value, sizeof(value), "SETS UTC  %sZ", stamp);
    text(value, 360, 468, TFT_LIGHTGREY, 2);
  } else {
    text("OUT OF RANGE (2024 - 2099 UTC)", 360, 468, TFT_ORANGE, 2);
  }
  const char* note = g_state.ntp_state == 1 ? "Contacting the time server..."
                     : g_state.ntp_state == 2 ? "Clock set from the network."
                     : g_state.ntp_state == 3 ? "Network time failed; set it by hand."
                     : g_state.wifi_connected ? "Wi-Fi is connected: SYNC NTP can set the clock (optional)."
                                              : "No Wi-Fi needed: set the time by hand.";
  text(note, 360, 500, g_state.ntp_state == 3 ? TFT_ORANGE : kMuted, 2);
  const bool ntp_ready = g_state.wifi_connected && g_state.ntp_state != 1 && !g_ntp_tapped;
  button("SYNC NTP", kNtpX, kClockActionY, kClockActionW, kClockActionH, ntp_ready ? TFT_DARKCYAN : TFT_DARKGREY);
  button("CANCEL", kCancelX, kClockActionY, kClockActionW, kClockActionH, TFT_MAROON);
  button("SAVE", kSaveX, kClockActionY, kClockActionW, kClockActionH, TFT_DARKGREEN);
}

void start_clock_edit() {
  g_clock_edit = true;
  g_clock_dirty = false;
  clock_load_fields();
  g_ntp_tapped = false;
  draw_clock_editor();
}

Action handle_compact_clock_editor(int x, int y) {
  using clock_settings::Field;
  static constexpr Field fields[] = {Field::year, Field::month, Field::day,
                                     Field::hour, Field::minute};
  const auto layout = make_clock_editor_layout(g_layout.content, g_layout.footer);
  for (int i = 0; i < 5; ++i) {
    const bool plus = hit(x, y, layout.field_plus[i]);
    const bool minus = hit(x, y, layout.field_minus[i]);
    if (!plus && !minus) continue;
    const auto before = g_clock_local;
    g_clock_local = clock_settings::adjust(g_clock_local, fields[i], plus ? 1 : -1);
    g_clock_dirty = true;
    const int old_values[] = {before.year, before.month, before.day, before.hour, before.minute};
    const int values[] = {g_clock_local.year, g_clock_local.month, g_clock_local.day,
                          g_clock_local.hour, g_clock_local.minute};
    // Month/year adjustment can also clamp the day (for example Jan 31 -> Feb 28).
    for (int field = 0; field < 5; ++field)
      if (values[field] != old_values[field]) draw_compact_clock_field_value(field);
    draw_compact_clock_preview();
    return {};
  }
  const bool offset_minus = hit(x, y, layout.offset_minus);
  const bool offset_plus = hit(x, y, layout.offset_plus);
  if (offset_minus || offset_plus) {
    const int32_t next = clock_settings::step_offset(g_clock_offset, offset_plus ? 1 : -1);
    if (next == g_clock_offset) return {};
    g_clock_offset = next;
    bool local_changed = false;
    clock_settings::LocalTime local;
    if (!g_clock_dirty && g_state.rtc_valid &&
        clock_settings::utc_to_local(g_state.rtc_epoch, next, &local)) {
      g_clock_local = local;
      local_changed = true;
    }
    draw_compact_clock_offset_value();
    if (local_changed)
      for (int i = 0; i < 5; ++i) draw_compact_clock_field_value(i);
    draw_compact_clock_preview();
    return {};
  }
  if (hit(x, y, layout.ntp)) {
    if (!g_state.wifi_connected || g_state.ntp_state == 1 || g_ntp_tapped) return {};
    g_ntp_tapped = true;
    draw_compact_clock_ntp_button();
    return {ActionKind::clock_ntp_sync, 0};
  }
  if (hit(x, y, layout.cancel)) {
    g_clock_edit = false;
    draw_content();
    return {};
  }
  if (hit(x, y, layout.save)) {
    if (!g_clock_dirty && g_state.rtc_valid) {
      g_clock_edit = false;
      draw_content();
      return g_clock_offset != g_state.utc_offset_minutes
                 ? Action{ActionKind::clock_offset_changed, g_clock_offset}
                 : Action{};
    }
    uint32_t utc = 0;
    if (!clock_settings::local_to_utc(g_clock_local, g_clock_offset, &utc)) return {};
    g_clock_edit = false;
    draw_content();
    return {ActionKind::clock_set_utc, g_clock_offset, utc};
  }
  return {};
}

Action handle_clock_editor(int x, int y) {
  if (!g_layout.legacy) return handle_compact_clock_editor(x, y);
  using clock_settings::Field;
  static constexpr Field kFields[] = {Field::year, Field::month, Field::day, Field::hour, Field::minute};
  for (int i = 0; i < 5; ++i) {
    const int column = kClockColumnX + i * kClockColumnPitch;
    const bool plus = hit(x, y, column, kClockPlusY, kClockButtonW, kClockButtonH);
    const bool minus = hit(x, y, column, kClockMinusY, kClockButtonW, kClockButtonH);
    if (plus || minus) {
      g_clock_local = clock_settings::adjust(g_clock_local, kFields[i], plus ? 1 : -1);
      g_clock_dirty = true;
      draw_clock_editor();
      return {};
    }
  }
  const bool offset_minus = hit(x, y, kOffsetMinusX, kOffsetY, kOffsetButtonW, kOffsetButtonH);
  const bool offset_plus = hit(x, y, kOffsetPlusX, kOffsetY, kOffsetButtonW, kOffsetButtonH);
  if (offset_minus || offset_plus) {
    const int32_t next = clock_settings::step_offset(g_clock_offset, offset_plus ? 1 : -1);
    if (next == g_clock_offset) return {};
    g_clock_offset = next;   // nothing is stored until SAVE
    // If only the zone is being changed the RTC is already right: show the same instant in the new zone.
    clock_settings::LocalTime local;
    if (!g_clock_dirty && g_state.rtc_valid && clock_settings::utc_to_local(g_state.rtc_epoch, next, &local))
      g_clock_local = local;
    draw_clock_editor();
    return {};
  }
  if (hit(x, y, kNtpX, kClockActionY, kClockActionW, kClockActionH))
  {
    if (!g_state.wifi_connected || g_state.ntp_state == 1 || g_ntp_tapped) return {};
    g_ntp_tapped = true;   // one request per tap, even before the new state is reported
    return {ActionKind::clock_ntp_sync, 0};
  }
  if (hit(x, y, kCancelX, kClockActionY, kClockActionW, kClockActionH)) {
    g_clock_edit = false;   // nothing was stored, so there is nothing to undo
    draw_content();
    return {};
  }
  if (hit(x, y, kSaveX, kClockActionY, kClockActionW, kClockActionH)) {
    if (!g_clock_dirty && g_state.rtc_valid) {   // only the zone can have changed; the RTC is already right
      g_clock_edit = false;
      draw_content();
      return g_clock_offset != g_state.utc_offset_minutes ? Action{ActionKind::clock_offset_changed, g_clock_offset}
                                                           : Action{};
    }
    uint32_t utc = 0;
    if (!clock_settings::local_to_utc(g_clock_local, g_clock_offset, &utc)) return {};
    g_clock_edit = false;
    draw_content();
    return {ActionKind::clock_set_utc, g_clock_offset, utc};
  }
  return {};
}

void draw_content() {
  if (g_clock_edit) {
    draw_clock_editor();
    return;
  }
  if (g_layout.legacy) M5.Display.fillRect(kRailW, kHeaderH, 1280 - kRailW, 720 - kHeaderH, kBg);
  else {
    const int top=g_layout.navigation.y+g_layout.navigation.h;
    M5.Display.fillRect(g_layout.content.x,top,g_layout.content.w,
      g_layout.content.y+g_layout.content.h-top,kBg);
    M5.Display.fillRect(g_layout.footer.x,g_layout.footer.y,g_layout.footer.w,g_layout.footer.h,kBg);
  }
  switch (g_section) {
    case Section::connectivity: draw_connectivity(); break;
    case Section::firmware_updates: draw_firmware_updates(); break;
    case Section::location_adsb: draw_location(); break;
    case Section::data_maps: draw_data_maps(); break;
    case Section::display_audio: draw_display_audio(); break;
    case Section::radio_defaults: draw_radio_defaults(); break;
    case Section::storage: draw_storage(); break;
    case Section::companion: draw_companion(); break;
    case Section::system: draw_system(); break;
    default: break;
  }
}

void restore_settings_content() {
  if(!g_layout.legacy) {
    const int top=g_layout.header.y+g_layout.header.h;
    M5.Display.fillRect(g_layout.viewport.x,top,g_layout.viewport.w,
      g_layout.viewport.y+g_layout.viewport.h-top,kBg);
    draw_rail();
  }
  draw_content();
}

void start_edit(EditField field) {
  g_edit = field;
  const int32_t value = field == EditField::latitude ? g_state.latitude_e7
                                                     : g_state.longitude_e7;
  snprintf(g_entry, sizeof(g_entry), "%.7f", value / 10000000.0);
}

void draw_keypad() {
  if(!g_layout.legacy) {
    set_wifi_editor_viewport();
    if(!text_editor::active()) text_editor::begin(g_edit==EditField::latitude?"LATITUDE (-90..90)":"LONGITUDE (-180..180)",g_entry,sizeof(g_entry)-1,false,"SAVE");
    text_editor::draw();return;
  }
  M5.Display.fillRect(kRailW, kHeaderH, 1280 - kRailW, 720 - kHeaderH, kBg);
  text(g_edit == EditField::latitude ? "EDIT LATITUDE" : "EDIT LONGITUDE",
       330, 112, kBlue, 3);
  button(g_entry[0] ? g_entry : "0", 360, 150, 820, 58, TFT_NAVY);
  static constexpr const char* keys[] = {"1", "2", "3", "4", "5", "6", "7",
                                          "8", "9", "-", "0", ".", "BACK"};
  for (int i = 0; i < 13; ++i) {
    const int col = i % 4, row = i / 4;
    button(keys[i], 360 + col * 205, 235 + row * 68, 185, 54, TFT_DARKGREY);
  }
  button("CANCEL", 360, 515, 385, 58, TFT_MAROON);
  button("SAVE", 795, 515, 385, 58, TFT_DARKGREEN);
}

void set_wifi_editor_viewport() {
  if(g_layout.legacy) {text_editor::set_viewport({0,72,1280,648});return;}
  const int top=g_layout.header.y+g_layout.header.h;
  text_editor::set_viewport({g_layout.viewport.x,top,g_layout.viewport.w,
    g_layout.viewport.y+g_layout.viewport.h-top});
}
void begin_wifi_edit(WifiEdit edit, const char* ssid = nullptr) {
  set_wifi_editor_viewport();
  g_wifi_edit = edit;
  g_wifi_shift = false;
  g_wifi_symbols = false;
  g_wifi_show_password = false;
  if (ssid) strlcpy(g_wifi_edit_ssid, ssid, sizeof(g_wifi_edit_ssid));
  else g_wifi_edit_ssid[0] = '\0';
  g_wifi_edit_password[0] = '\0';
  text_editor::begin(edit == WifiEdit::ssid ? "HIDDEN NETWORK SSID" : "WI-FI PASSWORD",
                     edit == WifiEdit::ssid ? g_wifi_edit_ssid : g_wifi_edit_password,
                     edit == WifiEdit::ssid ? sizeof(g_wifi_edit_ssid) - 1
                                            : sizeof(g_wifi_edit_password) - 1,
                     edit == WifiEdit::password, edit == WifiEdit::ssid ? "NEXT" : "CONNECT");
}

const char* wifi_key_row(int row) {
  static constexpr const char* kNormal[] = {
      "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
  static constexpr const char* kShift[] = {
      "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  static constexpr const char* kSymbols[] = {
      "!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>?/`~", ""};
  return g_wifi_symbols ? kSymbols[row] : (g_wifi_shift ? kShift[row] : kNormal[row]);
}

void draw_wifi_keyboard() {
  set_wifi_editor_viewport();
  if (!text_editor::active())
    text_editor::begin(g_wifi_edit == WifiEdit::ssid ? "HIDDEN NETWORK SSID" : "WI-FI PASSWORD",
                       g_wifi_edit == WifiEdit::ssid ? g_wifi_edit_ssid : g_wifi_edit_password,
                       g_wifi_edit == WifiEdit::ssid ? sizeof(g_wifi_edit_ssid) - 1
                                                      : sizeof(g_wifi_edit_password) - 1,
                       g_wifi_edit == WifiEdit::password,
                       g_wifi_edit == WifiEdit::ssid ? "NEXT" : "CONNECT");
  text_editor::draw();
}

void wifi_connection_feedback() {
  if(!g_layout.legacy) page_index()=0;
  g_state.wifi_connected=false;
  g_state.wifi_connecting=true;
  strlcpy(g_state.wifi_message,"Connecting...",sizeof(g_state.wifi_message));
}
Action queue_wifi_request(const char* ssid, const char* password) {
  if (!ssid || !ssid[0]) return {};
  strlcpy(g_wifi_request_ssid, ssid, sizeof(g_wifi_request_ssid));
  strlcpy(g_wifi_request_password, password ? password : "",
          sizeof(g_wifi_request_password));
  memset(g_wifi_edit_password, 0, sizeof(g_wifi_edit_password));
  g_wifi_request_pending = true;
  g_wifi_edit = WifiEdit::none;
  wifi_connection_feedback();
  restore_settings_content();
  return {ActionKind::connect_wifi, 0};
}

Action handle_wifi_keyboard(int x, int y) {
  const auto result = text_editor::handle_touch(x, y);
  char* entry = g_wifi_edit == WifiEdit::ssid ? g_wifi_edit_ssid : g_wifi_edit_password;
  const size_t capacity = g_wifi_edit == WifiEdit::ssid ? sizeof(g_wifi_edit_ssid)
                                                        : sizeof(g_wifi_edit_password);
  strlcpy(entry, text_editor::value(), capacity);
  if (result == text_editor::Result::cancelled) {
    memset(g_wifi_edit_password, 0, sizeof(g_wifi_edit_password));
    g_wifi_edit = WifiEdit::none;
    restore_settings_content();
    return {};
  }
  if (result == text_editor::Result::accepted) {
    if (g_wifi_edit == WifiEdit::ssid) {
      if (!g_wifi_edit_ssid[0]) return {};
      g_wifi_edit = WifiEdit::password;
      set_wifi_editor_viewport();
      text_editor::begin("WI-FI PASSWORD", g_wifi_edit_password,
                         sizeof(g_wifi_edit_password) - 1, true, "CONNECT");
      draw_wifi_keyboard();
      return {};
    }
    return queue_wifi_request(g_wifi_edit_ssid, g_wifi_edit_password);
  }
  return {};
}

bool accept_location_query();

Action handle_location_keyboard(int x, int y) {
  const auto result = text_editor::handle_touch(x, y);
  strlcpy(g_location_query, text_editor::value(), sizeof(g_location_query));
  if (result == text_editor::Result::cancelled) {
    g_location_edit = false;
    restore_settings_content();
  } else if (result == text_editor::Result::accepted) {
    const bool queued = accept_location_query();
    restore_settings_content();
    if (!queued) return {};
    return {ActionKind::location_query_lookup, 0};
  }
  return {};
}

bool accept_location_query() {
  g_location_edit = false;
  if (!g_location_query[0]) return false;
  g_location_request_pending = true;
  return true;
}

bool valid_coordinate(EditField field, double value) {
  return std::isfinite(value) &&
         (field == EditField::latitude ? value >= -90.0 && value <= 90.0
                                       : value >= -180.0 && value <= 180.0);
}

Action save_coordinate() {
    char* end = nullptr;
    const double value = strtod(g_entry, &end);
    if (end != g_entry && *end == '\0' && valid_coordinate(g_edit, value)) {
      const int32_t e7 = static_cast<int32_t>(llround(value * 10000000.0));
      if (g_edit == EditField::latitude) {
        g_state.latitude_e7 = e7;
        g_latitude_set = true;
      } else {
        g_state.longitude_e7 = e7;
        g_longitude_set = true;
      }
      g_state.location_configured = g_latitude_set && g_longitude_set;
      g_edit = EditField::none;
      restore_settings_content();
      return {ActionKind::location_changed, 0};
    }
  return {};
}

Action handle_keypad(int x, int y) {
  if(!g_layout.legacy) {
    const auto result=text_editor::handle_touch(x,y);
    strlcpy(g_entry,text_editor::value(),sizeof(g_entry));
    if(result==text_editor::Result::cancelled) {
      g_edit=EditField::none;restore_settings_content();return {};
    }
    if(result==text_editor::Result::accepted) {
      const auto action=save_coordinate();
      if(g_edit!=EditField::none) draw_keypad();
      return action;
    }
    return {};
  }
  if (hit(x, y, 360, 515, 385, 58)) {
    g_edit = EditField::none;
    draw_content();
    return {};
  }
  if (hit(x, y, 795, 515, 385, 58)) return save_coordinate();
  static constexpr char keys[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9',
                                   '-', '0', '.', '\b'};
  for (int i = 0; i < 13; ++i) {
    const int col = i % 4, row = i / 4;
    if (!hit(x, y, 360 + col * 205, 235 + row * 68, 185, 54)) continue;
    const size_t length = strlen(g_entry);
    if (keys[i] == '\b') {
      if (length) g_entry[length - 1] = '\0';
    } else if (length + 1 < sizeof(g_entry)) {
      g_entry[length] = keys[i];
      g_entry[length + 1] = '\0';
    }
    draw_keypad();
    return {};
  }
  return {};
}

template <typename T, size_t N>
T next_value(T current, const T (&values)[N]) {
  size_t index = 0;
  while (index < N && values[index] != current) ++index;
  return values[(index + 1) % N];
}

}  // namespace

void enter(const State& state_value, Section section) {
  g_rom_action_pending=false;
  g_state = state_value;
  g_section = section;
  g_edit = EditField::none;
  g_wifi_edit = WifiEdit::none;
  g_location_edit = false;
  g_clock_edit = false;
  g_catalog_cursor.armed = -1;
  g_catalog_cursor.page = 0;
  g_active = true;
  g_latitude_set = state_value.location_configured;
  g_longitude_set = state_value.location_configured;
  draw();
}

void leave() {
  g_rom_action_pending=false;
  accessory_forget_confirmation=false;
  g_active = false;
  g_edit = EditField::none;
  g_wifi_edit = WifiEdit::none;
  g_location_edit = false;
  g_clock_edit = false;
}

void draw() {
  M5.Display.setFont(nullptr);
  M5.Display.setTextSize(1);
  if (g_layout.legacy) M5.Display.fillScreen(kBg);
  else M5.Display.fillRect(g_layout.viewport.x,g_layout.viewport.y,g_layout.viewport.w,g_layout.viewport.h,kBg);
  draw_header();
  draw_rail();
  draw_content();
}

void update(const State& state_value) {
  const bool storage_changed=g_section==Section::storage &&
    (g_state.sd_ready!=state_value.sd_ready || g_state.sd_total_bytes!=state_value.sd_total_bytes ||
     g_state.sd_free_bytes!=state_value.sd_free_bytes || g_state.sd_capacity_known!=state_value.sd_capacity_known);

  const bool header_changed = g_state.wifi_c6_rom_busy!=state_value.wifi_c6_rom_busy || g_state.wifi_power_enabled != state_value.wifi_power_enabled ||
                              g_state.wifi_start_at_boot != state_value.wifi_start_at_boot ||
                              g_state.wifi_external_antenna != state_value.wifi_external_antenna ||
                              g_state.wifi_connected != state_value.wifi_connected ||
                              g_state.wifi_connecting != state_value.wifi_connecting ||
                              g_state.wifi_scanning != state_value.wifi_scanning ||
                              g_state.sound_default != state_value.sound_default ||
                              strcmp(g_state.wifi_ssid, state_value.wifi_ssid) != 0 ||
                              strcmp(g_state.wifi_ip, state_value.wifi_ip) != 0;
  const bool page_changed = g_section == Section::connectivity &&
                            (g_state.wifi_power_enabled != state_value.wifi_power_enabled ||
                             g_state.wifi_start_at_boot != state_value.wifi_start_at_boot ||
                             g_state.wifi_external_antenna != state_value.wifi_external_antenna ||
                             g_state.wifi_scanning != state_value.wifi_scanning ||
                             g_state.wifi_connecting != state_value.wifi_connecting ||
                             g_state.wifi_hosted_update_required != state_value.wifi_hosted_update_required ||
                             g_state.network_count != state_value.network_count ||
                             g_state.saved_network_count != state_value.saved_network_count ||
                             g_state.wifi_rssi != state_value.wifi_rssi ||
                             memcmp(g_state.networks, state_value.networks,
                                    sizeof(g_state.networks)) != 0 ||
                             strcmp(g_state.wifi_message, state_value.wifi_message) != 0 ||
                             strcmp(g_state.wifi_hosted_host_version,
                                    state_value.wifi_hosted_host_version) != 0 ||
                             strcmp(g_state.wifi_hosted_c6_version,
                                    state_value.wifi_hosted_c6_version) != 0 ||
                             memcmp(g_state.profiles, state_value.profiles,
                                    sizeof(g_state.profiles)) != 0);
  const bool rom_transition=rom_firmware_current(g_state)!=rom_firmware_current(state_value) ||
    g_state.wifi_c6_rom_phase!=state_value.wifi_c6_rom_phase ||
    g_state.wifi_c6_rom_busy!=state_value.wifi_c6_rom_busy ||
    g_state.wifi_c6_image_embedded!=state_value.wifi_c6_image_embedded ||
    g_state.wifi_c6_rom_recovery!=state_value.wifi_c6_rom_recovery;
  const bool firmware_changed = g_section == Section::firmware_updates &&
      (rom_transition || g_state.wifi_c6_rom_result!=state_value.wifi_c6_rom_result || g_state.wifi_hosted_transport_ready != state_value.wifi_hosted_transport_ready ||
       g_state.wifi_c6_image_embedded != state_value.wifi_c6_image_embedded ||
       g_state.wifi_c6_update_percent != state_value.wifi_c6_update_percent ||
       strcmp(g_state.wifi_hosted_host_version, state_value.wifi_hosted_host_version) != 0 ||
       strcmp(g_state.wifi_hosted_c6_version, state_value.wifi_hosted_c6_version) != 0 ||
       strcmp(g_state.wifi_c6_update_state, state_value.wifi_c6_update_state) != 0 ||
       strcmp(g_state.wifi_c6_update_stage, state_value.wifi_c6_update_stage) != 0);
  const bool power_changed = g_section == Section::system &&
                             (g_state.battery_level != state_value.battery_level ||
                              g_state.battery_mv != state_value.battery_mv ||
                              g_state.battery_current_ma != state_value.battery_current_ma ||
                              g_state.vbus_mv != state_value.vbus_mv ||
                              strcmp(g_state.charging_state, state_value.charging_state) != 0);
  const bool clock_changed = g_section == Section::system &&
                             (g_state.rtc_valid != state_value.rtc_valid ||
                              strcmp(g_state.rtc_utc, state_value.rtc_utc) != 0 ||
                              strcmp(g_state.rtc_local, state_value.rtc_local) != 0 ||
                              g_state.utc_offset_minutes != state_value.utc_offset_minutes);
  const bool ntp_changed = g_state.ntp_state != state_value.ntp_state;
  if (ntp_changed) g_ntp_tapped = false;
  const bool rtc_flipped = g_state.rtc_valid != state_value.rtc_valid;
  const bool catalog_inventory_changed =
      (g_state.catalog_ready != state_value.catalog_ready || g_state.catalog_busy != state_value.catalog_busy ||
       g_state.catalog_progress_percent != state_value.catalog_progress_percent ||
       strcmp(g_state.catalog_message, state_value.catalog_message) != 0 ||
       strcmp(g_state.catalog_date, state_value.catalog_date) != 0 ||
       memcmp(g_state.catalog_packs, state_value.catalog_packs, sizeof(g_state.catalog_packs)) != 0);
  const bool catalog_changed = g_section == Section::data_maps && catalog_inventory_changed;
  const bool location_changed = g_section == Section::location_adsb &&
      (g_state.location_configured != state_value.location_configured ||
       g_state.latitude_e7 != state_value.latitude_e7 ||
       g_state.longitude_e7 != state_value.longitude_e7 ||
       strcmp(g_state.location_label,state_value.location_label) != 0 ||
       strcmp(g_state.map_pack,state_value.map_pack) != 0 ||
       g_state.ip_location_busy != state_value.ip_location_busy ||
       g_state.ip_location_ready != state_value.ip_location_ready ||
       g_state.ip_latitude_e7 != state_value.ip_latitude_e7 ||
       g_state.ip_longitude_e7 != state_value.ip_longitude_e7 ||
       strcmp(g_state.ip_location_label, state_value.ip_location_label) != 0 ||
       strcmp(g_state.ip_location_message, state_value.ip_location_message) != 0);
  const bool companion_changed = g_section == Section::companion &&
      (g_state.web_console_enabled != state_value.web_console_enabled ||
       g_state.web_console_listening != state_value.web_console_listening ||
       strcmp(g_state.web_console_url, state_value.web_console_url) != 0 ||
       g_state.orcdial_pairing != state_value.orcdial_pairing ||
       g_state.orcdial_connected != state_value.orcdial_connected ||
       g_state.orcdial_verifying != state_value.orcdial_verifying ||
       g_state.orcdial_boot_connect != state_value.orcdial_boot_connect ||
       g_state.orcdial_upgrade != state_value.orcdial_upgrade ||
       g_state.orcdial_code != state_value.orcdial_code ||
       strcmp(g_state.orcdial_connection,state_value.orcdial_connection) ||
       strcmp(g_state.orcdial_failure,state_value.orcdial_failure) ||
       g_state.orcdial_paired != state_value.orcdial_paired ||
       g_state.orcdial_bridge_ready != state_value.orcdial_bridge_ready);
  const bool wifi_structure = g_state.wifi_hosted_update_required != state_value.wifi_hosted_update_required ||
      (g_state.saved_network_count==0)!=(state_value.saved_network_count==0) ||
      (g_state.network_count==0)!=(state_value.network_count==0);
  const bool wifi_buttons = g_state.wifi_power_enabled!=state_value.wifi_power_enabled ||
      g_state.wifi_start_at_boot!=state_value.wifi_start_at_boot ||
      g_state.wifi_external_antenna!=state_value.wifi_external_antenna ||
      g_state.wifi_scanning!=state_value.wifi_scanning;
  unsigned wifi_saved_mask=0,wifi_network_mask=0;
  for(int i=0;i<4;++i) if(memcmp(&g_state.profiles[i],&state_value.profiles[i],sizeof(g_state.profiles[i])) ||
      g_state.saved_network_count!=state_value.saved_network_count || g_state.wifi_power_enabled!=state_value.wifi_power_enabled) wifi_saved_mask|=1u<<i;
  for(int i=0;i<6;++i) if(memcmp(&g_state.networks[i],&state_value.networks[i],sizeof(g_state.networks[i])) ||
      g_state.network_count!=state_value.network_count || g_state.wifi_power_enabled!=state_value.wifi_power_enabled) wifi_network_mask|=1u<<i;
  const bool catalog_entries_changed=memcmp(g_state.catalog_packs,state_value.catalog_packs,sizeof(g_state.catalog_packs))!=0;
  if (catalog_entries_changed) g_catalog_cursor.catalog_changed(state_value.catalog_packs);
  const bool open_recovery=state_value.wifi_c6_rom_busy && g_section!=Section::firmware_updates &&
      g_edit==EditField::none && g_wifi_edit==WifiEdit::none && !g_location_edit && !g_clock_edit;
  if(rom_transition) g_rom_action_pending=false;
  g_state = state_value;
  if(open_recovery) {g_section=Section::firmware_updates;page_index()=0;draw();return;}
  if(g_edit==EditField::none&&g_state.location_configured)g_latitude_set=g_longitude_set=true;
  if (g_clock_edit) {
    // Keep the editor live: a finished NTP sync or a newly valid RTC refreshes untouched fields.
    const bool reload_clock = !g_clock_dirty &&
        ((ntp_changed && g_state.ntp_state == 2) || rtc_flipped);
    if (reload_clock) clock_load_fields();
    if (g_layout.legacy) {
      if (ntp_changed || rtc_flipped || header_changed) draw_clock_editor();
    } else {
      if (reload_clock) {
        for (int i = 0; i < 5; ++i) draw_compact_clock_field_value(i);
        draw_compact_clock_offset_value();
        draw_compact_clock_preview();
      }
      if (ntp_changed) {
        draw_compact_clock_note();
        draw_compact_clock_ntp_button();
      }
    }
    if (header_changed) draw_header();
    return;
  }
  if (header_changed) draw_header();
  if ((page_changed || (g_section==Section::connectivity && header_changed)) &&
      g_edit==EditField::none && g_wifi_edit==WifiEdit::none && !g_location_edit) {
    if(g_layout.legacy || wifi_structure) draw_content();
    else if(!g_state.wifi_hosted_update_required) {
      if(page_index()==0) {draw_wifi_status();if(wifi_buttons) draw_wifi_primary();}
      else if(page_index()==1) {for(int i=0;i<4;++i) if(wifi_saved_mask&(1u<<i)) draw_wifi_saved(i);}
      else {for(int i=0;i<6;++i) if(wifi_network_mask&(1u<<i)) draw_wifi_network(i);}
    }
  }
  if (firmware_changed && g_edit == EditField::none && g_wifi_edit == WifiEdit::none) {
    if(g_state.wifi_c6_rom_recovery) {if(rom_transition) draw_rom_update();else draw_rom_status();}
    else if (!g_layout.legacy && page_index()==0) draw_compact_firmware_status();
    else draw_content();
  }
  if (power_changed && g_edit == EditField::none && g_wifi_edit == WifiEdit::none)
    draw_system_power();
  if (clock_changed && g_edit == EditField::none && g_wifi_edit == WifiEdit::none) {
    if(g_layout.legacy) draw_content();else draw_compact_system_clock_values();
  }
  if (catalog_changed && g_edit == EditField::none && g_wifi_edit == WifiEdit::none) {
    if(!g_layout.legacy && page_index()==0) draw_compact_catalog_status();
    else draw_content();
  }
  if (location_changed && g_edit == EditField::none && g_wifi_edit == WifiEdit::none && !g_location_edit)
    draw_content();
  if(storage_changed && g_edit==EditField::none && g_wifi_edit==WifiEdit::none) draw_storage_status();
  if (companion_changed && g_edit == EditField::none && g_wifi_edit == WifiEdit::none)
    draw_content();
}

bool begin_clock_edit() {
  if(!g_active||g_section!=Section::system)return false;
  start_clock_edit();return true;
}
Action begin_location_method(int method) {
  if(method==0)return {ActionKind::location_ip_lookup,0};
  if(method==1) {
    g_location_edit=true;
    set_wifi_editor_viewport();
    text_editor::begin("ZIP CODE OR ADDRESS",g_location_query,sizeof(g_location_query)-1,false,"LOOK UP");
    text_editor::draw();
  } else if(method==2) {start_edit(EditField::latitude);draw_keypad();}
  return {};
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if(g_section==Section::firmware_updates && g_state.wifi_c6_rom_recovery) {
    if(g_rom_action_pending) return {};
    const auto v=c6_recovery_ui::view(g_state.wifi_c6_rom_phase,g_state.wifi_c6_rom_busy,g_state.wifi_c6_image_embedded,rom_firmware_current(g_state));
    if(v.cancel && hit(x,y,g_layout.firmware_cancel())) {
      g_rom_action_pending=true;
      wifi_button("CANCELING...",g_layout.firmware_cancel(),TFT_DARKGREY,false);
      return {ActionKind::c6_rom_cancel,0};
    }
    if(hit(x,y,g_layout.firmware_action(v.cancel))) {
      using Request=c6_recovery_ui::Request;
      switch(v.request) {
        case Request::begin:
          g_state.wifi_c6_update_percent=0;g_state.wifi_c6_rom_result=0;
          g_state.wifi_c6_rom_busy=true;g_state.wifi_c6_rom_phase=c6_recovery_ui::Phase::quiescing;
          draw_header();draw_rom_update();return {ActionKind::c6_rom_begin,0};
        case Request::install:
          g_rom_action_pending=true;
          wifi_button("INSTALLING...",g_layout.firmware_action(v.cancel),TFT_DARKGREY,false);
          return {ActionKind::c6_rom_release_install,0};
        case Request::resume:
          g_rom_action_pending=true;
          wifi_button("RESUMING...",g_layout.firmware_action(),TFT_DARKGREY,false);
          return {ActionKind::c6_rom_release_resume,0};
        case Request::cancel:
          g_rom_action_pending=true;
          wifi_button("CANCELING...",g_layout.firmware_action(),TFT_DARKGREY,false);
          return {ActionKind::c6_rom_cancel,0};
        default: return {};
      }
    }
    if(g_state.wifi_c6_rom_busy) return {};
  }
  if (!g_layout.legacy && !geometry_locked()) {
    if (hit(x,y,g_layout.close)) {g_active=false;return {ActionKind::close,0};}
    if (hit(x,y,g_layout.previous_section) || hit(x,y,g_layout.next_section)) {
      const int direction=hit(x,y,g_layout.previous_section)?-1:1;
      const int count=static_cast<int>(Section::count);
      for (int i=1;i<=count;++i) {
        const auto next=static_cast<Section>((static_cast<int>(g_section)+direction*i+count)%count);
        if (supports_section(next)) {g_section=next;draw_rail();draw_content();break;}
      }
      return {};
    }
    if (g_section==Section::location_adsb || g_section==Section::data_maps || g_section==Section::connectivity || g_section==Section::display_audio || g_section==Section::firmware_updates ||
        (g_section==Section::companion && page_count()>1) ||
        (g_section==Section::system && page_count()>1)) {
      const auto pages=page_count();
      if (hit(x,y,g_layout.previous_page) && page_index()>0) {--page_index();g_catalog_cursor.armed=-1;draw_content();return {};}
      if (hit(x,y,g_layout.next_page) && page_index()+1<pages) {++page_index();g_catalog_cursor.armed=-1;draw_content();return {};}
      if (g_section==Section::display_audio) return handle_display_touch(x,y);
      if (g_section==Section::firmware_updates && page_index()==1 && hit(x,y,g_layout.firmware_action()) && g_state.wifi_c6_image_embedded) {
        if (strcmp(g_state.wifi_c6_update_state,"ready")==0 || strcmp(g_state.wifi_c6_update_state,"optional")==0)
          return {ActionKind::c6_update_confirm,0};
        if (strcmp(g_state.wifi_c6_update_state,"current")==0) return {ActionKind::orcdial_c6_update,0};
      }
      if (g_section!=Section::location_adsb && g_section!=Section::data_maps && g_section!=Section::connectivity && g_section!=Section::companion && g_section!=Section::system) return {};
    }
  }
  // The full-width text editor covers the rail; repaint it once it closes.
  if (g_location_edit) {
    const Action action = handle_location_keyboard(x, y);
    if (!g_location_edit) draw_rail();
    return action;
  }
  if (g_wifi_edit != WifiEdit::none) {
    const Action action = handle_wifi_keyboard(x, y);
    if (g_wifi_edit == WifiEdit::none) draw_rail();
    return action;
  }
  if (g_edit != EditField::none) return handle_keypad(x, y);
  if (g_clock_edit) {
    if (!g_layout.legacy && hit(x, y, g_layout.close)) {
      g_clock_edit = false;
      g_active = false;
      return {ActionKind::close, 0};
    }
    if (!g_layout.legacy) return handle_clock_editor(x, y);
    if (g_layout.legacy && hit(x, y, 720, 13, 126, 46)) {   // CLOSE
      g_clock_edit = false;
      g_active = false;
      return {ActionKind::close, 0};
    }
    if (x < kRailW && y >= kRailY) g_clock_edit = false;   // leaving through the rail abandons the edit
    else return handle_clock_editor(x, y);
  }
  if (g_section == Section::system && !g_layout.legacy) {
    const auto system_layout = make_system_layout(g_layout.content, page_index());
    if (hit(x, y, system_layout.set_clock)) {
      start_clock_edit();
      return {};
    }
    if (hit(x, y, system_layout.run_setup)) return {ActionKind::run_setup, 0};
    return {};
  }
  if (g_layout.legacy && hit(x, y, 720, 13, 126, 46)) {
    g_active = false;
    return {ActionKind::close, 0};
  }
  if (g_layout.legacy && x < kRailW && y >= kRailY) {
    const int index = (y - kRailY) / kRailRowH;
    if (index >= 0 && index < static_cast<int>(Section::count)) {
      g_section = static_cast<Section>(index);
      draw_rail();
      draw_content();
    }
    return {};
  }
  if (g_section == Section::system && hit(x, y, 1000, 100, 218, 46)) {
    start_clock_edit();
    return {};
  }
  if(g_section==Section::system&&hit(x,y,720,100,240,46))return {ActionKind::run_setup,0};
#if ORCSDR_ORCDIAL
  if(g_layout.legacy && g_section==Section::companion) {
    if(hit(x,y,330,540,280,48)) {
      if(accessory_forget_confirmation){accessory_forget_confirmation=false;draw_content();return {};}
      if(g_state.orcdial_verifying)return {ActionKind::orcdial_confirm,int32_t(g_state.orcdial_code)};
      if(!strcmp(g_state.orcdial_failure,"Storage failed"))return {ActionKind::orcdial_forget,0};
      return {g_state.orcdial_pairing?ActionKind::orcdial_cancel:g_state.orcdial_connected?ActionKind::orcdial_disconnect:g_state.orcdial_paired?ActionKind::orcdial_connect:ActionKind::orcdial_pair,0};
    }
    if(hit(x,y,640,540,330,48)) {
      if(accessory_forget_confirmation){accessory_forget_confirmation=false;return {ActionKind::orcdial_forget,1};}
      if(g_state.orcdial_verifying)return {ActionKind::orcdial_cancel,0};
      if(g_state.orcdial_paired || !strcmp(g_state.orcdial_failure,"Storage failed")){accessory_forget_confirmation=true;draw_content();}return {};
    }
    if(g_state.orcdial_paired&&hit(x,y,640,606,330,48))return {ActionKind::orcdial_boot,g_state.orcdial_boot_connect?0:1};
  }
#endif
  if(!g_layout.legacy && g_section==Section::location_adsb) {
    const SettingsFormLayout l{g_layout.content};
    if(page_index()==0) {
      if(hit(x,y,l.coordinate(false))) {start_edit(EditField::latitude);draw_keypad();}
      else if(hit(x,y,l.coordinate(true))) {start_edit(EditField::longitude);draw_keypad();}
      else if(hit(x,y,l.location_map())) return {ActionKind::open_location_picker,0};
    } else if(page_index()==2) {
      if(hit(x,y,l.half(28)) && !g_state.ip_location_busy) {
        g_location_edit=true;set_wifi_editor_viewport();text_editor::begin("ZIP CODE OR ADDRESS",g_location_query,sizeof(g_location_query)-1,false,"LOOK UP");text_editor::draw();
      } else if(hit(x,y,l.half(28,true)) && !g_state.ip_location_busy) return {ActionKind::location_ip_lookup,0};
      else if(hit(x,y,l.lookup_result()) && g_state.ip_location_ready) {
        g_state.latitude_e7=g_state.ip_latitude_e7;g_state.longitude_e7=g_state.ip_longitude_e7;
        strlcpy(g_state.location_label,g_state.ip_location_label,sizeof(g_state.location_label));
        g_state.location_configured=true;g_latitude_set=g_longitude_set=true;draw_content();return {ActionKind::location_ip_confirm,0};
      }
    }
    return {};
  }
  if(!g_layout.legacy && g_section==Section::data_maps) {
    const SettingsFormLayout l{g_layout.content};const int index=compact_pack_index();
    if(index<0) {
      if(hit(x,y,l.catalog_check()) && !g_state.catalog_busy) return {ActionKind::catalog_check,0};
      if(hit(x,y,l.catalog_local())) return {ActionKind::open_local_maps,0};
    } else {
      const auto rank=page_index()-1;g_catalog_cursor.page=rank/5;
      if(hit(x,y,l.half(144))) {
        const int selected=g_catalog_cursor.install(g_state.catalog_packs,rank%5,g_state.catalog_ready,g_state.catalog_busy);
        if(selected>=0) return {ActionKind::catalog_install,selected};
      } else if(hit(x,y,l.half(144,true))) {
        const int selected=g_catalog_cursor.remove(g_state.catalog_packs,rank%5,g_state.catalog_busy);
        if(selected>=0) return {ActionKind::catalog_remove,selected};
        draw_content();
      }
    }
    return {};
  }
  if (g_section == Section::connectivity) {
    if (g_state.wifi_hosted_update_required) return {};
    const auto c=make_connectivity_layout(g_layout.content);
    const bool compact=!g_layout.legacy;
    const auto primary=[&](Rect r,int lx,int ly,int lw,int lh) {return compact ? page_index()==0 && hit(x,y,r) : hit(x,y,lx,ly,lw,lh);};
    if (primary(c.power,938,198,280,48))
      return {ActionKind::wifi_power_changed, g_state.wifi_power_enabled ? 0 : 1};
    if (primary(c.boot,930,262,288,48))
      return {ActionKind::wifi_start_at_boot_changed, g_state.wifi_start_at_boot ? 0 : 1};
    if (primary(c.antenna,930,326,288,48))
      return {ActionKind::wifi_antenna_changed, g_state.wifi_external_antenna ? 0 : 1};
    if (!g_state.wifi_power_enabled) return {};
    if (primary(c.scan,330,198,280,48) && !g_state.wifi_scanning)
      return {ActionKind::scan_wifi, 0};
    if (primary(c.hidden,634,198,280,48)) {
      begin_wifi_edit(WifiEdit::ssid);
      draw_wifi_keyboard();
      return {};
    }
    for (uint8_t i = 0; i < g_state.saved_network_count && i < 4; ++i) {
      const int row_y = 410 + i * 44;
      if ((compact ? page_index()==1 && hit(x,y,c.saved[i].connect) : hit(x, y, 674, row_y, 144, 36))) {
        wifi_connection_feedback();draw_content();
        return {ActionKind::connect_saved_wifi, i};
      }
      if (i && (compact ? page_index()==1 && hit(x,y,c.saved[i].up) : hit(x, y, 830, row_y, 88, 36)))
        return {ActionKind::move_wifi_up, i};
      if (i + 1 < g_state.saved_network_count && (compact ? page_index()==1 && hit(x,y,c.saved[i].down) : hit(x, y, 930, row_y, 106, 36)))
        return {ActionKind::move_wifi_down, i};
      if ((compact ? page_index()==1 && hit(x,y,c.saved[i].forget) : hit(x, y, 1048, row_y, 170, 36)))
        return {ActionKind::forget_wifi, i};
    }
    for (uint8_t i = 0; i < std::min<uint8_t>(g_state.network_count, 6); ++i) {
      const int row_x = 330 + (i % 2) * 428;
      const int row_y = 624 + (i / 2) * 30;
      if (!(compact ? page_index()==2 && hit(x,y,c.networks[i]) : hit(x, y, row_x, row_y, 418, 28))) continue;
      if (g_state.networks[i].saved) {
        for (uint8_t saved = 0; saved < g_state.saved_network_count; ++saved)
          if (strcmp(g_state.networks[i].ssid, g_state.profiles[saved].ssid) == 0)
            return {ActionKind::connect_saved_wifi, saved};
        return {};
      }
      if (!g_state.networks[i].secure)
        return queue_wifi_request(g_state.networks[i].ssid, "");
      begin_wifi_edit(WifiEdit::password, g_state.networks[i].ssid);
      draw_wifi_keyboard();
      return {};
    }
  } else if (g_section == Section::location_adsb) {
    if(hit(x,y,330,350,888,68))return {ActionKind::open_location_picker,0};
    if (hit(x, y, 820, 204, 398, 54)) { start_edit(EditField::latitude); draw_keypad(); }
    else if (hit(x, y, 820, 274, 398, 54)) { start_edit(EditField::longitude); draw_keypad(); }
    if (hit(x, y, 330, 575, 250, 46) && !g_state.ip_location_busy) {
      g_location_edit = true;
      set_wifi_editor_viewport();
      set_wifi_editor_viewport();
    text_editor::begin("ZIP CODE OR ADDRESS", g_location_query,
                         sizeof(g_location_query) - 1, false, "LOOK UP");
      text_editor::draw();
      return {};
    }
    if (hit(x, y, 600, 575, 180, 46) && !g_state.ip_location_busy)
      return {ActionKind::location_ip_lookup, 0};
    if (g_state.ip_location_ready && hit(x, y, 800, 575, 240, 46)) {
      g_state.latitude_e7 = g_state.ip_latitude_e7; g_state.longitude_e7 = g_state.ip_longitude_e7;
      strlcpy(g_state.location_label, g_state.ip_location_label, sizeof(g_state.location_label));
      g_state.location_configured = true; g_latitude_set = g_longitude_set = true;
      draw_content();
      return {ActionKind::location_ip_confirm, 0};
    }
  } else if (g_section == Section::data_maps) {
    if(hit(x,y,1000,665,218,36))return {ActionKind::open_local_maps,0};
    if (hit(x, y, 940, 126, 278, 48) && !g_state.catalog_busy)
      return {ActionKind::catalog_check, 0};
    const size_t pages = orcsdr::map_catalog_paging::page_count(g_state.catalog_packs);
    if (hit(x, y, 650, 137, 120, 42) && g_catalog_cursor.page > 0) {
      (void)g_catalog_cursor.turn(g_state.catalog_packs, -1);
      draw_content();
      return {};
    }
    if (hit(x, y, 787, 137, 120, 42) && g_catalog_cursor.page + 1 < pages) {
      (void)g_catalog_cursor.turn(g_state.catalog_packs, 1);
      draw_content();
      return {};
    }
    for (size_t row = 0; row < orcsdr::map_catalog_paging::kRowsPerPage; ++row) {
      const int pack_index = orcsdr::map_catalog_paging::index_for_row(
          g_state.catalog_packs, g_catalog_cursor.page, row);
      if (pack_index < 0) continue;
      const int row_y = 185 + static_cast<int>(row) * 96;
      if (hit(x, y, 930, row_y + 14, 132, 42)) {
        const int selected = g_catalog_cursor.install(g_state.catalog_packs, row,
                                                     g_state.catalog_ready, g_state.catalog_busy);
        if (selected >= 0) return {ActionKind::catalog_install, selected};
      }
      if (hit(x, y, 1072, row_y + 14, 126, 42) &&
          g_state.catalog_packs[pack_index].installed && !g_state.catalog_busy) {
        const int selected = g_catalog_cursor.remove(g_state.catalog_packs, row, g_state.catalog_busy);
        if (selected >= 0) return {ActionKind::catalog_remove, selected};
        draw_content();
        return {};
      }
    }
  } else if (g_section == Section::display_audio) {
    return handle_display_touch(x,y);
  } else if (g_section == Section::radio_defaults) {
    if (!g_layout.legacy) {
      const auto layout = make_radio_defaults_layout(g_layout.content,
                                                     g_state.rtl_usb_safe_mode);
      if (g_state.rtl_usb_safe_mode) {
        if (hit(x, y, layout.rows[5].action))
          return {ActionKind::rtl_usb_safe_mode_reset, 0};
        return {};
      }
      if (hit(x, y, layout.rows[1].action)) {
        g_state.auto_start_reception = !g_state.auto_start_reception;
        draw_content();
        return {ActionKind::auto_start_changed, g_state.auto_start_reception};
      }
      if (hit(x, y, layout.rows[4].action)) {
        g_state.graphics_default = !g_state.graphics_default;
        draw_content();
        return {ActionKind::graphics_changed, g_state.graphics_default};
      }
      return {};
    }
    if (g_state.rtl_usb_safe_mode) {
      if (hit(x, y, 330, 515, 280, 50))
        return {ActionKind::rtl_usb_safe_mode_reset, 0};
      return {};
    }
    if (hit(x, y, 330, 500, 260, 50)) {
      g_state.auto_start_reception = !g_state.auto_start_reception;
      draw_content();
      return {ActionKind::auto_start_changed, g_state.auto_start_reception};
    }
    if (hit(x, y, 620, 500, 240, 50)) {
      g_state.graphics_default = !g_state.graphics_default;
      draw_content();
      return {ActionKind::graphics_changed, g_state.graphics_default};
    }
  } else if (g_section == Section::companion) {
    if (!g_layout.legacy) {
      const bool shallow = g_layout.content.h < 320;
#if ORCSDR_ORCDIAL
      const bool include_dial = true;
      const bool dial_page = shallow && page_index() == 1;
#else
      const bool include_dial = false;
      const bool dial_page = false;
#endif
      const auto layout = make_companion_layout(g_layout.content, dial_page,
                                                 include_dial);
      if (!dial_page && hit(x, y, layout.web_action))
        return {ActionKind::web_console_changed,
                g_state.web_console_enabled ? 0 : 1};
#if ORCSDR_ORCDIAL
      if (include_dial && (!shallow || dial_page)) {
        if (hit(x, y, layout.primary_action)) {
          if (accessory_forget_confirmation) {
            accessory_forget_confirmation = false;
            draw_content();
            return {};
          }
          if (g_state.orcdial_verifying)
            return {ActionKind::orcdial_confirm, int32_t(g_state.orcdial_code)};
          if (!strcmp(g_state.orcdial_failure, "Storage failed"))
            return {ActionKind::orcdial_forget, 0};
          return {g_state.orcdial_pairing ? ActionKind::orcdial_cancel
                  : g_state.orcdial_connected ? ActionKind::orcdial_disconnect
                  : g_state.orcdial_paired ? ActionKind::orcdial_connect
                                           : ActionKind::orcdial_pair, 0};
        }
        if (layout.secondary_action.w && hit(x, y, layout.secondary_action)) {
          if (accessory_forget_confirmation)
            return {ActionKind::orcdial_forget, 1};
          if (g_state.orcdial_verifying) return {ActionKind::orcdial_cancel, 0};
          if (g_state.orcdial_paired ||
              !strcmp(g_state.orcdial_failure, "Storage failed")) {
            accessory_forget_confirmation = true;
            draw_content();
          }
          return {};
        }
        if (g_state.orcdial_paired && layout.boot_action.w &&
            hit(x, y, layout.boot_action))
          return {ActionKind::orcdial_boot,
                  g_state.orcdial_boot_connect ? 0 : 1};
      }
#endif
      return {};
    }
    if (hit(x, y, 960, 150, 170, 48))
      return {ActionKind::web_console_changed, g_state.web_console_enabled ? 0 : 1};
  }
  if (g_section == Section::firmware_updates) {
    if ((strcmp(g_state.wifi_c6_update_state, "ready") == 0 ||
         strcmp(g_state.wifi_c6_update_state, "optional") == 0) &&
        hit(x, y, 330, 445, 360, 58)) return {ActionKind::c6_update_confirm, 0};
    if (strcmp(g_state.wifi_c6_update_state, "current") == 0 &&
        g_state.wifi_c6_image_embedded && hit(x, y, 330, 445, 390, 58))
      return {ActionKind::orcdial_c6_update, 0};
    return {};
  }
  return {};
}

bool active() { return g_active; }
bool geometry_locked() {
  return g_active && (g_state.wifi_c6_rom_busy || g_edit!=EditField::none || g_wifi_edit!=WifiEdit::none || g_location_edit || g_clock_edit);
}
bool supports_section(Section value) {
  return value<Section::count;
}
bool set_viewport(Rect viewport) {
  if (geometry_locked()) return false;
  const auto next=make_settings_layout(viewport);
  if (!next.valid) return false;
  if (!audio_header::set_viewport(viewport)) return false;
  g_layout=next;
  focus_nav::set_viewport(M5.Display.width(),M5.Display.height());
  focus_nav::clear();
  return true;
}
const SettingsLayout& layout() {return g_layout;}
std::size_t content_page() {return page_index();}
const State& state() { return g_state; }
Section section() { return g_section; }

void show_documentation_section(Section section, const State& state_value,
                                bool show_wifi_keyboard) {
  if (section >= Section::count) return;
  g_rom_action_pending=false;
  g_state = state_value;
  g_section = section;
  g_edit = EditField::none;
  g_wifi_edit = WifiEdit::none;
  g_location_edit = false;
  g_clock_edit = false;
  g_active = true;
  if (show_wifi_keyboard) {
    begin_wifi_edit(WifiEdit::password, "Demo Network");
    strlcpy(g_wifi_edit_password, "example-password",
            sizeof(g_wifi_edit_password));
  }
  draw();
  if (show_wifi_keyboard) draw_wifi_keyboard();
}

bool take_wifi_credentials(char* ssid, size_t ssid_size,
                           char* password, size_t password_size) {
  if (!g_wifi_request_pending || !ssid || !password || ssid_size == 0 ||
      password_size == 0) return false;
  strlcpy(ssid, g_wifi_request_ssid, ssid_size);
  strlcpy(password, g_wifi_request_password, password_size);
  memset(g_wifi_request_password, 0, sizeof(g_wifi_request_password));
  g_wifi_request_ssid[0] = '\0';
  g_wifi_request_pending = false;
  return true;
}

bool take_location_query(char* query, size_t query_size) {
  if (!g_location_request_pending || !query || query_size == 0) return false;
  strlcpy(query, g_location_query, query_size);
  g_location_request_pending = false;
  return query[0] != '\0';
}

bool self_check() {
  if (!valid_coordinate(EditField::latitude, 90.0) ||
      valid_coordinate(EditField::latitude, 90.00001) ||
      !valid_coordinate(EditField::longitude, -180.0) ||
      valid_coordinate(EditField::longitude, -180.00001)) return false;
  if (next_value<uint16_t>(0, kTimeouts) != 30) return false;
  if (strlen(wifi_key_row(0)) != 10 || strlen(wifi_key_row(2)) != 9) return false;
  const bool saved_symbols = g_wifi_symbols;
  g_wifi_symbols = true;
  const bool symbols_ok = strchr(wifi_key_row(1), '\\') &&
                          strchr(wifi_key_row(2), '~');
  g_wifi_symbols = saved_symbols;
  char ssid[33]{}, password[64]{};
  strlcpy(g_wifi_request_ssid, "SELF-CHECK", sizeof(g_wifi_request_ssid));
  strlcpy(g_wifi_request_password, "not-a-real-password",
          sizeof(g_wifi_request_password));
  g_wifi_request_pending = true;
  const bool handoff_ok = take_wifi_credentials(
      ssid, sizeof(ssid), password, sizeof(password));
  const bool credentials_ok = handoff_ok && strcmp(ssid, "SELF-CHECK") == 0 &&
                              strcmp(password, "not-a-real-password") == 0 &&
                              g_wifi_request_password[0] == '\0';
  memset(password, 0, sizeof(password));
  const bool saved_location_edit = g_location_edit;
  const bool saved_location_pending = g_location_request_pending;
  char saved_location_query[sizeof(g_location_query)]{};
  strlcpy(saved_location_query, g_location_query, sizeof(saved_location_query));
  g_location_edit = true;
  g_location_request_pending = false;
  g_location_query[0] = '\0';
  const bool empty_location_ok = !accept_location_query() && !g_location_edit &&
                                 !g_location_request_pending;
  strlcpy(g_location_query, "97401", sizeof(g_location_query));
  const bool location_queued = accept_location_query();
  char query[64]{};
  const bool location_ok = location_queued && take_location_query(query, sizeof(query)) &&
                           strcmp(query, "97401") == 0;
  g_location_edit = saved_location_edit;
  g_location_request_pending = saved_location_pending;
  strlcpy(g_location_query, saved_location_query, sizeof(g_location_query));
  // Catalog now exposes all 16 slots. Keep the self-check's full-state
  // backup in PSRAM, not on the main task's constrained stack.
  static EXT_RAM_BSS_ATTR State saved_state;
  saved_state = g_state;
  const Section saved_section = g_section;
  const bool saved_active = g_active;
  g_active = true;
  g_section = Section::firmware_updates;
  strlcpy(g_state.wifi_c6_update_state, "ready", sizeof(g_state.wifi_c6_update_state));
  const bool c6_ready_routes = handle_touch(331, 446).kind == ActionKind::c6_update_confirm;
  strlcpy(g_state.wifi_c6_update_state, "optional", sizeof(g_state.wifi_c6_update_state));
  const bool c6_optional_routes = handle_touch(331, 446).kind == ActionKind::c6_update_confirm;
  strlcpy(g_state.wifi_c6_update_state, "current", sizeof(g_state.wifi_c6_update_state));
  g_state.wifi_c6_image_embedded = true;
  const bool c6_current_routes = handle_touch(331, 446).kind == ActionKind::orcdial_c6_update;
  g_section = Section::companion;
#if ORCSDR_ORCDIAL
  const bool accessory_routes = handle_touch(450, 560).kind == ActionKind::orcdial_pair;
#else
  const bool accessory_routes = handle_touch(950, 425).kind == ActionKind::none;
#endif
  g_section = Section::connectivity;
  g_state.wifi_hosted_update_required = false;
  const bool wifi_boot_routes = handle_touch(950, 286).kind == ActionKind::wifi_start_at_boot_changed;
  const bool wifi_antenna_routes = handle_touch(950, 350).kind == ActionKind::wifi_antenna_changed;
  const bool wifi_gap_clear = handle_touch(950, 318).kind == ActionKind::none;
  g_section = Section::radio_defaults;
  g_state.rtl_usb_safe_mode = true;
  const bool usb_recovery_routes =
      handle_touch(331, 516).kind == ActionKind::rtl_usb_safe_mode_reset;
  g_state = saved_state;
  g_section = saved_section;
  g_active = saved_active;
  if (!symbols_ok || !credentials_ok || !empty_location_ok || !location_ok ||
      !c6_ready_routes || !c6_optional_routes || !c6_current_routes || !usb_recovery_routes ||
      !accessory_routes || !wifi_boot_routes || !wifi_antenna_routes || !wifi_gap_clear) return false;
  return true;
}

}  // namespace orcsdr::settings
