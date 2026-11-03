// P25 screen: the part of OrcSDR's main.cpp that owns the P25 monitor (system profiles, candidate control channels, survey, hold),
// driven by the node's P25 decoder statistics instead of an on-board decoder. Voice following (IMBE) is not part of the node
// decoder, so the screen shows the control channel only: identity, grants, decode quality.
#include <M5Unified.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "dashboard_audio_control.hpp"
#include "data_screens.hpp"
#include "p25_dashboard.hpp"

namespace {

using namespace orcsdr;
using namespace dscreens;

constexpr uint32_t kSurveyDwellMs = 1500;   // main.cpp: dwell_ms=1500

// A profile the page can offer before the user has imported one: the control channel of the project owner's own recording
// (tests/decoders/vectors): NAC 0x1F0, WACN 0xBEE00, system 0x1F3, written in decimal as the profile format wants. Talkgroup aliases
// are not known, so they are left generic.
const char kSampleProfile[] =
    "version=2\n"
    "system_name=Lane County OR (sample)\n"
    "nac=496\n"
    "wacn=781824\n"
    "system_id=499\n"
    "rfss=1\n"
    "site=1\n"
    "control_channel_hz=453925000\n"
    "talkgroup=20051,TG 20051\n"
    "talkgroup=20101,TG 20101\n";

p25::Snapshot g_snap;
std::string g_texts[p25config::kMaxProfiles];
bool g_receiving = false, g_entered = false;
uint32_t g_survey_until = 0, g_last_publish = 0;
uint32_t g_survey_tsbk[p25config::kMaxControlChannels]{};
char g_request[160];
bool g_request_pending = false;
float g_rf_dbfs = -90;
uint32_t g_sps = 960000;

void request_start(uint32_t hz) {
  snprintf(g_request, sizeof g_request, "{\"op\":\"start\",\"hz\":%u,\"options\":\"mod=auto\"}", (unsigned)hz);
  g_request_pending = true;
}

bool load_profile(size_t index) {
  if (index >= g_snap.profiles.count || g_texts[index].empty()) return false;
  char error[48] = {};
  p25config::Config cfg;
  if (!p25config::parse(g_texts[index].c_str(), &cfg, error, sizeof error)) {
    snprintf(g_snap.config_status, sizeof g_snap.config_status, "Profile error: %.40s", error);
    ++g_snap.config_revision;
    return false;
  }
  g_snap.config = cfg;
  g_snap.profiles.active_index = (int8_t)index;
  snprintf(g_snap.profiles.active_id, sizeof g_snap.profiles.active_id, "%s", g_snap.profiles.profiles[index].id);
  g_snap.auto_follow = cfg.auto_follow;
  g_snap.encryption_skip = cfg.encryption_skip;
  g_snap.hold_talkgroup = cfg.hold_talkgroup;
  g_snap.hold = cfg.hold_talkgroup != 0;
  g_snap.candidate_count = cfg.control_channel_count;
  g_snap.candidate_index = 0;
  for (size_t i = 0; i < cfg.control_channel_count; ++i)
    if (cfg.control_channels_hz[i] == cfg.last_control_channel_hz) g_snap.candidate_index = (uint8_t)i;
  snprintf(g_snap.config_status, sizeof g_snap.config_status, "Profile %.40s", cfg.system_name);
  ++g_snap.config_revision;
  return true;
}

bool add_profile(const char* text) {
  if (g_snap.profiles.count >= p25config::kMaxProfiles) return false;
  char error[48] = {};
  p25config::Config cfg;
  if (!p25config::parse(text, &cfg, error, sizeof error)) return false;
  const size_t i = g_snap.profiles.count;
  p25config::ProfileSummary& p = g_snap.profiles.profiles[i];
  snprintf(p.name, sizeof p.name, "%s", cfg.system_name);
  char id[sizeof p.id];
  size_t n = 0;
  for (const char* c = cfg.system_name; *c && n + 1 < sizeof id; ++c) id[n++] = isalnum((unsigned char)*c) ? (char)tolower((unsigned char)*c) : '-';
  id[n] = 0;
  snprintf(p.id, sizeof p.id, "%s", id);
  g_texts[i] = text;
  g_snap.profiles.count = (uint8_t)(i + 1);
  return true;
}

void publish(uint32_t now) {
  g_snap.relative_dbfs = g_rf_dbfs;
  g_snap.running = g_receiving;
  g_snap.driver_ready = true;
  g_snap.wifi_connected = true;
  g_snap.sound_enabled = false;
  g_snap.volume = 0;
  g_snap.effective_sps = g_receiving ? g_sps : 0;
  g_snap.target_sps = 960000;
  g_snap.frequency_hz = g_snap.config.control_channel_count ? g_snap.config.control_channels_hz[g_snap.candidate_index] : 0;
  g_snap.battery_percent = -1;
  g_last_publish = now;
}

void enter(uint32_t now) {
  if (g_snap.profiles.count == 0) {
    add_profile(kSampleProfile);
    load_profile(0);
  } else if (g_snap.config.control_channel_count == 0) load_profile(0);
  publish(now);
  p25::enter(g_snap);
  g_entered = true;
  if (g_snap.config.control_channel_count) request_start(g_snap.config.control_channels_hz[g_snap.candidate_index]);
}

void leave() {
  g_entered = false;
  g_snap.survey_active = false;
  p25::leave();
}

void tune_candidate(size_t index) {
  g_snap.survey_active = false;
  g_snap.candidate_index = (uint8_t)index;
  request_start(g_snap.config.control_channels_hz[index]);
}

void update(uint32_t now) {
  if (g_snap.survey_active && now >= g_survey_until) {
    g_snap.candidate_levels[g_snap.candidate_index] = g_rf_dbfs;
    g_survey_tsbk[g_snap.candidate_index] = g_snap.decoded.tsbk_good;
    if (g_snap.candidate_index + 1u < g_snap.config.control_channel_count) {
      ++g_snap.candidate_index;
      g_survey_until = now + kSurveyDwellMs;
      request_start(g_snap.config.control_channels_hz[g_snap.candidate_index]);
    } else {
      g_snap.survey_active = false;
      const size_t count = g_snap.config.control_channel_count;
      size_t best = std::max_element(g_survey_tsbk, g_survey_tsbk + count) - g_survey_tsbk;
      if (g_survey_tsbk[best] == 0) best = std::max_element(g_snap.candidate_levels, g_snap.candidate_levels + count) - g_snap.candidate_levels;
      tune_candidate(best);
    }
  }
  if (now - g_last_publish >= 500) publish(now);
  p25::update(g_snap);
}

Result touch(int x, int y, bool pressed, uint32_t now) {
  Result r;
  if (!pressed) return r;
  if (audio_header::home_hit(x, y)) { r.exit = true; return r; }
  const p25::Action a = p25::handle_touch(x, y);
  const size_t count = g_snap.config.control_channel_count;
  using K = p25::ActionKind;
  switch (a.kind) {
    case K::exit_to_home: r.exit = true; break;
    case K::tune_hz:
      g_snap.survey_active = false;
      request_start(a.value);
      break;
    case K::previous_candidate:
      if (count) tune_candidate((g_snap.candidate_index + count - 1) % count);
      break;
    case K::next_candidate:
      if (count) tune_candidate((g_snap.candidate_index + 1) % count);
      break;
    case K::survey_toggle:
      if (g_snap.survey_active) { g_snap.survey_active = false; request_start(g_snap.frequency_hz); }
      else if (count) {
        std::fill(std::begin(g_snap.candidate_levels), std::end(g_snap.candidate_levels), -120.0f);
        memset(g_survey_tsbk, 0, sizeof g_survey_tsbk);
        g_snap.survey_active = true;
        g_snap.candidate_index = 0;
        g_survey_until = now + kSurveyDwellMs;
        request_start(g_snap.config.control_channels_hz[0]);
      } else {
        snprintf(g_snap.config_status, sizeof g_snap.config_status, "Select or import a P25 system");
        ++g_snap.config_revision;
      }
      break;
    case K::hold_toggle:
      if (g_snap.hold) { g_snap.hold = false; g_snap.hold_talkgroup = 0; }
      else {
        const auto& g = g_snap.decoded.current_grant;
        g_snap.hold_talkgroup = g.valid ? g.talkgroup : 0;
        g_snap.hold = true;
      }
      break;
    case K::hold_talkgroup:
      if (g_snap.hold && g_snap.hold_talkgroup == a.value) { g_snap.hold = false; g_snap.hold_talkgroup = 0; }
      else { g_snap.hold_talkgroup = (uint16_t)a.value; g_snap.hold = true; }
      break;
    case K::auto_follow_toggle: g_snap.auto_follow = !g_snap.auto_follow; break;
    case K::encryption_skip_toggle: g_snap.encryption_skip = !g_snap.encryption_skip; break;
    case K::select_profile:
      if (load_profile(a.value)) { g_snap.survey_active = false; if (g_snap.config.control_channel_count) request_start(g_snap.config.control_channels_hz[g_snap.candidate_index]); }
      break;
    case K::delete_profile:
      if (a.value < g_snap.profiles.count) {
        for (size_t i = a.value; i + 1 < g_snap.profiles.count; ++i) {
          g_snap.profiles.profiles[i] = g_snap.profiles.profiles[i + 1];
          g_texts[i] = g_texts[i + 1];
        }
        --g_snap.profiles.count;
        g_snap.profiles.active_index = -1;
        p25config::defaults(&g_snap.config);
        g_snap.candidate_count = 0;
        ++g_snap.config_revision;
      }
      break;
    case K::reload_config: ++g_snap.config_revision; break;
    default: break;   // sound/volume/span/rename/import/export need the device's SD card or speaker
  }
  if (a.kind != K::none) publish(now);
  return r;
}

uint32_t u(const char* line, const char* key) { return (uint32_t)num(line, key); }

void grant_from(const char* line, const char* prefix, p25core::Grant& g) {
  char k[24];
  auto key = [&](const char* name) { snprintf(k, sizeof k, "%s_%s", prefix, name); return k; };
  g = {};
  g.valid = flag(line, key("valid"));
  if (!g.valid) return;
  g.encrypted = flag(line, key("enc"));
  g.emergency = flag(line, key("emerg"));
  g.tdma = flag(line, key("tdma"));
  g.service_options = (uint8_t)u(line, key("svc"));
  g.channel_id = (uint8_t)u(line, key("chid"));
  g.channel_number = (uint16_t)u(line, key("chn"));
  g.slot = (uint8_t)u(line, key("slot"));
  g.talkgroup = (uint16_t)u(line, key("tg"));
  g.source_id = u(line, key("src"));
  g.frequency_hz = u(line, key("freq"));
  g.wacn = u(line, key("wacn"));
  g.system_id = (uint16_t)u(line, key("sys"));
  g.rfss = (uint8_t)u(line, key("rfss"));
  g.site = (uint8_t)u(line, key("site"));
  g.seen_ms = u(line, key("seen"));
}

void event(const char* line, uint32_t) {
  char type[16] = {};
  str(line, "type", type, sizeof type);
  if (strcmp(type, "stats")) return;
  p25core::Snapshot& d = g_snap.decoded;
  d = {};
  d.configured_modulation = static_cast<p25core::Modulation>(u(line, "configured_modulation"));
  d.selected_modulation = static_cast<p25core::Modulation>(u(line, "selected_modulation"));
  d.frame_sync = flag(line, "frame_sync");
  d.identity_valid = flag(line, "identity_valid");
  d.nac = (uint16_t)u(line, "nac");
  d.wacn = u(line, "wacn");
  d.system_id = (uint16_t)u(line, "system_id");
  d.rfss = (uint8_t)u(line, "rfss");
  d.site = (uint8_t)u(line, "site");
  d.sync_words = u(line, "sync_words");
  d.nid_good = u(line, "nid_good");
  d.nid_failed = u(line, "nid_failed");
  d.nid_corrected_bits = u(line, "nid_corrected_bits");
  d.tsbk_good = u(line, "tsbk_good");
  d.tsbk_failed = u(line, "tsbk_failed");
  d.phase2_band_plans = u(line, "phase2_band_plans");
  d.phase2_grants = u(line, "phase2_grants");
  d.phase2_mapping_errors = u(line, "phase2_mapping_errors");
  d.phase2_acquisition = flag(line, "phase2_acquisition");
  d.voice_ldus = u(line, "voice_ldus");
  d.voice_frames = u(line, "voice_frames");
  d.last_trellis_metric = (uint16_t)u(line, "last_trellis_metric");
  d.estimated_ber_percent = (float)num(line, "estimated_ber_percent");
  d.frame_error_percent = (float)num(line, "frame_error_percent");
  d.afc_offset_hz = (float)num(line, "afc_offset_hz");
  d.symbol_level = (float)num(line, "symbol_level");
  d.lock_quality_percent = (float)num(line, "lock_quality_percent");
  d.timing_error = (float)num(line, "timing_error");
  d.carrier_error_hz = (float)num(line, "carrier_error_hz");
  d.decode_rate_hz = (float)num(line, "decode_rate_hz");
  grant_from(line, "cg", d.current_grant);
  for (size_t i = 0; i < p25core::kRecentGrantCount; ++i) {
    char prefix[8];
    snprintf(prefix, sizeof prefix, "rg%u", (unsigned)i);
    grant_from(line, prefix, d.recent_grants[i]);
  }
}

void set_num(const char* key, double v) {
  if (!strcmp(key, "receiving")) g_receiving = v != 0;
  else if (!strcmp(key, "rf_dbfs")) g_rf_dbfs = (float)v;
  else if (!strcmp(key, "sps")) g_sps = (uint32_t)v;
}

void text(const char* key, const char* value) {
  if (!strcmp(key, "p25_profile") && add_profile(value) && g_entered) {
    snprintf(g_snap.config_status, sizeof g_snap.config_status, "Imported %.40s", g_snap.profiles.profiles[g_snap.profiles.count - 1].name);
    ++g_snap.config_revision;
  }
}

const char* take_request() {
  if (!g_request_pending) return nullptr;
  g_request_pending = false;
  return g_request;
}

void spectrum(const float* levels, int n, float floor_db) {
  if (!p25::spectrum_active()) return;
  const uint32_t span = g_snap.span_hz ? g_snap.span_hz : 960000;
  const int visible = (int)std::min<uint32_t>((uint32_t)n, (uint32_t)((uint64_t)n * span / 960000u));
  p25::draw_spectrum(levels, (size_t)((n - visible) / 2), (size_t)visible, floor_db);
}

}  // namespace

namespace dscreens {
extern const Screen kP25 = {dashboards::Id::p25, "p25", enter, leave, update, touch, event, set_num, take_request, spectrum, text};
}
