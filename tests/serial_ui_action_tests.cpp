// Host tests for RTL_UI ACTION (apps/orcsdr-tab5/ui/serial_ui_action.cpp). Expected replies, action names and range
// checks are typed from the code this replaced in main.cpp.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui
//   -Iapps/orcsdr-tab5/dsp, from the repository root: tests/serial_ui_action_tests.cpp
//   apps/orcsdr-tab5/ui/serial_ui_action.cpp
#include "serial_ui_action.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace orcsdr;
using namespace orcsdr::serial_ui_action;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_lines;
std::vector<std::string> g_calls;
bool g_airband_ok = true;
std::string g_airband_seen;
bool g_scan_prompt = false;
bool g_cb = true;
uint8_t g_profiles = 3;
fm::Action g_fm;
am::Action g_am;
cb::Action g_cbact;
p25::Action g_p25;
lora::Action g_lora;
settings::Action g_settings;

const Hooks kHooks = [] {
  Hooks h;
  h.write_line = [](const char* t) { g_lines.emplace_back(t); };
  h.airband_action = [](const char* a, bool has, uint32_t v) {
    g_airband_seen = std::string(a) + (has ? " " + std::to_string(v) : "");
    g_calls.emplace_back("airband");
    return g_airband_ok;
  };
  h.fm_action = [](const fm::Action& a) { g_fm = a; g_calls.emplace_back("fm"); };
  h.am_scan_prompt_active = [] { return g_scan_prompt; };
  h.am_discard_scan = [] { g_calls.emplace_back("am_discard"); };
  h.am_action = [](const am::Action& a) { g_am = a; g_calls.emplace_back("am"); };
  h.cb_active = [] { return g_cb; };
  h.cb_action = [](const cb::Action& a) { g_cbact = a; g_calls.emplace_back("cb"); };
  h.p25_action = [](const p25::Action& a) { g_p25 = a; g_calls.emplace_back("p25"); };
  h.lora_region_count = []() -> size_t { return 4; };
  h.lora_slot_count = []() -> uint16_t { return 8; };
  h.lora_action = [](const lora::Action& a) { g_lora = a; g_calls.emplace_back("lora"); };
  h.wifi_profile_count = [] { return g_profiles; };
  h.settings_action = [](const settings::Action& a) { g_settings = a; g_calls.emplace_back("settings"); };
  return h;
}();

void reset() {
  g_lines.clear();
  g_calls.clear();
  g_airband_ok = true;
  g_scan_prompt = false;
  g_cb = true;
  g_profiles = 3;
  g_fm = {};
  g_am = {};
  g_cbact = {};
  g_p25 = {};
  g_lora = {};
  g_settings = {};
}

// Sends a signed-in command and returns the single reply line.
std::string send(const std::string& command) {
  reset();
  CHECK(handle_command(command.c_str(), true, kHooks));
  CHECK(g_lines.size() == 1);
  return g_lines.empty() ? "" : g_lines[0];
}

const char kOk[] = "RTL_UI_ACTION_OK";

}  // namespace

int main() {
  // Sign-in and usage
  reset();
  CHECK(handle_command("RTL_UI ACTION FM UP", false, kHooks));
  CHECK(g_calls.empty() && g_lines.size() == 1 && g_lines[0] == "RTL_UI_ACTION_ERROR auth_required");
  CHECK(send("RTL_UI ACTION FM") ==
        "RTL_UI_ACTION_INVALID usage: RTL_UI ACTION <FM|AM|AIRBAND|CB|P25|LORA|SETTINGS> <action> [value]");
  CHECK(send("RTL_UI ACTION ") ==
        "RTL_UI_ACTION_INVALID usage: RTL_UI ACTION <FM|AM|AIRBAND|CB|P25|LORA|SETTINGS> <action> [value]");
  CHECK(send("RTL_UI ACTION WX UP") == "RTL_UI_ACTION_INVALID unknown_action");

  // AIRBAND passes through
  CHECK(send("RTL_UI ACTION AIRBAND SCAN") == kOk && g_airband_seen == "SCAN");
  CHECK(send("RTL_UI ACTION AIRBAND TUNE 118000000") == kOk && g_airband_seen == "TUNE 118000000");
  reset();
  g_airband_ok = false;
  CHECK(handle_command("RTL_UI ACTION AIRBAND BOGUS", true, kHooks));
  CHECK(g_lines.size() == 1 && g_lines[0] == "RTL_UI_ACTION_INVALID airband_action");

  // FM: every name, and the gain range
  using FK = fm::ActionKind;
  const std::pair<const char*, FK> fm_names[] = {
      {"TUNE", FK::tune_hz}, {"DOWN", FK::step_down}, {"UP", FK::step_up}, {"SEEK_DOWN", FK::seek_down},
      {"SEEK_UP", FK::seek_up}, {"SAVE", FK::save_preset}, {"STEP", FK::step_cycle},
      {"FILTER_DOWN", FK::filter_down}, {"FILTER_UP", FK::filter_up}, {"SPAN_DOWN", FK::span_down},
      {"SPAN_UP", FK::span_up}, {"SOUND", FK::sound_toggle}, {"VOL_DOWN", FK::volume_down},
      {"VOL_UP", FK::volume_up}, {"GRAPHICS", FK::graphics_toggle}, {"RECORD", FK::recording_toggle},
      {"GAIN_AUTO", FK::gain_auto}, {"SCAN", FK::scan_presets}, {"SETTINGS", FK::open_device_settings},
      {"HOME", FK::exit_to_browse}};
  for (const auto& [name, kind] : fm_names) {
    CHECK(send(std::string("RTL_UI ACTION FM ") + name + " 7") == kOk);
    CHECK(g_fm.kind == kind && g_fm.value == 7);
  }
  CHECK(send("RTL_UI ACTION FM GAIN 496") == kOk && g_fm.kind == FK::gain_tenth_db && g_fm.value == 496);
  CHECK(send("RTL_UI ACTION FM GAIN 497") == "RTL_UI_ACTION_INVALID fm_gain_out_of_range" && g_calls.empty());
  CHECK(send("RTL_UI ACTION FM GAIN") == "RTL_UI_ACTION_INVALID fm_gain_out_of_range");
  CHECK(send("RTL_UI ACTION FM BOGUS") == "RTL_UI_ACTION_INVALID unknown_action" && g_calls.empty());

  // AM
  CHECK(send("RTL_UI ACTION AM SCAN_DISCARD") == "RTL_UI_ACTION_INVALID am_scan_prompt_inactive" && g_calls.empty());
  reset();
  g_scan_prompt = true;
  CHECK(handle_command("RTL_UI ACTION AM SCAN_DISCARD", true, kHooks));
  CHECK(g_calls.size() == 1 && g_calls[0] == "am_discard" && g_lines.size() == 1 && g_lines[0] == kOk);
  using AK = am::ActionKind;
  const std::pair<const char*, AK> am_names[] = {
      {"DOWN", AK::step_down}, {"UP", AK::step_up}, {"STEP", AK::step_cycle}, {"SPACING", AK::scan_spacing_toggle},
      {"SPAN_DOWN", AK::span_down}, {"SPAN_UP", AK::span_up}, {"SAVE", AK::preset_save},
      {"SOUND", AK::sound_toggle}, {"VOL_DOWN", AK::volume_down}, {"VOL_UP", AK::volume_up},
      {"GRAPHICS", AK::graphics_toggle}, {"RECORD", AK::recording_toggle}, {"GAIN_AUTO", AK::gain_auto},
      {"SCAN", AK::scan_toggle}, {"SETTINGS", AK::open_device_settings}, {"HOME", AK::exit_home}};
  for (const auto& [name, kind] : am_names) {
    CHECK(send(std::string("RTL_UI ACTION AM ") + name) == kOk);
    CHECK(g_am.kind == kind && g_am.value == 0);
  }
  CHECK(send("RTL_UI ACTION AM TUNE 520000") == kOk && g_am.kind == AK::tune_hz && g_am.value == 520000);
  CHECK(send("RTL_UI ACTION AM TUNE 1710000") == kOk && g_am.value == 1710000);
  CHECK(send("RTL_UI ACTION AM TUNE 519999") == "RTL_UI_ACTION_INVALID am_frequency_out_of_range");
  CHECK(send("RTL_UI ACTION AM TUNE 1710001") == "RTL_UI_ACTION_INVALID am_frequency_out_of_range");
  CHECK(send("RTL_UI ACTION AM TUNE") == "RTL_UI_ACTION_INVALID am_frequency_out_of_range");
  for (const auto& [name, kind] : {std::pair<const char*, AK>{"PRESET", AK::preset_recall},
                                   {"REPLACE", AK::preset_replace}, {"DELETE", AK::preset_delete}}) {
    CHECK(send(std::string("RTL_UI ACTION AM ") + name + " 1") == kOk && g_am.kind == kind && g_am.value == 0);
    CHECK(send(std::string("RTL_UI ACTION AM ") + name + " 160") == kOk && g_am.value == 159);
    CHECK(send(std::string("RTL_UI ACTION AM ") + name + " 0") == "RTL_UI_ACTION_INVALID am_preset_out_of_range");
    CHECK(send(std::string("RTL_UI ACTION AM ") + name + " 161") == "RTL_UI_ACTION_INVALID am_preset_out_of_range");
    CHECK(send(std::string("RTL_UI ACTION AM ") + name) == "RTL_UI_ACTION_INVALID am_preset_out_of_range");
  }
  CHECK(send("RTL_UI ACTION AM GAIN 496") == kOk && g_am.kind == AK::gain_tenth_db);
  CHECK(send("RTL_UI ACTION AM GAIN 497") == "RTL_UI_ACTION_INVALID am_gain_out_of_range");
  CHECK(send("RTL_UI ACTION AM FILTER 3000") == kOk && g_am.kind == AK::filter_bandwidth_hz && g_am.value == 3000);
  CHECK(send("RTL_UI ACTION AM FILTER 30000") == kOk);
  CHECK(send("RTL_UI ACTION AM FILTER 2999") == "RTL_UI_ACTION_INVALID am_filter_out_of_range");
  CHECK(send("RTL_UI ACTION AM FILTER 30001") == "RTL_UI_ACTION_INVALID am_filter_out_of_range");
  CHECK(send("RTL_UI ACTION AM BOGUS") == "RTL_UI_ACTION_INVALID unknown_action");

  // CB
  using CK = cb::ActionKind;
  for (const auto& [name, kind] : {std::pair<const char*, CK>{"PREV", CK::channel_down}, {"NEXT", CK::channel_up},
                                   {"SCAN", CK::scan_toggle}, {"HOLD", CK::hold_toggle}, {"SKIP", CK::skip},
                                   {"MODE", CK::mode_cycle}}) {
    CHECK(send(std::string("RTL_UI ACTION CB ") + name + " 9") == kOk && g_cbact.kind == kind && g_cbact.value == 0);
  }
  reset();
  g_cb = false;
  CHECK(handle_command("RTL_UI ACTION CB NEXT", true, kHooks));
  CHECK(g_calls.empty() && g_lines.size() == 1 && g_lines[0] == "RTL_UI_ACTION_INVALID cb_dashboard_inactive");
  CHECK(send("RTL_UI ACTION CB BOGUS") == "RTL_UI_ACTION_INVALID unknown_action");

  // P25
  using PK = p25::ActionKind;
  const std::pair<const char*, PK> p25_names[] = {
      {"TUNE", PK::tune_hz}, {"PREV", PK::previous_candidate}, {"NEXT", PK::next_candidate},
      {"SURVEY", PK::survey_toggle}, {"HOLD", PK::hold_toggle}, {"HOLD_TG", PK::hold_talkgroup},
      {"SKIP", PK::skip_talkgroup}, {"FOLLOW", PK::auto_follow_toggle}, {"ENCRYPT_SKIP", PK::encryption_skip_toggle},
      {"RELOAD", PK::reload_config}, {"SPAN_DOWN", PK::span_down}, {"SPAN_UP", PK::span_up},
      {"SOUND", PK::sound_toggle}, {"VOL_DOWN", PK::volume_down}, {"VOL_UP", PK::volume_up},
      {"SETTINGS", PK::open_device_settings}, {"HOME", PK::exit_to_home}};
  for (const auto& [name, kind] : p25_names) {
    CHECK(send(std::string("RTL_UI ACTION P25 ") + name + " 851012500") == kOk);
    CHECK(g_p25.kind == kind && g_p25.value == 851012500u);
  }
  CHECK(send("RTL_UI ACTION P25 BOGUS") == "RTL_UI_ACTION_INVALID unknown_action");

  // LORA
  using LK = lora::ActionKind;
  const std::pair<const char*, LK> lora_names[] = {
      {"VIEW", LK::select_view}, {"NODE", LK::select_node}, {"DETAILS", LK::toggle_packet_details},
      {"FAVORITE", LK::toggle_favorite}, {"FILTER", LK::filter_next}, {"SCAN", LK::scan_toggle},
      {"IQ", LK::record_iq_toggle}, {"LOG", LK::logging_toggle}, {"CLEAR", LK::clear_events},
      {"EXPORT", LK::export_log}, {"FOLLOW", LK::follow_node}, {"CHANNELS", LK::open_channels},
      {"SETTINGS", LK::open_settings}, {"REGION_PREV", LK::region_previous}, {"REGION_NEXT", LK::region_next},
      {"SLOT_PREV", LK::channel_previous}, {"SLOT_NEXT", LK::channel_next}, {"HOME", LK::exit_home}};
  for (const auto& [name, kind] : lora_names) {
    CHECK(send(std::string("RTL_UI ACTION LORA ") + name + " 2") == kOk && g_lora.kind == kind && g_lora.value == 2);
  }
  CHECK(send("RTL_UI ACTION LORA REGION 1") == kOk && g_lora.kind == LK::region_select);
  CHECK(send("RTL_UI ACTION LORA REGION 4") == kOk);
  CHECK(send("RTL_UI ACTION LORA REGION 0") == "RTL_UI_ACTION_INVALID lora_region_or_slot_out_of_range");
  CHECK(send("RTL_UI ACTION LORA REGION 5") == "RTL_UI_ACTION_INVALID lora_region_or_slot_out_of_range");
  CHECK(send("RTL_UI ACTION LORA REGION") == "RTL_UI_ACTION_INVALID lora_region_or_slot_out_of_range");
  CHECK(send("RTL_UI ACTION LORA SLOT 8") == kOk && g_lora.kind == LK::channel_select && g_lora.value == 8);
  CHECK(send("RTL_UI ACTION LORA SLOT 9") == "RTL_UI_ACTION_INVALID lora_region_or_slot_out_of_range");
  CHECK(send("RTL_UI ACTION LORA SLOT 0") == "RTL_UI_ACTION_INVALID lora_region_or_slot_out_of_range");
  CHECK(send("RTL_UI ACTION LORA BOGUS") == "RTL_UI_ACTION_INVALID unknown_action");

  // SETTINGS
  using SK = settings::ActionKind;
  const std::pair<const char*, SK> settings_names[] = {
      {"C6_UPDATE_CONFIRM", SK::c6_update_confirm}, {"SCAN", SK::scan_wifi},
      {"BRIGHTNESS", SK::brightness_changed}, {"ROTATION", SK::rotation_changed}, {"TIMEOUT", SK::timeout_changed},
      {"VOLUME", SK::volume_changed}, {"SOUND", SK::sound_changed}, {"AUTO_START", SK::auto_start_changed},
      {"GRAPHICS", SK::graphics_changed}, {"WEB", SK::web_console_changed}, {"CATALOG_CHECK", SK::catalog_check},
      {"CATALOG_INSTALL", SK::catalog_install}, {"CATALOG_REMOVE", SK::catalog_remove}, {"CLOSE", SK::close}};
  for (const auto& [name, kind] : settings_names) {
    CHECK(send(std::string("RTL_UI ACTION SETTINGS ") + name + " 5") == kOk);
    CHECK(g_settings.kind == kind && g_settings.value == 5);
  }
  for (const auto& [name, kind] : {std::pair<const char*, SK>{"WIFI_POWER", SK::wifi_power_changed},
                                   {"WIFI_BOOT", SK::wifi_start_at_boot_changed},
                                   {"ANTENNA", SK::wifi_antenna_changed}}) {
    CHECK(send(std::string("RTL_UI ACTION SETTINGS ") + name + " 1") == kOk && g_settings.kind == kind);
    CHECK(send(std::string("RTL_UI ACTION SETTINGS ") + name + " 0") == kOk && g_settings.value == 0);
    CHECK(send(std::string("RTL_UI ACTION SETTINGS ") + name + " 2") == "RTL_UI_ACTION_INVALID value_must_be_0_or_1");
  }
  CHECK(send("RTL_UI ACTION SETTINGS CONNECT_SAVED 2") == kOk && g_settings.kind == SK::connect_saved_wifi);
  CHECK(send("RTL_UI ACTION SETTINGS CONNECT_SAVED 3") == "RTL_UI_ACTION_INVALID profile_index");
  CHECK(send("RTL_UI ACTION SETTINGS FORGET 3") == "RTL_UI_ACTION_INVALID profile_index");
  CHECK(send("RTL_UI ACTION SETTINGS MOVE_UP 1") == kOk && g_settings.kind == SK::move_wifi_up);
  CHECK(send("RTL_UI ACTION SETTINGS MOVE_UP 0") == "RTL_UI_ACTION_INVALID profile_move");
  CHECK(send("RTL_UI ACTION SETTINGS MOVE_UP 3") == "RTL_UI_ACTION_INVALID profile_move");
  CHECK(send("RTL_UI ACTION SETTINGS MOVE_DOWN 1") == kOk && g_settings.kind == SK::move_wifi_down);
  CHECK(send("RTL_UI ACTION SETTINGS MOVE_DOWN 2") == "RTL_UI_ACTION_INVALID profile_move");
  reset();
  g_profiles = 1;
  CHECK(handle_command("RTL_UI ACTION SETTINGS MOVE_DOWN 0", true, kHooks));
  CHECK(g_lines.size() == 1 && g_lines[0] == "RTL_UI_ACTION_INVALID profile_move");
  for (const char* range : {"10", "25", "50", "100"}) {
    CHECK(send(std::string("RTL_UI ACTION SETTINGS RANGE ") + range) == kOk && g_settings.kind == SK::range_changed);
  }
  CHECK(send("RTL_UI ACTION SETTINGS RANGE 20") == "RTL_UI_ACTION_INVALID range_must_be_10_25_50_100");
  CHECK(send("RTL_UI ACTION SETTINGS RANGE") == "RTL_UI_ACTION_INVALID range_must_be_10_25_50_100");
  CHECK(send("RTL_UI ACTION SETTINGS BOGUS") == "RTL_UI_ACTION_INVALID unknown_action");

  // Other commands fall through.
  for (const char* other : {"RTL_UI ACTION", "RTL_UI ACTIONS FM UP", "RTL_UI STATUS", "rtl_ui action fm up"}) {
    reset();
    CHECK(!handle_command(other, true, kHooks));
    CHECK(!handle_command(other, false, kHooks));
    CHECK(g_calls.empty() && g_lines.empty());
  }
  if (failures) {
    std::printf("serial_ui_action_tests: %d FAILED\n", failures);
    return 1;
  }
  std::printf("serial_ui_action_tests: ok\n");
  return 0;
}
