#pragma once

#include <cstddef>
#include <cstdint>

#include "map_packs.hpp"

namespace orcsdr::settings {

enum class Section : uint8_t {
  connectivity,
  firmware_updates,
  location_adsb,
  data_maps,
  display_audio,
  radio_defaults,
  storage,
  companion,
  system,
  count
};

struct WifiNetwork {
  char ssid[33]{};
  int16_t rssi = 0;
  bool secure = false;
  bool saved = false;
};

struct WifiProfileView {
  char ssid[33]{};
  bool connected = false;
};

struct CatalogPackView {
  char id[20]{};
  char title[28]{};
  char version[16]{};
  char source_date[16]{};
  char status[24]{};
  uint32_t runtime_bytes = 0;
  uint32_t archive_bytes = 0;
  bool installed = false;
  bool update_available = false;
};

struct State {
  bool wifi_power_enabled = true;
  bool wifi_start_at_boot = false;
  bool wifi_external_antenna = false;
  bool wifi_ready = false;
  bool wifi_scanning = false;
  bool wifi_connected = false;
  bool wifi_connecting = false;
  bool wifi_hosted_update_required = false;
  bool wifi_hosted_transport_ready = false;
  bool orcdial_bridge_ready = false;
  bool orcdial_paired = false;
  bool orcdial_pairing = false;
  bool orcdial_connected=false,orcdial_verifying=false,orcdial_boot_connect=false,orcdial_upgrade=false;
  uint32_t orcdial_code=0;
  char orcdial_identity[16]{},orcdial_connection[32]{},orcdial_failure[32]{};
  bool wifi_c6_image_embedded = false;
  uint8_t wifi_c6_update_percent = 0;
  char wifi_ssid[33]{};
  char wifi_ip[16]{};
  char wifi_message[48]{};
  char wifi_hosted_host_version[16]{};
  char wifi_hosted_c6_version[16]{};
  char wifi_c6_update_state[20]{};
  char wifi_c6_update_stage[24]{};
  int16_t wifi_rssi = 0;
  WifiNetwork networks[6]{};
  uint8_t network_count = 0;
  WifiProfileView profiles[4]{};
  uint8_t saved_network_count = 0;

  bool location_configured = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint16_t radar_range_nm = 25;
  char location_label[40]{};
  char map_pack[40]{};
  // A world map is embedded in this firmware, so CHOOSE ON MAP works.
  bool map_picker_available = false;
  bool ip_location_busy = false;
  bool ip_location_ready = false;
  int32_t ip_latitude_e7 = 0;
  int32_t ip_longitude_e7 = 0;
  char ip_location_label[40]{};
  char ip_location_message[48]{};

  uint8_t brightness = 180;
  uint8_t rotation = 1;
  uint16_t screen_timeout_sec = 0;
  uint8_t volume = 128;
  bool sound_default = true;
  bool auto_start_reception = true;
  bool graphics_default = true;
  bool rtl_usb_safe_mode = false;
  char default_band[16]{};
  uint32_t fm_frequency_hz = 0;

  bool sd_ready = false;
  uint64_t sd_total_bytes = 0;
  uint64_t sd_free_bytes = 0;
  bool catalog_ready = false;
  bool catalog_busy = false;
  uint8_t catalog_progress_percent = 0;
  char catalog_message[80]{};
  char catalog_date[16]{};
  CatalogPackView catalog_packs[5]{};
  // Map packs the signed catalog offers (GitHub release assets); catalog_map_slot[i] is the slot to install or remove.
  static constexpr uint8_t kCatalogMapMax = 4;
  CatalogPackView catalog_maps[kCatalogMapMax]{};
  uint8_t catalog_map_slot[kCatalogMapMax]{};
  uint8_t catalog_map_count = 0;
  map_packs::Summary map_packs;   // map packs found on the SD card (Data & Maps)
  bool companion_supported = false;
  bool web_console_enabled = false;
  bool web_console_listening = false;
  char web_console_url[48]{};
  uint8_t paired_phone_count = 0;
  int32_t battery_level = -1;
  int16_t battery_mv = -1;
  int32_t battery_current_ma = 0;
  int16_t vbus_mv = -1;
  char charging_state[16]{};
  char build_identity[40]{};
  uint32_t uptime_seconds = 0;
  bool rtc_valid = false;
  char rtc_utc[24]{};
  // Clock setup: the hardware RTC holds UTC; the offset (minutes east of UTC) gives the local time.
  int16_t utc_offset_minutes = 0;
  uint32_t rtc_epoch = 0;     // UTC seconds, valid when rtc_valid
  char rtc_local[24]{};       // "YYYY-MM-DD HH:MM:SS" at the offset, when rtc_valid
  uint8_t ntp_state = 0;      // 0 idle, 1 syncing, 2 done, 3 failed
};

enum class ActionKind : uint8_t {
  none,
  close,
  wifi_power_changed,
  wifi_start_at_boot_changed,
  wifi_antenna_changed,
  c6_update_confirm,
  orcdial_c6_update,
  orcdial_pair,
  orcdial_cancel,orcdial_confirm,orcdial_connect,orcdial_disconnect,orcdial_forget,orcdial_boot,
  scan_wifi,
  connect_wifi,
  connect_saved_wifi,
  forget_wifi,
  move_wifi_up,
  move_wifi_down,
  location_changed,
  location_ip_lookup,
  location_query_lookup,
  location_ip_confirm,
  range_changed,
  brightness_changed,
  rotation_changed,
  timeout_changed,
  volume_changed,
  sound_changed,
  auto_start_changed,
  graphics_changed
  ,catalog_check
  ,catalog_install
  ,catalog_remove
  ,web_console_changed
  ,rtl_usb_safe_mode_reset
  // Opens the map picker on the current location (Location & ADS-B).
  ,location_pick_on_map
  // Re-scan the SD card's /orcmaps folder for map packs.
  ,map_packs_rescan
  ,clock_set_utc          // utc = UTC seconds, value = UTC offset in minutes: write the RTC and store the offset
  ,clock_offset_changed   // value = UTC offset in minutes: store it (the time itself was already right)
  ,clock_ntp_sync         // optional: set the RTC from the network (Wi-Fi must already be connected)
};

struct Action {
  ActionKind kind = ActionKind::none;
  int32_t value = 0;
  uint32_t utc = 0;   // clock_set_utc: UTC seconds to write (value is then the UTC offset in minutes)
};

void enter(const State& state, Section section = Section::connectivity);
void leave();
void draw();
void update(const State& state);
Action handle_touch(int32_t x, int32_t y);
// Pages with a scrollable list (Data & Maps) take the touch as a gesture: a drag scrolls, a tap acts on release.
// The caller feeds every touch sample (pressed or not) to handle_gesture while wants_gesture() is true; other pages
// keep the immediate handle_touch.
bool wants_gesture();
Action handle_gesture(int32_t x, int32_t y, bool pressed);
bool active();
const State& state();
Section section();
void show_documentation_section(Section section, const State& state,
                                bool show_wifi_keyboard = false);
bool take_wifi_credentials(char* ssid, size_t ssid_size,
                           char* password, size_t password_size);
bool take_location_query(char* query, size_t query_size);
bool self_check();

}  // namespace orcsdr::settings
