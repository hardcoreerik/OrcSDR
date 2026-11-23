// Host test for p25_actions: every P25 dashboard action, with fake hooks that log each call in order and each console
// write on its own. The expectations were read off the former handle_p25_dashboard_action in main.cpp (commit 033bfc42).
#include "p25_actions.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using orcsdr::p25::Action;
using orcsdr::p25::ActionKind;
namespace pa = orcsdr::p25_actions;
using Log = std::vector<std::string>;

int g_failures = 0;
Log g_log;

std::string join(const Log& lines) {
  std::string out;
  for (const auto& line : lines) out += "  " + line + "\n";
  return out;
}

#define CHECK(condition)                                                          \
  do {                                                                            \
    if (!(condition)) {                                                           \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);           \
      ++g_failures;                                                               \
    }                                                                             \
  } while (0)
#define CHECK_LOG(expected)                                                       \
  do {                                                                            \
    if (g_log != (expected)) {                                                    \
      std::fprintf(stderr, "FAIL line %d: log is\n%sexpected\n%s", __LINE__,      \
                   join(g_log).c_str(), join(expected).c_str());                  \
      ++g_failures;                                                               \
    }                                                                             \
  } while (0)

void note(const char* format, ...) __attribute__((format(printf, 1, 2)));
void note(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  g_log.emplace_back(buffer);
}

// ---- fake device ----
std::atomic<bool> g_survey_active{false}, g_survey_cancel{false}, g_hold{false}, g_auto_follow{true}, g_enc_skip{true};
uint16_t g_hold_tg = 0, g_skipped_tg = 0;
uint32_t g_skip_until = 0, g_voice_hz = 0, g_control_hz = 851000000;
std::atomic<uint32_t> g_session{0};
uint8_t g_candidate = 0;
orcsdr::p25config::Config g_config;
orcsdr::p25config::StoreState g_profiles;
char g_status[64] = "SD profile pending";
std::atomic<uint32_t> g_span{2400000};
bool g_voice = false;
bool g_band_p25 = true;
bool g_sd = true;
bool g_sd_ready = true;
bool g_audio = true;
uint16_t g_current_tg = 0;
uint16_t g_grant_tg = 0;
bool g_ok = true;
const char* g_error = "bad_file";
char g_name[48] = "My System";
bool g_have_name = true;

pa::Hooks make_hooks() {
  pa::Hooks h;
  h.io.write = [](const char* t) { note("W:%s", t); };
  h.io.write_line = [](const char* t) { note("L:%s", t); };
  h.survey_active = &g_survey_active;
  h.survey_cancel_requested = &g_survey_cancel;
  h.hold = &g_hold;
  h.auto_follow = &g_auto_follow;
  h.encryption_skip = &g_enc_skip;
  h.hold_talkgroup = &g_hold_tg;
  h.skipped_talkgroup = &g_skipped_tg;
  h.skip_until_ms = &g_skip_until;
  h.voice_frequency_hz = &g_voice_hz;
  h.control_frequency_hz = &g_control_hz;
  h.voice_session = &g_session;
  h.candidate_index = &g_candidate;
  h.config = &g_config;
  h.profiles = &g_profiles;
  h.config_status = g_status;
  h.config_status_size = sizeof(g_status);
  h.scope_span_hz = &g_span;
  h.scope_span_min_hz = 120000;
  h.volume_step = 16;
  h.cancel_survey = []() { note("cancel_survey"); };
  h.start_survey = []() { note("start_survey"); };
  h.tune_control = [](uint32_t hz) { note("tune %u", static_cast<unsigned>(hz)); };
  h.request_hot_retune = [](uint32_t hz) { note("hot %u", static_cast<unsigned>(hz)); };
  h.request_config_save = []() { note("save_config"); };
  h.load_config = []() { note("load_config"); };
  h.band_is_p25 = []() { return g_band_p25; };
  h.millis_now = []() { return 5000u; };
  h.follow_is_voice = []() { return g_voice; };
  h.follow_to_control = []() { note("follow_control"); g_voice = false; };
  h.suspend_voice = []() { note("suspend_voice"); };
  h.current_talkgroup = []() { return g_current_tg; };
  h.grant_talkgroup_to_hold = []() { return g_grant_tg; };
  h.sd_available = []() { return g_sd; };
  h.sd_ready = []() { return g_sd_ready; };
  h.select_profile = [](const char* id, char* e, size_t n) {
    note("select %s", id);
    if (!g_ok) snprintf(e, n, "%s", g_error);
    return g_ok;
  };
  h.import_profile = [](const char* id, char* e, size_t n) {
    note("import %s", id);
    if (!g_ok) snprintf(e, n, "%s", g_error);
    return g_ok;
  };
  h.export_profile = [](const char* id, char* e, size_t n) {
    note("export %s", id);
    if (!g_ok) snprintf(e, n, "%s", g_error);
    return g_ok;
  };
  h.take_profile_name = [](char* name, size_t n) {
    note("take_name");
    if (!g_have_name) return false;
    snprintf(name, n, "%s", g_name);
    return true;
  };
  h.rename_profile = [](const char* id, const char* name, char* e, size_t n) {
    note("rename %s %s", id, name);
    if (!g_ok) snprintf(e, n, "%s", g_error);
    return g_ok;
  };
  h.delete_profile = [](const char* id, char* e, size_t n) {
    note("delete %s", id);
    if (!g_ok) snprintf(e, n, "%s", g_error);
    return g_ok;
  };
  h.span_cap_hz = []() { return 3000000u; };
  h.reset_spectrum_renderer = []() { note("reset_spectrum"); };
  h.audio_user_enabled = []() { return g_audio; };
  h.set_audio_user_enabled = [](bool e) { note("audio_enabled %d", e); };
  h.adjust_volume = [](int d) { note("volume %d", d); };
  h.open_radio_defaults_settings = []() { note("open_settings"); };
  h.leave_dashboard = []() { note("leave"); };
  h.show_home = []() { note("home"); };
  h.refresh_active_screen = []() { note("refresh"); };
  return h;
}

void reset() {
  g_log.clear();
  g_survey_active = false; g_survey_cancel = false; g_hold = false; g_auto_follow = true; g_enc_skip = true;
  g_hold_tg = 0; g_skipped_tg = 0; g_skip_until = 0; g_voice_hz = 0; g_control_hz = 851000000;
  g_session = 0; g_candidate = 0;
  g_config = orcsdr::p25config::Config{};
  g_config.control_channel_count = 3;
  g_config.control_channels_hz[0] = 851000000;
  g_config.control_channels_hz[1] = 852000000;
  g_config.control_channels_hz[2] = 853000000;
  g_profiles = orcsdr::p25config::StoreState{};
  g_profiles.count = 2;
  snprintf(g_profiles.profiles[0].id, sizeof(g_profiles.profiles[0].id), "alpha");
  snprintf(g_profiles.profiles[1].id, sizeof(g_profiles.profiles[1].id), "beta");
  snprintf(g_status, sizeof(g_status), "SD profile pending");
  g_span = 2400000;
  g_voice = false; g_band_p25 = true; g_sd = true; g_sd_ready = true; g_audio = true;
  g_current_tg = 0; g_grant_tg = 0; g_ok = true; g_error = "bad_file";
  g_have_name = true;
}

void run(ActionKind kind, uint32_t value = 0) {
  const auto hooks = make_hooks();
  pa::handle(Action{kind, value}, hooks);
}

void test_tune_and_candidates() {
  reset();
  run(ActionKind::tune_hz, 852000000);
  CHECK_LOG((Log{"cancel_survey", "tune 852000000", "refresh"}));

  reset();
  run(ActionKind::next_candidate);
  CHECK_LOG((Log{"cancel_survey", "tune 852000000", "save_config", "refresh"}));
  CHECK(g_candidate == 1);
  reset(); g_candidate = 2;
  run(ActionKind::next_candidate);
  CHECK(g_candidate == 0);
  CHECK_LOG((Log{"cancel_survey", "tune 851000000", "save_config", "refresh"}));
  reset();
  run(ActionKind::previous_candidate);
  CHECK(g_candidate == 2);
  CHECK_LOG((Log{"cancel_survey", "tune 853000000", "save_config", "refresh"}));
  reset(); g_config.control_channel_count = 0;
  run(ActionKind::previous_candidate);
  CHECK_LOG((Log{"refresh"}));
  reset(); g_config.control_channel_count = 0;
  run(ActionKind::next_candidate);
  CHECK_LOG((Log{"refresh"}));
}

void test_survey_hold_skip() {
  reset(); g_survey_active = false;
  run(ActionKind::survey_toggle);
  CHECK_LOG((Log{"start_survey", "refresh"}));
  reset(); g_survey_active = true;
  run(ActionKind::survey_toggle);
  CHECK_LOG((Log{"refresh"}));
  CHECK(g_survey_cancel.load());

  reset(); g_grant_tg = 4321;
  run(ActionKind::hold_toggle);
  CHECK_LOG((Log{"W:RTL_P25_HOLD enabled=1 tg=4321 mode=talkgroup\n", "save_config", "refresh"}));
  CHECK(g_hold.load() && g_hold_tg == 4321);
  reset(); g_grant_tg = 0;
  run(ActionKind::hold_toggle);
  CHECK_LOG((Log{"W:RTL_P25_HOLD enabled=1 tg=0 mode=next\n", "save_config", "refresh"}));
  reset(); g_hold = true; g_hold_tg = 77;
  run(ActionKind::hold_toggle);
  CHECK_LOG((Log{"W:RTL_P25_HOLD enabled=0 tg=0 mode=next\n", "save_config", "refresh"}));
  CHECK(!g_hold.load() && g_hold_tg == 0);

  reset();
  run(ActionKind::hold_talkgroup, 900);
  CHECK_LOG((Log{"W:RTL_P25_HOLD enabled=1 tg=900\n", "save_config", "refresh"}));
  reset(); g_hold = true; g_hold_tg = 900;
  run(ActionKind::hold_talkgroup, 900);
  CHECK_LOG((Log{"W:RTL_P25_HOLD enabled=0 tg=0\n", "save_config", "refresh"}));
  reset(); g_hold = true; g_hold_tg = 901;
  run(ActionKind::hold_talkgroup, 900);
  CHECK(g_hold.load() && g_hold_tg == 900);

  // skip: not following a call and no talkgroup: nothing is remembered or left, and it says so
  reset();
  run(ActionKind::skip_talkgroup);
  CHECK_LOG((Log{"W:RTL_P25_SKIP tg=unknown remembered=0 left_call=0\n", "refresh"}));
  CHECK(g_skipped_tg == 0);
  // skip a talkgroup on the control channel
  reset(); g_current_tg = 555;
  run(ActionKind::skip_talkgroup);
  CHECK_LOG((Log{"W:RTL_P25_SKIP tg=555 duration_ms=30000\n", "refresh"}));
  CHECK(g_skipped_tg == 555 && g_skip_until == 35000);
  // skip during a voice call: also back to the control channel
  reset(); g_current_tg = 556; g_voice = true; g_voice_hz = 852500000;
  run(ActionKind::skip_talkgroup);
  CHECK_LOG((Log{"W:RTL_P25_SKIP tg=556 duration_ms=30000\n", "follow_control", "suspend_voice",
                 "hot 851000000", "refresh"}));
  CHECK(g_session.load() == 1 && g_voice_hz == 0);
  // voice with no talkgroup known: still leaves the call
  reset(); g_current_tg = 0; g_voice = true;
  run(ActionKind::skip_talkgroup);
  CHECK_LOG((Log{"W:RTL_P25_SKIP tg=unknown remembered=0 left_call=1\n", "follow_control", "suspend_voice",
                 "hot 851000000", "refresh"}));
  CHECK(g_skipped_tg == 0);
}

void test_toggles_and_reload() {
  reset(); g_auto_follow = true; g_voice = false;
  run(ActionKind::auto_follow_toggle);
  CHECK(!g_auto_follow.load());
  CHECK_LOG((Log{"save_config", "refresh"}));
  reset(); g_auto_follow = true; g_voice = true;
  run(ActionKind::auto_follow_toggle);
  CHECK_LOG((Log{"follow_control", "suspend_voice", "hot 851000000", "save_config", "refresh"}));
  reset(); g_auto_follow = false; g_voice = true;
  run(ActionKind::auto_follow_toggle);   // switching it on never leaves the call
  CHECK(g_auto_follow.load());
  CHECK_LOG((Log{"save_config", "refresh"}));

  reset();
  run(ActionKind::encryption_skip_toggle);
  CHECK(!g_enc_skip.load());
  CHECK_LOG((Log{"save_config", "refresh"}));

  reset();
  run(ActionKind::reload_config);
  CHECK_LOG((Log{"cancel_survey", "load_config", "tune 851000000", "refresh"}));
  reset(); g_band_p25 = false;
  run(ActionKind::reload_config);
  CHECK_LOG((Log{"cancel_survey", "load_config", "refresh"}));
  reset(); g_config.control_channel_count = 0;
  run(ActionKind::reload_config);
  CHECK_LOG((Log{"cancel_survey", "load_config", "refresh"}));
}

void test_profiles() {
  reset();
  run(ActionKind::select_profile, 1);
  CHECK_LOG((Log{"select beta", "tune 851000000", "refresh"}));
  reset(); g_band_p25 = false;
  run(ActionKind::select_profile, 0);
  CHECK_LOG((Log{"select alpha", "refresh"}));
  reset(); g_ok = false;
  run(ActionKind::select_profile, 0);
  CHECK_LOG((Log{"select alpha", "refresh"}));
  CHECK(std::strcmp(g_status, "Select failed: bad_file") == 0);
  reset(); g_sd = false;
  run(ActionKind::select_profile, 0);
  CHECK_LOG((Log{"refresh"}));
  reset();
  run(ActionKind::select_profile, 2);   // == count
  CHECK_LOG((Log{"refresh"}));

  reset();
  run(ActionKind::import_profile);
  CHECK_LOG((Log{"import imported", "load_config", "refresh"}));
  reset(); snprintf(g_profiles.profiles[0].id, 32, "imported"); snprintf(g_profiles.profiles[1].id, 32, "imported-1");
  run(ActionKind::import_profile);
  CHECK_LOG((Log{"import imported-2", "load_config", "refresh"}));
  reset(); g_ok = false; g_error = "a_very_long_error_text_that_is_longer_than_forty_five_characters_for_sure";
  run(ActionKind::import_profile);
  CHECK(std::strcmp(g_status, "Import failed: a_very_long_error_text_that_is_longer_than_fo") == 0);   // %.45s
  reset(); g_sd_ready = false;
  run(ActionKind::import_profile);
  CHECK_LOG((Log{"refresh"}));

  reset();
  run(ActionKind::export_profile, 0);
  CHECK_LOG((Log{"export alpha", "refresh"}));
  CHECK(std::strcmp(g_status, "Exported /orcsdr/exports/p25-profile.cfg") == 0);
  reset(); g_ok = false;
  run(ActionKind::export_profile, 1);
  CHECK(std::strcmp(g_status, "bad_file") == 0);
  reset(); g_sd = false;
  run(ActionKind::export_profile, 0);
  CHECK_LOG((Log{"refresh"}));

  reset();
  run(ActionKind::rename_profile, 1);
  CHECK_LOG((Log{"take_name", "rename beta My System", "load_config", "refresh"}));
  reset(); g_have_name = false;
  run(ActionKind::rename_profile, 1);
  CHECK_LOG((Log{"take_name", "refresh"}));
  CHECK(std::strcmp(g_status, "SD profile pending") == 0);
  reset(); g_ok = false;
  run(ActionKind::rename_profile, 1);
  CHECK(std::strcmp(g_status, "Rename failed: bad_file") == 0);
  reset(); g_ok = false; g_error = "";
  run(ActionKind::rename_profile, 1);
  CHECK(std::strcmp(g_status, "SD profile pending") == 0);   // an empty error leaves the status alone

  reset();
  run(ActionKind::delete_profile, 0);
  CHECK_LOG((Log{"delete alpha", "load_config", "refresh"}));
  reset(); g_ok = false;
  run(ActionKind::delete_profile, 0);
  CHECK(std::strcmp(g_status, "Delete failed: bad_file") == 0);
  reset(); g_sd = false;
  run(ActionKind::delete_profile, 0);
  CHECK_LOG((Log{"refresh"}));
}

void test_receiver_and_navigation() {
  reset(); g_span = 2400000;
  run(ActionKind::span_down);
  CHECK(g_span.load() == 1200000);
  CHECK_LOG((Log{"reset_spectrum", "refresh"}));
  reset(); g_span = 150000;
  run(ActionKind::span_down);
  CHECK(g_span.load() == 120000);
  reset(); g_span = 2400000;
  run(ActionKind::span_up);
  CHECK(g_span.load() == 3000000);
  reset(); g_span = 1000000;
  run(ActionKind::span_up);
  CHECK(g_span.load() == 2000000);

  reset(); g_audio = true;
  run(ActionKind::sound_toggle);
  CHECK_LOG((Log{"audio_enabled 0", "refresh"}));
  reset(); g_audio = false;
  run(ActionKind::sound_toggle);
  CHECK_LOG((Log{"audio_enabled 1", "refresh"}));
  reset(); run(ActionKind::volume_down);
  CHECK_LOG((Log{"volume -16", "refresh"}));
  reset(); run(ActionKind::volume_up);
  CHECK_LOG((Log{"volume 16", "refresh"}));
  reset(); run(ActionKind::open_device_settings);
  CHECK_LOG((Log{"open_settings", "refresh"}));   // unlike Shortwave, the P25 handler still refreshes
  reset(); run(ActionKind::exit_to_home);
  CHECK_LOG((Log{"leave", "home", "refresh"}));
  reset(); run(ActionKind::none);
  CHECK_LOG((Log{"refresh"}));
}

}  // namespace

int main() {
  test_tune_and_candidates();
  test_survey_hold_skip();
  test_toggles_and_reload();
  test_profiles();
  test_receiver_and_navigation();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::puts("PASS p25_actions");
  return 0;
}
