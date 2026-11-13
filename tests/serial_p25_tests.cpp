// Host test for the RTL_P25 command family (serial_p25.cpp). The expected replies are the format strings the
// dispatcher in main.cpp used before the family moved, filled in with the scripted device facts below.
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_p25_tests.cpp and apps/orcsdr-tab5/ui/{serial_p25,p25_decoder_core}.cpp
#include "serial_p25.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace orcsdr::serial_p25;
namespace p25core = orcsdr::p25core;
namespace p25config = orcsdr::p25config;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// "W:" is one verbatim write (it carries its own '\n'), "L:" is a line written through println, "C:" is a hook
// call; one list, in the order they happened. The plain readers of configuration and profile store are not logged.
std::vector<std::string> g_log;

IqStatus g_iq;
bool g_capture_is_p25 = true;
Encryption g_encryption;
p25core::Snapshot g_snapshot;
p25config::Config g_config;
Status g_status;
float g_levels[p25config::kMaxControlChannels] = {};
p25core::Grant g_probe_grant;
bool g_probing = false;
Phase2Events g_events;
bool g_trace = false;
p25config::StoreState g_profiles;
bool g_profile_ok = true;
const char* g_profile_error = "";
const char* g_reload_status = "loaded";

bool profile_result(char* error, size_t error_size) {
  if (g_profile_ok) return true;
  std::snprintf(error, error_size, "%s", g_profile_error);
  return false;
}

Hooks make_hooks() {
  Hooks hooks;
  hooks.write = [](const char* text) { g_log.push_back(std::string("W:") + text); };
  hooks.write_line = [](const char* text) { g_log.push_back(std::string("L:") + text); };
  hooks.iq_start = [] { g_log.push_back("C:iq_start"); };
  hooks.iq_capture_is_p25 = []() -> bool {
    g_log.push_back("C:iq_capture_is_p25");
    return g_capture_is_p25;
  };
  hooks.iq_stop_and_export = [] { g_log.push_back("C:iq_stop_and_export"); };
  hooks.iq_status = []() -> IqStatus { return g_iq; };
  hooks.replay = [](const char* path) { g_log.push_back(std::string("C:replay [") + path + "]"); };
  hooks.encryption = []() -> Encryption { return g_encryption; };
  hooks.snapshot = []() -> p25core::Snapshot {
    g_log.push_back("C:snapshot");
    return g_snapshot;
  };
  hooks.config = []() -> const p25config::Config* { return &g_config; };
  hooks.set_modulation = [](p25core::Modulation modulation) {
    g_log.push_back("C:set_modulation");
    g_config.modulation = modulation;   // the reply must show the configuration as it is afterwards
  };
  hooks.status = []() -> Status {
    g_log.push_back("C:status");
    return g_status;
  };
  hooks.candidate_level = [](size_t index) -> float { return g_levels[index]; };
  hooks.phase2_probe_grant = []() -> const p25core::Grant* {
    g_log.push_back("C:phase2_probe_grant");
    return g_probing ? &g_probe_grant : nullptr;
  };
  hooks.phase2_events = []() -> Phase2Events {
    g_log.push_back("C:phase2_events");
    return g_events;
  };
  hooks.phase2_trace = []() -> bool { return g_trace; };
  hooks.set_phase2_trace = [](bool enabled) {
    g_log.push_back(enabled ? "C:set_phase2_trace 1" : "C:set_phase2_trace 0");
    g_trace = enabled;
  };
  hooks.profiles = []() -> const p25config::StoreState* { return &g_profiles; };
  hooks.profile_select = [](const char* id, char* error, size_t error_size) -> bool {
    g_log.push_back(std::string("C:select [") + id + "]");
    return profile_result(error, error_size);
  };
  hooks.profile_import = [](const char* path, const char* id, char* error, size_t error_size) -> bool {
    g_log.push_back(std::string("C:import [") + path + "] [" + id + "]");
    return profile_result(error, error_size);
  };
  hooks.profile_export = [](const char* id, const char* path, char* error, size_t error_size) -> bool {
    g_log.push_back(std::string("C:export [") + id + "] [" + path + "]");
    return profile_result(error, error_size);
  };
  hooks.profile_rename = [](const char* id, const char* name, char* error, size_t error_size) -> bool {
    g_log.push_back(std::string("C:rename [") + id + "] [" + name + "]");
    return profile_result(error, error_size);
  };
  hooks.profile_delete = [](const char* id, char* error, size_t error_size) -> bool {
    g_log.push_back(std::string("C:delete [") + id + "]");
    return profile_result(error, error_size);
  };
  hooks.load_config = [] { g_log.push_back("C:load_config"); };
  hooks.config_reload = []() -> const char* {
    g_log.push_back("C:config_reload");
    return g_reload_status;
  };
  hooks.scan = [] { g_log.push_back("C:scan"); };
  return hooks;
}

void expect(int line, const std::vector<std::string>& expected) {
  if (g_log != expected) {
    ++failures;
    std::printf("FAIL line %d output:\n", line);
    for (size_t i = 0; i < g_log.size() || i < expected.size(); ++i) {
      std::printf("  got:  [%s]\n  want: [%s]\n", i < g_log.size() ? g_log[i].c_str() : "<nothing>",
                  i < expected.size() ? expected[i].c_str() : "<nothing>");
    }
  }
  g_log.clear();
}
#define EXPECT(...) expect(__LINE__, __VA_ARGS__)

void script_state() {
  g_iq.source = "p25";
  g_iq.active = true;
  g_iq.ready = false;
  g_iq.bytes = 4096;
  g_iq.max_bytes = 1048512;
  g_iq.frequency_hz = 851012500;
  g_iq.rate = 960000;
  g_iq.last_path = "/orcsdr/iq/p25_0001.orciq";

  g_encryption.last = 0x00841A2Bu;
  g_encryption.detected = true;
  g_encryption.muted_frames = 205;
  g_encryption.returns = 206;
  g_encryption.skip_enabled = true;

  std::snprintf(g_config.system_name, sizeof(g_config.system_name), "County Fire");
  g_config.control_channel_count = 2;
  g_config.control_channels_hz[0] = 851012500;
  g_config.control_channels_hz[1] = 851037500;
  g_config.modulation = p25core::Modulation::cqpsk;
  g_levels[0] = -41.5f;
  g_levels[1] = -120.0f;

  g_profiles.count = 2;
  g_profiles.active_index = 1;
  std::snprintf(g_profiles.active_id, sizeof(g_profiles.active_id), "county");
  std::snprintf(g_profiles.profiles[0].id, sizeof(g_profiles.profiles[0].id), "city");
  std::snprintf(g_profiles.profiles[0].name, sizeof(g_profiles.profiles[0].name), "City Police");
  std::snprintf(g_profiles.profiles[1].id, sizeof(g_profiles.profiles[1].id), "county");
  std::snprintf(g_profiles.profiles[1].name, sizeof(g_profiles.profiles[1].name), "County Fire");

  p25core::Snapshot& d = g_snapshot;
  d.configured_modulation = p25core::Modulation::auto_detect;
  d.selected_modulation = p25core::Modulation::c4fm;
  d.frame_sync = true;
  d.identity_valid = false;
  d.nac = 0x0A5;
  d.wacn = 0xBEE1;
  d.system_id = 0x1C3;
  d.rfss = 2;
  d.site = 17;
  d.sync_words = 101;
  d.nid_good = 102;
  d.nid_failed = 103;
  d.tsbk_good = 104;
  d.tsbk_failed = 105;
  d.phase2_band_plans = 106;
  d.phase2_grants = 999;   // not printed: the grant counter comes from the trunking events
  d.phase2_mapping_errors = 107;
  d.phase2_acquisition = true;
  d.phase2_symbols = 108;
  d.phase2_sync_words = 109;
  d.phase2_complete_bursts = 110;
  d.phase2_truncated_bursts = 111;
  d.phase2_last_duid_codeword = 0x3C;
  d.phase2_last_duid_valid = true;
  d.phase2_last_duid = 5;
  d.phase2_last_duid_errors = 2;
  d.phase2_voice_bursts = 112;
  d.phase2_control_bursts = 113;
  d.phase2_unknown_bursts = 114;
  d.phase2_reverse_polarity = true;
  d.phase2_best_sync_errors = 7;
  d.estimated_ber_percent = 1.75f;
  d.voice_ldus = 115;
  d.voice_frames = 116;
  d.voice_queue_drops = 117;
  d.voice_unrouted_frames = 118;
  d.encryption_sync_good = 119;
  d.encryption_sync_failed = 120;
  d.phase2_last_sync_ms = 121;
  d.phase2_last_burst_ms = 122;
  d.lock_quality_percent = 87.5f;
  d.timing_error = 0.125f;
  d.carrier_error_hz = -212.5f;
  d.decode_rate_hz = 3.25f;
  d.recent_grants[0].valid = true;
  d.recent_grants[3].valid = true;
  d.recent_grants[7].valid = true;
  d.current_grant.talkgroup = 1201;
  d.current_grant.source_id = 7654321;
  d.current_grant.frequency_hz = 853212500;
  d.current_grant.slot = 1;
  d.current_grant.channel_id = 3;
  d.current_grant.channel_number = 456;
  d.current_grant.service_options = 0x4A;
  d.current_grant.encrypted = true;
  d.current_grant.wacn = 0xABCDE;
  d.current_grant.system_id = 0x2F1;
  d.current_grant.rfss = 5;
  d.current_grant.site = 6;

  g_probe_grant.talkgroup = 2202;
  g_probe_grant.source_id = 11;
  g_probe_grant.frequency_hz = 854000000;
  g_probe_grant.slot = 0;
  g_probe_grant.channel_id = 1;
  g_probe_grant.channel_number = 9;
  g_probe_grant.service_options = 0;
  g_probe_grant.encrypted = false;
  g_probe_grant.wacn = 1;
  g_probe_grant.system_id = 2;
  g_probe_grant.rfss = 3;
  g_probe_grant.site = 4;

  Status& s = g_status;
  s.usb_overruns = 217;
  s.usb_drops = 218;
  s.encryption = 0x00841A2Bu;
  s.follow = Follow::voice;
  s.frequency_hz = 851012500;
  s.survey = true;
  s.candidate = 1;
  s.hold = true;
  s.hold_talkgroup = 4101;
  s.auto_follow = false;
  s.encryption_skip = true;
  s.relative_dbfs = -37.5f;
  s.phase2_grant_events = 201;
  s.phase2_sync_events = 202;
  s.phase2_rejections = 203;
  s.phase2_trace = true;
  s.grant_events = 204;
  s.control_hz = 851037500;
  s.voice_hz = 852612500;
  s.encrypted_detected = true;
  s.encrypted_muted_frames = 205;
  s.encrypted_returns = 206;
  s.imbe_frames = 207;
  s.imbe_errors = 208;
  s.pcm_frames = 209;
  s.voice_stack_hwm = 210;
  s.imbe_max_us = 211;
  s.imbe_synth_max_us = 212;
  s.audio_queue_max_us = 213;
  s.heap_free = 214;
  s.heap_min = 215;
  s.psram_free = 216;
  s.iq_drops = 219;
  s.audio_drops = 220;

  g_events.trace = true;
  g_events.grants = 201;
  g_events.syncs = 202;
  g_events.rejections = 203;
}

const char kStatusLine[] =
    "W:RTL_P25_STATUS configured=1 profile_id=\"county\" profile=\"County Fire\" identity_source=air "
    "frequency_hz=851012500 survey=1 candidate=1 hold=1 hold_tg=4101 auto_follow=0 "
    "encryption_skip=1 relative_dbfs=-37.5 "
    "modulation_configured=auto modulation_selected=c4fm lock_quality=87.5 "
    "timing_error=0.1250 carrier_error_hz=-212.5 decode_rate_hz=3.25 "
    "frame_sync=1 identity=0 nac=0A5 wacn=0BEE1 sysid=1C3 rfss=2 site=17 "
    "sync_words=101 nid_good=102 nid_failed=103 tsbk_good=104 tsbk_failed=105 "
    "p2_band_plans=106 p2_grants=201 p2_mapping_errors=107 p2_syncs=202 "
    "p2_rejections=203 p2_trace=1 "
    "p2_active=1 p2_symbols=108 p2_sync_words=109 p2_complete_bursts=110 "
    "p2_truncated_bursts=111 p2_duid_codeword=3C p2_duid_valid=1 "
    "p2_duid=5 p2_duid_errors=2 p2_voice_bursts=112 p2_control_bursts=113 "
    "p2_unknown_bursts=114 p2_reverse=1 "
    "p2_best_sync_errors=7 "
    "ber_percent=1.75 grants=3 grant_events=204 follow=voice control_hz=851037500 voice_hz=852612500 "
    "voice_ldus=115 voice_frames=116 voice_queue_drops=117 voice_unrouted=118 "
    "enc_sync_good=119 "
    "enc_sync_failed=120 encrypted_detected=1 algid=84 kid=1A2B "
    "encrypted_muted_frames=205 encrypted_returns=206 imbe_frames=207 "
    "imbe_errors=208 pcm_frames=209 voice_stack_hwm=210 imbe_max_us=211 "
    "imbe_synth_max_us=212 audio_queue_max_us=213 heap_free=214 heap_min=215 "
    "psram_free=216 usb_overruns=217 usb_drops=218 iq_drops=219 audio_drops=220\n";

// The one write of an RTL_P25_STATUS reply that is the status line itself.
std::string status_line(const Hooks& hooks) {
  CHECK(handle_command("RTL_P25_STATUS", false, hooks));
  std::string line;
  for (const std::string& entry : g_log) {
    if (entry.rfind("W:RTL_P25_STATUS ", 0) == 0) line = entry;
  }
  g_log.clear();
  return line;
}

}  // namespace

int main() {
  const Hooks hooks = make_hooks();
  script_state();

  // Routes that do not check authentication: the same thing happens either way.
  for (bool authenticated : {false, true}) {
    CHECK(handle_command("RTL_P25_IQ_STATUS", authenticated, hooks));
    EXPECT({"W:RTL_P25_IQ_STATUS source=p25 active=1 ready=0 bytes=4096 max_bytes=1048512 "
            "frequency_hz=851012500 rate=960000 last_path=\"/orcsdr/iq/p25_0001.orciq\"\n"});

    CHECK(handle_command("RTL_P25_ENCRYPTION_STATUS", authenticated, hooks));
    EXPECT({"W:RTL_P25_ENCRYPTION_STATUS detected=1 algid=84 kid=1A2B muted_frames=205 "
            "returns=206 skip_enabled=1\n"});

    CHECK(handle_command("RTL_P25_MODULATION", authenticated, hooks));
    EXPECT({"C:snapshot",
            "W:RTL_P25_MODULATION configured=cqpsk selected=c4fm timing_gain=0.005000 carrier_gain=0.008000\n"});

    // The snapshot is taken first, then the device facts, then one write for the status line and one per candidate.
    CHECK(handle_command("RTL_P25_STATUS", authenticated, hooks));
    EXPECT({"C:snapshot", "C:status", kStatusLine,
            "W:RTL_P25_CANDIDATE index=0 frequency_hz=851012500 relative_dbfs=-41.5\n",
            "W:RTL_P25_CANDIDATE index=1 frequency_hz=851037500 relative_dbfs=-120.0\n"});

    // Not probing: the grant printed is the decoder's current one.
    CHECK(handle_command("RTL_P25_PHASE2_STATUS", authenticated, hooks));
    EXPECT({"C:snapshot", "C:phase2_probe_grant", "C:phase2_events",
            "W:RTL_P25_PHASE2_STATUS trace=1 active=1 grants=201 mapping_errors=107 "
            "syncs=202 rejections=203 "
            "tg=1201 src=7654321 carrier_hz=853212500 slot=1 channel_id=3 channel=456 service=4A "
            "encrypted=1 wacn=ABCDE sysid=2F1 rfss=5 site=6 symbols=108 "
            "sync_words=109 complete_bursts=110 truncated_bursts=111 duid_codeword=3C "
            "duid_valid=1 duid=5 duid_errors=2 voice_bursts=112 control_bursts=113 "
            "unknown_bursts=114 reverse=1 best_sync_errors=7 last_sync_ms=121 "
            "last_burst_ms=122\n"});

    g_trace = false;
    CHECK(handle_command("RTL_P25_PHASE2_TRACE", authenticated, hooks));
    EXPECT({"W:RTL_P25_PHASE2_TRACE enabled=0\n"});
    g_trace = true;
    CHECK(handle_command("RTL_P25_PHASE2_TRACE", authenticated, hooks));
    EXPECT({"W:RTL_P25_PHASE2_TRACE enabled=1\n"});

    CHECK(handle_command("RTL_P25_PROFILE_LIST", authenticated, hooks));
    EXPECT({"W:RTL_P25_PROFILE_LIST_BEGIN count=2 active=\"county\"\n",
            "W:RTL_P25_PROFILE index=0 id=\"city\" name=\"City Police\" active=0\n",
            "W:RTL_P25_PROFILE index=1 id=\"county\" name=\"County Fire\" active=1\n",
            "L:RTL_P25_PROFILE_LIST_DONE"});
  }

  // Status variants: nothing exported yet, a short algorithm id, the other follow states, a probed grant.
  g_iq.last_path = "";
  g_iq.source = "none";
  g_iq.active = false;
  g_iq.ready = true;
  CHECK(handle_command("RTL_P25_IQ_STATUS", false, hooks));
  EXPECT({"W:RTL_P25_IQ_STATUS source=none active=0 ready=1 bytes=4096 max_bytes=1048512 "
          "frequency_hz=851012500 rate=960000 last_path=\"none\"\n"});

  g_encryption = Encryption{};
  g_encryption.last = 0x000A0005u;
  CHECK(handle_command("RTL_P25_ENCRYPTION_STATUS", false, hooks));
  EXPECT({"W:RTL_P25_ENCRYPTION_STATUS detected=0 algid=0A kid=0005 muted_frames=0 "
          "returns=0 skip_enabled=0\n"});

  g_status.follow = Follow::control;
  CHECK(status_line(hooks).find(" follow=control control_hz=") != std::string::npos);
  g_status.follow = Follow::phase2_probe;
  CHECK(status_line(hooks).find(" follow=phase2_probe control_hz=") != std::string::npos);
  g_status.follow = Follow::voice;

  // No control channels: not configured, and no candidate lines.
  g_config.control_channel_count = 0;
  CHECK(handle_command("RTL_P25_STATUS", true, hooks));
  CHECK(g_log.size() == 3 && g_log[2].rfind("W:RTL_P25_STATUS configured=0 profile_id=\"county\" ", 0) == 0);
  g_log.clear();
  g_config.control_channel_count = 2;

  g_probing = true;
  g_events = Phase2Events{};
  g_snapshot.phase2_acquisition = false;
  CHECK(handle_command("RTL_P25_PHASE2_STATUS", false, hooks));
  EXPECT({"C:snapshot", "C:phase2_probe_grant", "C:phase2_events",
          "W:RTL_P25_PHASE2_STATUS trace=0 active=0 grants=0 mapping_errors=107 "
          "syncs=0 rejections=0 "
          "tg=2202 src=11 carrier_hz=854000000 slot=0 channel_id=1 channel=9 service=00 "
          "encrypted=0 wacn=00001 sysid=002 rfss=3 site=4 symbols=108 "
          "sync_words=109 complete_bursts=110 truncated_bursts=111 duid_codeword=3C "
          "duid_valid=1 duid=5 duid_errors=2 voice_bursts=112 control_bursts=113 "
          "unknown_bursts=114 reverse=1 best_sync_errors=7 last_sync_ms=121 "
          "last_burst_ms=122\n"});
  g_probing = false;

  // An empty profile store.
  {
    const p25config::StoreState saved = g_profiles;
    g_profiles = p25config::StoreState{};
    CHECK(handle_command("RTL_P25_PROFILE_LIST", false, hooks));
    EXPECT({"W:RTL_P25_PROFILE_LIST_BEGIN count=0 active=\"\"\n", "L:RTL_P25_PROFILE_LIST_DONE"});
    g_profiles = saved;
  }

  // Every route that changes something refuses an unauthenticated caller first: the refusal and nothing else.
  CHECK(handle_command("RTL_P25_IQ_START", false, hooks));
  EXPECT({"L:RTL_P25_IQ_ERROR auth_required"});
  CHECK(handle_command("RTL_P25_IQ_STOP", false, hooks));
  EXPECT({"L:RTL_P25_IQ_ERROR auth_required"});
  CHECK(handle_command("RTL_P25_REPLAY /orcsdr/iq/a.orciq", false, hooks));
  EXPECT({"L:RTL_P25_REPLAY_ERROR auth_required"});
  CHECK(handle_command("RTL_P25_MODULATION C4FM", false, hooks));
  EXPECT({"L:RTL_P25_MODULATION_ERROR auth_required"});
  CHECK(handle_command("RTL_P25_MODULATION bogus", false, hooks));   // refused before the value is looked at
  EXPECT({"L:RTL_P25_MODULATION_ERROR auth_required"});
  CHECK(handle_command("RTL_P25_PHASE2_TRACE ON", false, hooks));
  EXPECT({"L:RTL_P25_PHASE2_TRACE_ERROR auth_required"});
  CHECK(handle_command("RTL_P25_PROFILE_SELECT county", false, hooks));
  EXPECT({"L:RTL_P25_PROFILE_ERROR auth_required"});
  CHECK(handle_command("RTL_P25_PROFILE_IMPORT /a.cfg city", false, hooks));
  EXPECT({"L:RTL_P25_PROFILE_ERROR auth_required"});
  CHECK(handle_command("RTL_P25_PROFILE_EXPORT city /a.cfg", false, hooks));
  EXPECT({"L:RTL_P25_PROFILE_ERROR auth_required"});
  CHECK(handle_command("RTL_P25_PROFILE_RENAME city New Name", false, hooks));
  EXPECT({"L:RTL_P25_PROFILE_ERROR auth_required"});
  CHECK(handle_command("RTL_P25_PROFILE_DELETE city CONFIRM", false, hooks));
  EXPECT({"L:RTL_P25_PROFILE_ERROR auth_required"});
  CHECK(g_config.modulation == p25core::Modulation::cqpsk);
  CHECK(g_trace);

  // CONFIG_RELOAD and SCAN are only routes for an authenticated caller: otherwise the line is not taken at all,
  // so the dispatcher carries on to its later routes.
  CHECK(!handle_command("RTL_P25_CONFIG_RELOAD", false, hooks));
  CHECK(!handle_command("RTL_P25_SCAN", false, hooks));
  EXPECT({});
  CHECK(handle_command("RTL_P25_CONFIG_RELOAD", true, hooks));
  EXPECT({"C:config_reload", "W:RTL_P25_CONFIG_RELOAD status=\"loaded\"\n"});
  CHECK(handle_command("RTL_P25_SCAN", true, hooks));
  EXPECT({"C:scan"});

  // IQ capture and replay. The actions print their own results.
  CHECK(handle_command("RTL_P25_IQ_START", true, hooks));
  EXPECT({"C:iq_start"});
  CHECK(handle_command("RTL_P25_IQ_STOP", true, hooks));
  EXPECT({"C:iq_capture_is_p25", "C:iq_stop_and_export"});
  g_capture_is_p25 = false;
  CHECK(handle_command("RTL_P25_IQ_STOP", true, hooks));
  EXPECT({"C:iq_capture_is_p25", "L:RTL_P25_IQ_ERROR no_p25_capture"});
  g_capture_is_p25 = true;
  CHECK(handle_command("RTL_P25_REPLAY /orcsdr/iq/a b.orciq", true, hooks));   // the rest of the line, as given
  EXPECT({"C:replay [/orcsdr/iq/a b.orciq]"});
  CHECK(handle_command("RTL_P25_REPLAY ", true, hooks));
  EXPECT({"C:replay []"});

  // Modulation: exact upper-case names only; the reply shows the configuration after the change.
  CHECK(handle_command("RTL_P25_MODULATION C4FM", true, hooks));
  EXPECT({"C:set_modulation", "W:RTL_P25_MODULATION_OK configured=c4fm\n"});
  CHECK(g_config.modulation == p25core::Modulation::c4fm);
  CHECK(handle_command("RTL_P25_MODULATION AUTO", true, hooks));
  EXPECT({"C:set_modulation", "W:RTL_P25_MODULATION_OK configured=auto\n"});
  CHECK(g_config.modulation == p25core::Modulation::auto_detect);
  CHECK(handle_command("RTL_P25_MODULATION CQPSK", true, hooks));
  EXPECT({"C:set_modulation", "W:RTL_P25_MODULATION_OK configured=cqpsk\n"});
  CHECK(g_config.modulation == p25core::Modulation::cqpsk);
  for (const char* line : {"RTL_P25_MODULATION c4fm", "RTL_P25_MODULATION ", "RTL_P25_MODULATION C4FM ",
                           "RTL_P25_MODULATION  AUTO"}) {
    CHECK(handle_command(line, true, hooks));
    EXPECT({"L:RTL_P25_MODULATION_ERROR usage: RTL_P25_MODULATION AUTO|C4FM|CQPSK"});
  }
  CHECK(g_config.modulation == p25core::Modulation::cqpsk);

  // Phase II trace switch.
  CHECK(handle_command("RTL_P25_PHASE2_TRACE OFF", true, hooks));
  EXPECT({"C:set_phase2_trace 0", "W:RTL_P25_PHASE2_TRACE_OK enabled=0\n"});
  CHECK(handle_command("RTL_P25_PHASE2_TRACE ON", true, hooks));
  EXPECT({"C:set_phase2_trace 1", "W:RTL_P25_PHASE2_TRACE_OK enabled=1\n"});
  for (const char* line : {"RTL_P25_PHASE2_TRACE on", "RTL_P25_PHASE2_TRACE ", "RTL_P25_PHASE2_TRACE ON "}) {
    CHECK(handle_command(line, true, hooks));
    EXPECT({"L:RTL_P25_PHASE2_TRACE_ERROR usage: RTL_P25_PHASE2_TRACE ON|OFF"});
  }
  CHECK(g_trace);

  // Profile select: the id is the rest of the line; the error text is whatever the store reported (it may be empty).
  CHECK(handle_command("RTL_P25_PROFILE_SELECT county", true, hooks));
  EXPECT({"C:select [county]", "W:RTL_P25_PROFILE_OK operation=select id=\"county\"\n"});
  g_profile_ok = false;
  g_profile_error = "profile_not_found";
  CHECK(handle_command("RTL_P25_PROFILE_SELECT nope", true, hooks));
  EXPECT({"C:select [nope]", "W:RTL_P25_PROFILE_ERROR operation=select detail=\"profile_not_found\"\n"});
  g_profile_error = "";
  CHECK(handle_command("RTL_P25_PROFILE_SELECT nope", true, hooks));
  EXPECT({"C:select [nope]", "W:RTL_P25_PROFILE_ERROR operation=select detail=\"\"\n"});
  g_profile_ok = true;

  // Profile import: <path> <id>; reloads the configuration on success.
  CHECK(handle_command("RTL_P25_PROFILE_IMPORT /orcsdr/in.cfg city", true, hooks));
  EXPECT({"C:import [/orcsdr/in.cfg] [city]", "C:load_config",
          "W:RTL_P25_PROFILE_OK operation=import id=\"city\" source=\"/orcsdr/in.cfg\"\n"});
  CHECK(handle_command("RTL_P25_PROFILE_IMPORT /orcsdr/in.cfg", true, hooks));   // one argument: the store is not asked
  EXPECT({"W:RTL_P25_PROFILE_ERROR operation=import detail=\"usage\"\n"});
  CHECK(handle_command("RTL_P25_PROFILE_IMPORT ", true, hooks));
  EXPECT({"W:RTL_P25_PROFILE_ERROR operation=import detail=\"usage\"\n"});
  g_profile_ok = false;
  g_profile_error = "invalid_profile";
  CHECK(handle_command("RTL_P25_PROFILE_IMPORT /orcsdr/in.cfg city", true, hooks));
  EXPECT({"C:import [/orcsdr/in.cfg] [city]", "W:RTL_P25_PROFILE_ERROR operation=import detail=\"invalid_profile\"\n"});
  g_profile_error = "";   // e.g. no SD card: the reply says "usage"
  CHECK(handle_command("RTL_P25_PROFILE_IMPORT /orcsdr/in.cfg city", true, hooks));
  EXPECT({"C:import [/orcsdr/in.cfg] [city]", "W:RTL_P25_PROFILE_ERROR operation=import detail=\"usage\"\n"});
  g_profile_ok = true;

  // Profile export: <id> <path>; nothing is reloaded.
  CHECK(handle_command("RTL_P25_PROFILE_EXPORT city /orcsdr/exports/city.cfg", true, hooks));
  EXPECT({"C:export [city] [/orcsdr/exports/city.cfg]",
          "W:RTL_P25_PROFILE_OK operation=export id=\"city\" path=\"/orcsdr/exports/city.cfg\"\n"});
  CHECK(handle_command("RTL_P25_PROFILE_EXPORT city", true, hooks));
  EXPECT({"W:RTL_P25_PROFILE_ERROR operation=export detail=\"usage\"\n"});
  g_profile_ok = false;
  g_profile_error = "write_failed";
  CHECK(handle_command("RTL_P25_PROFILE_EXPORT city /x.cfg", true, hooks));
  EXPECT({"C:export [city] [/x.cfg]", "W:RTL_P25_PROFILE_ERROR operation=export detail=\"write_failed\"\n"});
  g_profile_error = "";
  CHECK(handle_command("RTL_P25_PROFILE_EXPORT city /x.cfg", true, hooks));
  EXPECT({"C:export [city] [/x.cfg]", "W:RTL_P25_PROFILE_ERROR operation=export detail=\"usage\"\n"});
  g_profile_ok = true;

  // Profile rename: <id> <name, may hold spaces>; the configuration is reloaded only for the active profile.
  CHECK(handle_command("RTL_P25_PROFILE_RENAME city City Police North", true, hooks));
  EXPECT({"C:rename [city] [City Police North]",
          "W:RTL_P25_PROFILE_OK operation=rename id=\"city\" name=\"City Police North\"\n"});
  CHECK(handle_command("RTL_P25_PROFILE_RENAME county County", true, hooks));
  EXPECT({"C:rename [county] [County]", "C:load_config",
          "W:RTL_P25_PROFILE_OK operation=rename id=\"county\" name=\"County\"\n"});
  CHECK(handle_command("RTL_P25_PROFILE_RENAME city", true, hooks));
  EXPECT({"W:RTL_P25_PROFILE_ERROR operation=rename detail=\"usage\"\n"});
  g_profile_ok = false;
  g_profile_error = "profile_not_found";
  CHECK(handle_command("RTL_P25_PROFILE_RENAME county County", true, hooks));
  EXPECT({"C:rename [county] [County]", "W:RTL_P25_PROFILE_ERROR operation=rename detail=\"profile_not_found\"\n"});
  g_profile_error = "";
  CHECK(handle_command("RTL_P25_PROFILE_RENAME county County", true, hooks));
  EXPECT({"C:rename [county] [County]", "W:RTL_P25_PROFILE_ERROR operation=rename detail=\"usage\"\n"});
  g_profile_ok = true;

  // Profile delete: <id> CONFIRM, the word exactly; without it the store is not asked.
  CHECK(handle_command("RTL_P25_PROFILE_DELETE city CONFIRM", true, hooks));
  EXPECT({"C:delete [city]", "C:load_config", "W:RTL_P25_PROFILE_OK operation=delete id=\"city\"\n"});
  for (const char* line : {"RTL_P25_PROFILE_DELETE city", "RTL_P25_PROFILE_DELETE city confirm",
                           "RTL_P25_PROFILE_DELETE city CONFIRMED", "RTL_P25_PROFILE_DELETE "}) {
    CHECK(handle_command(line, true, hooks));
    EXPECT({"W:RTL_P25_PROFILE_ERROR operation=delete detail=\"confirmation_required\"\n"});
  }
  g_profile_ok = false;
  g_profile_error = "active_profile";
  CHECK(handle_command("RTL_P25_PROFILE_DELETE county CONFIRM", true, hooks));
  EXPECT({"C:delete [county]", "W:RTL_P25_PROFILE_ERROR operation=delete detail=\"active_profile\"\n"});
  g_profile_error = "";
  CHECK(handle_command("RTL_P25_PROFILE_DELETE county CONFIRM", true, hooks));
  EXPECT({"C:delete [county]", "W:RTL_P25_PROFILE_ERROR operation=delete detail=\"confirmation_required\"\n"});
  g_profile_ok = true;

  // Lines that are not this family's: exact names, and the argument forms need their trailing space.
  for (bool authenticated : {false, true}) {
    for (const char* line :
         {"RTL_P25", "RTL_P25_STATUS ", "RTL_P25_STATUS X", "rtl_p25_status", " RTL_P25_STATUS", "RTL_P25_IQ_START ",
          "RTL_P25_IQ_STOP NOW", "RTL_P25_IQ_STATUS ", "RTL_P25_REPLAY", "RTL_P25_ENCRYPTION_STATUS ",
          "RTL_P25_PHASE2_STATUS ", "RTL_P25_PROFILE_LIST ", "RTL_P25_PROFILE_SELECT", "RTL_P25_PROFILE_IMPORT",
          "RTL_P25_PROFILE_EXPORT", "RTL_P25_PROFILE_RENAME", "RTL_P25_PROFILE_DELETE", "RTL_P25_CONFIG_RELOAD ",
          "RTL_P25_SCAN ", "RTL_P25_SCAN NOW", "RTL_TUNE P25 851012500", "RTL_HELP", ""}) {
      CHECK(!handle_command(line, authenticated, hooks));
    }
  }
  EXPECT({});

  if (failures == 0) std::printf("serial_p25 tests passed\n");
  return failures == 0 ? 0 : 1;
}
