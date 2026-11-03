#pragma once
#include "c6_recovery_ui.hpp"

#include <cstddef>
#include <cstdint>

#include "catalog_sync.hpp"
#include "adsb_dashboard.hpp"
#include "settings_layout.hpp"

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
  bool wifi_c6_rom_recovery = false;
  char wifi_c6_rom_target_version[16]{};
  bool wifi_c6_rom_busy = false;
  c6_recovery_ui::Phase wifi_c6_rom_phase = c6_recovery_ui::Phase::idle;
  int wifi_c6_rom_result = 0;
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
  uint16_t radar_range_nm = orcsdr::adsb::kDefaultRadarRangeNm;
  char location_label[40]{};
  char map_pack[256]{};
  bool ip_location_busy = false;
  bool ip_location_ready = false;
  int32_t ip_latitude_e7 = 0;
  int32_t ip_longitude_e7 = 0;
  char ip_location_label[40]{};
  char ip_location_message[48]{};

  uint8_t brightness = 180;
  uint8_t rotation = 3;
  // Opt-in board policy. Defaults retain Tab5's rotation indices 1/3.
  bool orientation_profile = false;
  uint16_t orientation_degrees = 0;
  uint8_t orientation_manual_angles = 0x0f; // bits 0..3 = 0,90,180,270 degrees
  bool orientation_auto_available = false;
  bool orientation_auto_enabled = false;
  bool orientation_locked = false;
  uint16_t screen_timeout_sec = 0;
  uint8_t volume = 128;
  bool sound_default = true;
  bool auto_start_reception = true;
  bool graphics_default = true;
  bool rtl_usb_safe_mode = false;
  char default_band[16]{};
  uint32_t fm_frequency_hz = 0;

  bool sd_ready = false;
  bool sd_capacity_known = true; // legacy providers already supply free capacity
  uint64_t sd_total_bytes = 0;
  uint64_t sd_free_bytes = 0;
  bool catalog_ready = false;
  bool catalog_busy = false;
  uint8_t catalog_progress_percent = 0;
  char catalog_message[80]{};
  char catalog_date[16]{};
  // Mirrors catalog::kPackCount so dynamic catalog entries are visible in Settings.
  static constexpr std::size_t kCatalogViewCapacity = catalog::kPackCount;
  CatalogPackView catalog_packs[kCatalogViewCapacity]{};
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
  ,clock_set_utc          // utc = UTC seconds, value = UTC offset in minutes: write the RTC and store the offset
  ,clock_offset_changed   // value = UTC offset in minutes: store it (the time itself was already right)
  ,clock_ntp_sync         // optional: set the RTC from the network (Wi-Fi must already be connected)
  ,open_local_maps
  ,open_location_picker
  ,run_setup
  ,orientation_angle_changed // value is degrees; adapter owns validation/persistence
  ,orientation_auto_changed  // value is 0/1; adapter owns sensor policy
  ,c6_rom_begin,c6_rom_release_install,c6_rom_release_resume,c6_rom_cancel
};

struct Action {
  ActionKind kind = ActionKind::none;
  int32_t value = 0;
  uint32_t utc = 0;   // clock_set_utc: UTC seconds to write (value is then the UTC offset in minutes)
};

void enter(const State& state, Section section = Section::connectivity);
// UI-owner structural transition only. No state reset or drawing. Reject while
// an editor owns geometry; the adapter must defer the orientation transition.
bool set_viewport(Rect viewport);
const SettingsLayout& layout();
bool geometry_locked();
// Compact conversion is incremental. Check before wiring a board adapter;
// every section continues to support the default Tab5 viewport.
bool supports_section(Section section);
std::size_t content_page();
void leave();
void draw();
void update(const State& state);
// Open an existing location method: network=0, address=1, coordinates=2.
Action begin_location_method(int method);
bool begin_clock_edit();
Action handle_touch(int32_t x, int32_t y);
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
