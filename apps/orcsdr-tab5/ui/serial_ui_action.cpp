#include "serial_ui_action.hpp"

#include <cstdio>
#include <cstring>

#include "receiver_band_plan.hpp"

namespace orcsdr::serial_ui_action {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strncmp(command, "RTL_UI ACTION ", 14) == 0 && !authenticated) {
    hooks.write_line("RTL_UI_ACTION_ERROR auth_required");
    return true;
  }
  if (strncmp(command, "RTL_UI ACTION ", 14) == 0) {
    char domain[12]{}, action[24]{};
    unsigned long value = 0;
    const int fields = sscanf(command + 14, "%11s %23s %lu", domain, action, &value);
    if (fields < 2) { hooks.write_line("RTL_UI_ACTION_INVALID usage: RTL_UI ACTION <FM|AM|AIRBAND|CB|P25|LORA|SETTINGS> <action> [value]"); return true; }
    if (strcmp(domain, "AIRBAND") == 0) {
      const bool applied = hooks.airband_action(action, fields == 3, static_cast<uint32_t>(value));
      hooks.write_line(applied ? "RTL_UI_ACTION_OK" : "RTL_UI_ACTION_INVALID airband_action");
      return true;
    }
    if (strcmp(domain, "FM") == 0) {
      using K = orcsdr::fm::ActionKind; K kind = K::none;
      if (!strcmp(action, "TUNE")) kind=K::tune_hz; else if (!strcmp(action, "DOWN")) kind=K::step_down;
      else if (!strcmp(action, "UP")) kind=K::step_up; else if (!strcmp(action, "SEEK_DOWN")) kind=K::seek_down;
      else if (!strcmp(action, "SEEK_UP")) kind=K::seek_up; else if (!strcmp(action, "SAVE")) kind=K::save_preset;
      else if (!strcmp(action, "STEP")) kind=K::step_cycle; else if (!strcmp(action, "FILTER_DOWN")) kind=K::filter_down;
      else if (!strcmp(action, "FILTER_UP")) kind=K::filter_up; else if (!strcmp(action, "SPAN_DOWN")) kind=K::span_down;
      else if (!strcmp(action, "SPAN_UP")) kind=K::span_up; else if (!strcmp(action, "SOUND")) kind=K::sound_toggle;
      else if (!strcmp(action, "VOL_DOWN")) kind=K::volume_down; else if (!strcmp(action, "VOL_UP")) kind=K::volume_up;
      else if (!strcmp(action, "GRAPHICS")) kind=K::graphics_toggle; else if (!strcmp(action, "RECORD")) kind=K::recording_toggle;
      else if (!strcmp(action, "GAIN_AUTO")) kind=K::gain_auto; else if (!strcmp(action, "GAIN")) kind=K::gain_tenth_db;
      else if (!strcmp(action, "SCAN")) kind=K::scan_presets; else if (!strcmp(action, "SETTINGS")) kind=K::open_device_settings;
      else if (!strcmp(action, "HOME")) kind=K::exit_to_browse;
      if (kind == K::gain_tenth_db && (fields != 3 || value > 496)) {
        hooks.write_line("RTL_UI_ACTION_INVALID fm_gain_out_of_range");
        return true;
      }
      if (kind != K::none) { hooks.fm_action(fm::Action{kind, static_cast<uint32_t>(value)}); hooks.write_line("RTL_UI_ACTION_OK"); return true; }
    } else if (strcmp(domain, "AM") == 0) {
      if (!strcmp(action, "SCAN_DISCARD")) {
        if (!hooks.am_scan_prompt_active()) {
          hooks.write_line("RTL_UI_ACTION_INVALID am_scan_prompt_inactive");
          return true;
        }
        hooks.am_discard_scan();
        hooks.write_line("RTL_UI_ACTION_OK");
        return true;
      }
      using K = orcsdr::am::ActionKind; K kind = K::none;
      if (!strcmp(action, "TUNE")) kind=K::tune_hz; else if (!strcmp(action, "DOWN")) kind=K::step_down;
      else if (!strcmp(action, "UP")) kind=K::step_up; else if (!strcmp(action, "STEP")) kind=K::step_cycle;
      else if (!strcmp(action, "SPACING")) kind=K::scan_spacing_toggle;
      else if (!strcmp(action, "FILTER")) kind=K::filter_bandwidth_hz; else if (!strcmp(action, "SPAN_DOWN")) kind=K::span_down;
      else if (!strcmp(action, "SPAN_UP")) kind=K::span_up; else if (!strcmp(action, "PRESET")) kind=K::preset_recall;
      else if (!strcmp(action, "SAVE")) kind=K::preset_save; else if (!strcmp(action, "REPLACE")) kind=K::preset_replace;
      else if (!strcmp(action, "DELETE")) kind=K::preset_delete;
      else if (!strcmp(action, "SOUND")) kind=K::sound_toggle;
      else if (!strcmp(action, "VOL_DOWN")) kind=K::volume_down; else if (!strcmp(action, "VOL_UP")) kind=K::volume_up;
      else if (!strcmp(action, "GRAPHICS")) kind=K::graphics_toggle; else if (!strcmp(action, "RECORD")) kind=K::recording_toggle;
      else if (!strcmp(action, "GAIN_AUTO")) kind=K::gain_auto; else if (!strcmp(action, "GAIN")) kind=K::gain_tenth_db;
      else if (!strcmp(action, "SCAN")) kind=K::scan_toggle;
      else if (!strcmp(action, "SETTINGS")) kind=K::open_device_settings; else if (!strcmp(action, "HOME")) kind=K::exit_home;
      if (kind == K::tune_hz &&
          (fields != 3 || value < orcsdr::receiver_bands::kAmBroadcast.min_hz ||
           value > orcsdr::receiver_bands::kAmBroadcast.max_hz)) {
        hooks.write_line("RTL_UI_ACTION_INVALID am_frequency_out_of_range");
        return true;
      }
      if ((kind == K::preset_recall || kind == K::preset_replace || kind == K::preset_delete) &&
          (fields != 3 || value < 1 || value > 160)) {
        hooks.write_line("RTL_UI_ACTION_INVALID am_preset_out_of_range");
        return true;
      }
      if (kind == K::gain_tenth_db && (fields != 3 || value > 496)) {
        hooks.write_line("RTL_UI_ACTION_INVALID am_gain_out_of_range");
        return true;
      }
      if (kind == K::filter_bandwidth_hz &&
          (fields != 3 || value < 3000 || value > 30000)) {
        hooks.write_line("RTL_UI_ACTION_INVALID am_filter_out_of_range");
        return true;
      }
      if (kind != K::none) {
        hooks.am_action(am::Action{kind, static_cast<uint32_t>(
            (kind == K::preset_recall || kind == K::preset_replace ||
             kind == K::preset_delete) ? value - 1 : value)});
        hooks.write_line("RTL_UI_ACTION_OK");
        return true;
      }
    } else if (strcmp(domain, "CB") == 0) {
      if (!hooks.cb_active()) {
        hooks.write_line("RTL_UI_ACTION_INVALID cb_dashboard_inactive");
        return true;
      }
      using K = orcsdr::cb::ActionKind; K kind = K::none;
      if (!strcmp(action, "PREV")) kind=K::channel_down;
      else if (!strcmp(action, "NEXT")) kind=K::channel_up;
      else if (!strcmp(action, "SCAN")) kind=K::scan_toggle;
      else if (!strcmp(action, "HOLD")) kind=K::hold_toggle;
      else if (!strcmp(action, "SKIP")) kind=K::skip;
      else if (!strcmp(action, "MODE")) kind=K::mode_cycle;
      if (kind != K::none) { hooks.cb_action(cb::Action{kind}); hooks.write_line("RTL_UI_ACTION_OK"); return true; }
    } else if (strcmp(domain, "P25") == 0) {
      using K = orcsdr::p25::ActionKind; K kind = K::none;
      if (!strcmp(action, "TUNE")) kind=K::tune_hz; else if (!strcmp(action, "PREV")) kind=K::previous_candidate;
      else if (!strcmp(action, "NEXT")) kind=K::next_candidate; else if (!strcmp(action, "SURVEY")) kind=K::survey_toggle;
      else if (!strcmp(action, "HOLD")) kind=K::hold_toggle; else if (!strcmp(action, "HOLD_TG")) kind=K::hold_talkgroup;
      else if (!strcmp(action, "SKIP")) kind=K::skip_talkgroup; else if (!strcmp(action, "FOLLOW")) kind=K::auto_follow_toggle;
      else if (!strcmp(action, "ENCRYPT_SKIP")) kind=K::encryption_skip_toggle; else if (!strcmp(action, "RELOAD")) kind=K::reload_config;
      else if (!strcmp(action, "SPAN_DOWN")) kind=K::span_down; else if (!strcmp(action, "SPAN_UP")) kind=K::span_up;
      else if (!strcmp(action, "SOUND")) kind=K::sound_toggle; else if (!strcmp(action, "VOL_DOWN")) kind=K::volume_down;
      else if (!strcmp(action, "VOL_UP")) kind=K::volume_up; else if (!strcmp(action, "SETTINGS")) kind=K::open_device_settings;
      else if (!strcmp(action, "HOME")) kind=K::exit_to_home;
      if (kind != K::none) { hooks.p25_action(p25::Action{kind, static_cast<uint32_t>(value)}); hooks.write_line("RTL_UI_ACTION_OK"); return true; }
    } else if (strcmp(domain, "LORA") == 0) {
      using K = orcsdr::lora::ActionKind; K kind = K::none;
      if (!strcmp(action, "VIEW")) kind=K::select_view; else if (!strcmp(action, "NODE")) kind=K::select_node;
      else if (!strcmp(action, "DETAILS")) kind=K::toggle_packet_details;
      else if (!strcmp(action, "FAVORITE")) kind=K::toggle_favorite; else if (!strcmp(action, "FILTER")) kind=K::filter_next;
      else if (!strcmp(action, "SCAN")) kind=K::scan_toggle; else if (!strcmp(action, "IQ")) kind=K::record_iq_toggle;
      else if (!strcmp(action, "LOG")) kind=K::logging_toggle; else if (!strcmp(action, "CLEAR")) kind=K::clear_events;
      else if (!strcmp(action, "EXPORT")) kind=K::export_log; else if (!strcmp(action, "FOLLOW")) kind=K::follow_node;
      else if (!strcmp(action, "CHANNELS")) kind=K::open_channels; else if (!strcmp(action, "SETTINGS")) kind=K::open_settings;
      else if (!strcmp(action, "REGION_PREV")) kind=K::region_previous; else if (!strcmp(action, "REGION_NEXT")) kind=K::region_next;
      else if (!strcmp(action, "REGION")) kind=K::region_select; else if (!strcmp(action, "SLOT_PREV")) kind=K::channel_previous;
      else if (!strcmp(action, "SLOT_NEXT")) kind=K::channel_next; else if (!strcmp(action, "SLOT")) kind=K::channel_select;
      else if (!strcmp(action, "HOME")) kind=K::exit_home;
      if ((kind == K::region_select &&
           (fields != 3 || value < 1 || value > hooks.lora_region_count())) ||
          (kind == K::channel_select &&
           (fields != 3 || value < 1 ||
            value > hooks.lora_slot_count()))) {
        hooks.write_line("RTL_UI_ACTION_INVALID lora_region_or_slot_out_of_range");
        return true;
      }
      if (kind != K::none) { hooks.lora_action(lora::Action{kind, static_cast<uint32_t>(value)}); hooks.write_line("RTL_UI_ACTION_OK"); return true; }
    } else if (strcmp(domain, "SETTINGS") == 0) {
      using K = orcsdr::settings::ActionKind; K kind = K::none;
      if (!strcmp(action, "WIFI_POWER")) kind=K::wifi_power_changed; else if (!strcmp(action, "WIFI_BOOT")) kind=K::wifi_start_at_boot_changed; else if (!strcmp(action, "ANTENNA")) kind=K::wifi_antenna_changed;
      else if (!strcmp(action, "C6_UPDATE_CONFIRM")) kind=K::c6_update_confirm;
      else if (!strcmp(action, "SCAN")) kind=K::scan_wifi; else if (!strcmp(action, "CONNECT_SAVED")) kind=K::connect_saved_wifi;
      else if (!strcmp(action, "FORGET")) kind=K::forget_wifi; else if (!strcmp(action, "MOVE_UP")) kind=K::move_wifi_up;
      else if (!strcmp(action, "MOVE_DOWN")) kind=K::move_wifi_down; else if (!strcmp(action, "RANGE")) kind=K::range_changed;
      else if (!strcmp(action, "BRIGHTNESS")) kind=K::brightness_changed; else if (!strcmp(action, "ROTATION")) kind=K::rotation_changed;
      else if (!strcmp(action, "TIMEOUT")) kind=K::timeout_changed; else if (!strcmp(action, "VOLUME")) kind=K::volume_changed;
      else if (!strcmp(action, "SOUND")) kind=K::sound_changed; else if (!strcmp(action, "AUTO_START")) kind=K::auto_start_changed;
      else if (!strcmp(action, "GRAPHICS")) kind=K::graphics_changed; else if (!strcmp(action, "WEB")) kind=K::web_console_changed;
      else if (!strcmp(action, "CATALOG_CHECK")) kind=K::catalog_check; else if (!strcmp(action, "CATALOG_INSTALL")) kind=K::catalog_install;
      else if (!strcmp(action, "CATALOG_REMOVE")) kind=K::catalog_remove; else if (!strcmp(action, "CLOSE")) kind=K::close;
      if ((kind == K::wifi_power_changed || kind == K::wifi_start_at_boot_changed ||
           kind == K::wifi_antenna_changed) && value > 1) {
        hooks.write_line("RTL_UI_ACTION_INVALID value_must_be_0_or_1");
        return true;
      }
      if ((kind == K::connect_saved_wifi || kind == K::forget_wifi) &&
          value >= hooks.wifi_profile_count()) {
        hooks.write_line("RTL_UI_ACTION_INVALID profile_index");
        return true;
      }
      if ((kind == K::move_wifi_up && (value == 0 || value >= hooks.wifi_profile_count())) ||
          (kind == K::move_wifi_down &&
           (hooks.wifi_profile_count() < 2 ||
            value >= static_cast<unsigned long>(hooks.wifi_profile_count() - 1)))) {   // count >= 2 here
        hooks.write_line("RTL_UI_ACTION_INVALID profile_move");
        return true;
      }
      if(kind==K::range_changed&&(fields!=3||
          (value!=10&&value!=25&&value!=50&&value!=100))) {
        hooks.write_line("RTL_UI_ACTION_INVALID range_must_be_10_25_50_100");return true;
      }
      if (kind != K::none) { hooks.settings_action(settings::Action{kind, static_cast<int32_t>(value)}); hooks.write_line("RTL_UI_ACTION_OK"); return true; }
    }
    hooks.write_line("RTL_UI_ACTION_INVALID unknown_action");
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_ui_action
