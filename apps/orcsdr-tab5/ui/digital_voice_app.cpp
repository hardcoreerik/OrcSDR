#include "digital_voice_app.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "digital_voice_dashboard_surface.hpp"
#include "dv_bandplan.hpp"
#include "dv_discovery_bridge.hpp"
#include "dv_radio.hpp"
#include "dv_scanner.hpp"
#include "dv_survey.hpp"
#include "dv_capture.hpp"
#include "dv_voice_service.hpp"
#include "screen_controller.hpp"

namespace orcsdr::digital_voice_app {
namespace {
using namespace digital_voice_ui;

// Everything lives in PSRAM for the lifetime of a visit: the Tab5 internal boot pool is nearly full.
struct State {
  // DISCOVER: channels with integrity-validated activity seen during the current discovery run (frequency + protocol + sightings).
  static constexpr uint8_t kDiscMax = 32, kDiscFound = 3;      // a channel is "found" after 3 sightings; fewer are candidates
  bool disc_running = false;
  uint8_t disc_n = 0;
  uint32_t disc_hz[kDiscMax]{};
  uint8_t disc_proto[kDiscMax]{}, disc_seen[kDiscMax]{};
  uint16_t disc_saved = 0, disc_skipped = 0;
  // Wideband pre-screen: phase 1 = survey the picked bands, 2 = decode the occupied channels in batches, 0 = off (or a plain sweep).
  uint8_t disc_phase = 0, sv_rate_idx = 0, hot_n = 0, hot_at = 0;
  bool sv_rate_held = false, sv_failed = false;
  uint32_t survey_passes = 0;
  dv_survey::Work* sv_work = nullptr;       // PSRAM, allocated on the first survey of a visit
  dv_survey::Survey survey;
  uint32_t hot_hz[dv_survey::kMaxHot]{};
  // DEVELOPMENT capture (RTL_DV CAPTURE): the live IQ stream to the card for offline analysis. Off unless asked for by command.
  uint8_t cap_phase = 0;                    // 0 off, 1 tuning/settling, 2 recording, 3 writing the sidecar
  bool cap_saving = false;
  uint32_t cap_settle_until = 0, cap_center = 0, cap_rate = 0, cap_seconds = 0;
  uint8_t* cap_mem = nullptr;               // PSRAM, 10 MB, allocated on the first capture of a visit
  dv_capture::Capture cap;
  char cap_json[4096];
  uint8_t filled_system_offset = 0;            // the Systems page offset the snapshot rows were last filled for
  bool background = false;          // the screen is hidden but the receiver, scanner and speech are still running
  Surface surface;
  Snapshot snapshot;
  dv::Scanner scanner;
  uint32_t last_loss_events = 0;
  uint32_t last_epoch = 0;
  uint32_t last_hold_count = 0;
  bool have_call = false;           // snapshot's live call fields belong to the CURRENT channel visit
  uint32_t call_epoch = 0;
  uint32_t actions_applied = 0;
  bool replaying = false;           // a recorded file is being fed through the decoders (events are labelled REPLAY)
  bool replay_done = false;
  uint32_t replay_start_ms = 0;
  uint64_t replay_fed = 0;          // bytes fed so far
  uint8_t replay_buf[16384];
  uint8_t plan_mask[dv::kMaxScanChannels]{};   // per plan entry: decoders to run (0 = auto: rotate within the dwell)
  uint32_t saved_hz[dv::kMaxScanChannels]{};   // the saved channel list (persisted as the `dv_plan` setting)
  uint8_t saved_mask[dv::kMaxScanChannels]{};
  uint8_t saved_n = 0;
  uint8_t plan_option = 0;                     // 0 = saved list, 1..kBandPresetCount = a band preset, then MY BANDS, then the custom range
  uint16_t band_mask = 0x0012;                 // MY BANDS: bit i = built-in band i. Default VHF business + UHF business; persisted as dv_bands
  uint32_t custom_start = 0, custom_step = 0;  // RTL_DV RANGE; 0 = none
  size_t custom_count = 0;
  // Session ledger of channels that showed activity: what the SCAN counters report (the Last Heard list holds only the newest few).
  uint32_t led_hz[64]{};
  uint8_t led_proto[64]{}, led_level[64]{};    // level 1 = sync only, 2 = integrity, 3 = identity
  bool led_new[64]{};
  uint8_t led_n = 0;
  bool visit_sync_logged = false, visit_counted = false;
  uint32_t base_dmr = 0, base_n48 = 0, base_n96 = 0;      // decoder counters when the current channel visit began
  uint8_t mask_override = 0;                   // RTL_DV MASK: forces one mask on every channel
  uint8_t auto_phase = 0;
  uint32_t auto_switch_ms = 0;
  dv::Discovery db;                  // persistent: live observations only (what the Tab5's own receiver validated)
  dv::Discovery replay_db;           // RAM only: recorded-IQ replays never touch the persistent database
  bool show_replay_db = false;       // the newest event came from a replay (and is not being learned)
  bool learn_replay = false;         // RTL_DV DB LEARN_REPLAY ON: the owner chose to let recorded captures feed the persistent database
  uint32_t db_saved_revision = 0, db_saved_structure = 0, db_last_save_ms = 0, db_saves = 0, db_save_failures = 0;
  bool db_saving = false;
  bool rec_saving = false;           // a speech recording or IQ capture is being written by the shared SD writer
  bool rec_is_iq = false;            // which of the two
  uint16_t iq_files = 0;
  uint16_t speech_files = 0;
  bool have_identity = false;        // the current-activity fields hold an identity-validated call (kept over later control bursts)
  uint32_t last_identity_ms = 0, identity_freq = 0;
};
constexpr uint32_t kIdentityStickyMs = 15000;
void service_persistence(State& st, bool final_flush);
char* g_persist = nullptr;           // SD write buffer: allocated once in PSRAM and never freed (the writer may still read it)
State* g_state = nullptr;
Hooks g_hooks{};
uint32_t g_open_failed = 0, g_unapplied_actions = 0, g_repaints = 0, g_painted_revision = 0;
uint64_t g_render_us_total = 0;                 // time spent in Surface::update when it drew something (UI task), from the micros hook
uint32_t g_render_n = 0, g_render_us_max = 0;

uint32_t now() { return g_hooks.now_ms ? g_hooks.now_ms() : 0; }
bool overlay() { return g_hooks.overlay_open && g_hooks.overlay_open(); }

bool ensure_state() {
  if (g_state) return true;
  void* mem = heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) return false;
  g_state = new (mem) State();
  return true;
}
void free_state() {
  if (!g_state) return;
  g_state->~State();
  heap_caps_free(g_state);
  g_state = nullptr;
}

void start_voice_service() {
  dv_voice_service::Hooks h;
  h.audio_out = g_hooks.audio_out;
  h.audio_allowed = g_hooks.audio_allowed;
  h.audio_path_ready = g_hooks.audio_path_ready;
  h.set_exclusive = g_hooks.audio_exclusive;
  h.now_ms = g_hooks.now_ms;
  h.utc_epoch = g_hooks.utc_epoch;
  dv_voice_service::fill_platform_hooks(h);   // pump task on core 1, PSRAM stack
  dv_voice_service::install(h);
  if (g_hooks.flag_load) { dv_voice_service::set_save_speech(g_hooks.flag_load("dv_rec")); dv_radio::iq_set_enabled(g_hooks.flag_load("dv_iq")); }   // off unless the owner turned it on
  (void)dv_voice_service::start();            // false = no memory: the LIVE page then says AUDIO UNAVAILABLE
}

VoicePhase voice_phase_of(dv_voice::Phase p) {
  switch (p) {
    case dv_voice::Phase::listening: return VoicePhase::listening;
    case dv_voice::Phase::acquiring: return VoicePhase::acquiring;
    case dv_voice::Phase::buffering: return VoicePhase::buffering;
    case dv_voice::Phase::decoding: return VoicePhase::decoding;
    case dv_voice::Phase::silence_frame: return VoicePhase::silence_frame;
    case dv_voice::Phase::degraded: return VoicePhase::degraded;
    case dv_voice::Phase::call_ended: return VoicePhase::call_ended;
    case dv_voice::Phase::protected_voice: return VoicePhase::protected_voice;
    case dv_voice::Phase::privacy_unknown: return VoicePhase::privacy_unknown;
    case dv_voice::Phase::muted: return VoicePhase::muted;
    default: return VoicePhase::unavailable;
  }
}

// Voice panel facts for the LIVE page. Quantised so the panel repaints only when something visible changed.
void refresh_voice(Snapshot& s) {
  const dv_voice_service::Published p = dv_voice_service::snapshot();
  VoiceAudioView v;
  v.available = p.running;
  v.phase = p.running ? voice_phase_of(p.engine.phase) : VoicePhase::unavailable;
  v.listen = static_cast<ListenMode>(static_cast<uint8_t>(p.engine.listen));
  v.active_slot = p.engine.active_slot;
  const int rms = p.engine.stats.rms_last;
  v.level = static_cast<uint8_t>(rms <= 0 ? 0 : rms >= 5600 ? 8 : rms / 700 + 1);
  if (v.phase != VoicePhase::decoding && v.phase != VoicePhase::degraded) v.level = 0;
  v.privacy_ts1 = p.engine.privacy[1]; v.privacy_ts2 = p.engine.privacy[2];
  v.speech_frames = static_cast<uint32_t>(p.engine.stats.frames_speech);
  v.silence_frames = static_cast<uint32_t>(p.engine.stats.frames_silence);
  v.error_frames = static_cast<uint32_t>(p.engine.stats.frames_error);
  v.concealed_frames = static_cast<uint32_t>(p.engine.stats.frames_concealed);
  v.underruns = static_cast<uint32_t>(p.engine.stats.underruns);
  v.buffered_ms = static_cast<uint16_t>(p.engine.stats.buffered_ms / 20u * 20u);
  v.latency_ms = static_cast<uint16_t>(p.engine.stats.latency_ms_last);
  v.source_id = p.engine.source_id; v.destination_id = p.engine.destination_id; v.group_call = p.engine.group_call;
  const VoiceAudioView& o = s.voice;
  if (v.available != o.available || v.phase != o.phase || v.listen != o.listen || v.active_slot != o.active_slot ||
      v.level != o.level || v.privacy_ts1 != o.privacy_ts1 || v.privacy_ts2 != o.privacy_ts2 ||
      v.speech_frames != o.speech_frames || v.silence_frames != o.silence_frames || v.error_frames != o.error_frames ||
      v.buffered_ms != o.buffered_ms || v.source_id != o.source_id || v.destination_id != o.destination_id) {
    s.voice = v;
    ++s.audio_revision;
  }
}

void install_radio_hooks() {
  dv_radio::Hooks h;
  h.device_ready = g_hooks.receiver_ready;
  h.tune = g_hooks.tune;
  h.release = g_hooks.release;
  h.now_ms = g_hooks.now_ms;
  h.micros = g_hooks.micros;
  h.lost_samples = g_hooks.lost_samples;
  h.utc_epoch = g_hooks.utc_epoch;
  dv_radio::fill_platform_hooks(h);   // decode worker on core 0: the DSP task never decodes
  dv_radio::install(h);
}

// "460.5,461.0:dmr,454.225:nxdn48" (MHz when < 10000, else Hz). Optional protocol per entry: dmr | nxdn48 | nxdn96 | auto
// (auto = rotate the decoders within the dwell). Returns the entry count, 0 on any invalid entry.
size_t parse_plan(const char* csv, uint32_t* out, uint8_t* masks, size_t capacity) {
  size_t n = 0;
  const char* p = csv;
  while (*p) {
    char* end = nullptr;
    const double v = std::strtod(p, &end);
    if (end == p) return 0;
    const double hz = v < 10000.0 ? v * 1e6 : v;
    if (n >= capacity || hz < 1.0 || hz > 4.0e9) return 0;
    uint8_t mask = 0;
    p = end;
    if (*p == ':') {
      ++p;
      if (std::strncmp(p, "dmr", 3) == 0) { mask = dv_radio::kMaskDmr; p += 3; }
      else if (std::strncmp(p, "nxdn48", 6) == 0) { mask = dv_radio::kMaskNxdn48; p += 6; }
      else if (std::strncmp(p, "nxdn96", 6) == 0) { mask = dv_radio::kMaskNxdn96; p += 6; }
      else if (std::strncmp(p, "auto", 4) == 0) { mask = 0; p += 4; }
      else return 0;
    }
    out[n] = static_cast<uint32_t>(std::llround(hz));
    masks[n++] = mask;
    while (*p == ' ') ++p;
    if (*p == ',') ++p;
    else if (*p) return 0;
  }
  return n;
}

uint8_t mask_for_protocol(Protocol p) {
  switch (p) {
    case Protocol::dmr: return dv_radio::kMaskDmr;
    case Protocol::nxdn48: return dv_radio::kMaskNxdn48;
    case Protocol::nxdn96: return dv_radio::kMaskNxdn96;
    default: return 0;
  }
}
Protocol protocol_for_mask(uint8_t m) {
  if (m == dv_radio::kMaskDmr) return Protocol::dmr;
  if (m == dv_radio::kMaskNxdn48) return Protocol::nxdn48;
  if (m == dv_radio::kMaskNxdn96) return Protocol::nxdn96;
  return Protocol::unknown;
}
size_t plan_option_count(const State& st) { return 2 + dv::kBandPresetCount + (st.custom_start ? 1 : 0); }
size_t my_bands_channels(const State& st) {
  size_t n = 0;
  for (size_t i = 0; i < dv::kBandPresetCount; ++i) if ((st.band_mask >> i) & 1U) n += dv::channel_count(dv::kBandPresets[i]);
  return n;
}
size_t my_bands_count(const State& st) {
  size_t n = 0;
  for (size_t i = 0; i < dv::kBandPresetCount; ++i) n += (st.band_mask >> i) & 1U;
  return n;
}

// The scan plan follows the selected option: the saved channel list, a built-in band range, or the custom range.
void apply_plan_option(State& st) {
  if (st.scanner.active()) return;
  if (st.plan_option >= plan_option_count(st)) st.plan_option = 0;
  if (st.plan_option == 0) {
    if (st.saved_n && st.scanner.set_plan(st.saved_hz, st.saved_n)) std::memcpy(st.plan_mask, st.saved_mask, sizeof(st.plan_mask));
    else st.scanner.clear_plan();
  } else if (st.plan_option <= dv::kBandPresetCount) {
    const dv::BandPreset& p = dv::kBandPresets[st.plan_option - 1];
    if (!st.scanner.set_range(p.start_hz, p.step_hz, dv::channel_count(p))) st.scanner.clear_plan();
  } else if (st.plan_option == 1 + dv::kBandPresetCount) {      // MY BANDS: the picked bands scanned in order as one plan
    dv::Segment seg[dv::kMaxSegments];
    size_t n = 0;
    for (size_t i = 0; i < dv::kBandPresetCount && n < dv::kMaxSegments; ++i)
      if ((st.band_mask >> i) & 1U) seg[n++] = {dv::kBandPresets[i].start_hz, dv::kBandPresets[i].step_hz, static_cast<uint32_t>(dv::channel_count(dv::kBandPresets[i]))};
    if (!n || !st.scanner.set_ranges(seg, n)) st.scanner.clear_plan();
  } else if (!st.scanner.set_range(st.custom_start, st.custom_step, st.custom_count)) {
    st.scanner.clear_plan();
  }
}

void save_list(State& st) {
  if (!g_hooks.save_plan) return;
  char csv[400];
  size_t w = 0;
  csv[0] = 0;
  for (size_t i = 0; i < st.saved_n && w + 28 < sizeof(csv); ++i) {
    const uint8_t m = st.saved_mask[i];
    const char* tok = m == dv_radio::kMaskDmr ? "dmr" : m == dv_radio::kMaskNxdn48 ? "nxdn48" : m == dv_radio::kMaskNxdn96 ? "nxdn96" : "auto";
    w += static_cast<size_t>(std::snprintf(csv + w, sizeof(csv) - w, "%s%lu.%06lu:%s", i ? "," : "", static_cast<unsigned long>(st.saved_hz[i] / 1000000u),
                                           static_cast<unsigned long>(st.saved_hz[i] % 1000000u), tok));
  }
  g_hooks.save_plan(csv);
}

void load_saved_plan() {
  if (!g_state || !g_hooks.load_plan) return;
  char csv[400];
  if (!g_hooks.load_plan(csv, sizeof(csv))) return;
  uint32_t plan[dv::kMaxScanChannels];
  uint8_t masks[dv::kMaxScanChannels];
  const size_t n = parse_plan(csv, plan, masks, dv::kMaxScanChannels);
  if (!n) return;
  std::memcpy(g_state->saved_hz, plan, sizeof(plan));
  std::memcpy(g_state->saved_mask, masks, sizeof(masks));
  g_state->saved_n = static_cast<uint8_t>(n);
  apply_plan_option(*g_state);
}

void format_utc(uint32_t epoch, char* out, size_t cap) {
  const uint32_t s = epoch % 86400u;
  std::snprintf(out, cap, "%02u:%02u:%02u", static_cast<unsigned>(s / 3600), static_cast<unsigned>((s / 60) % 60),
                static_cast<unsigned>(s % 60));
}

NxdnChannel nxdn_channel(uint8_t rfct) {
  switch (rfct) {
    case 0: return NxdnChannel::rcch;
    case 1: return NxdnChannel::rtch;
    case 2: return NxdnChannel::rdch;
    default: return NxdnChannel::other;
  }
}

bool same_activity(const Heard& h, const dv_radio::Event& e) {
  return h.protocol == e.protocol && h.frequency_hz == e.frequency_hz && h.source_id == e.source_id &&
         h.destination_id == e.destination_id && h.has_source == e.has_source && h.ran == e.ran;
}

uint8_t entry_mask(const State& st) {
  const auto ss = st.scanner.status();
  return ss.kind == dv::PlanKind::list && ss.index < dv::kMaxScanChannels ? st.plan_mask[ss.index] : 0;   // a range always rotates (auto)
}

// Session ledger: the channels that showed activity this visit to the dashboard, with the best evidence each reached.
void ledger_note(State& st, uint32_t hz, Protocol pr, uint8_t level, bool is_new) {
  size_t i = 0;
  while (i < st.led_n && !(st.led_hz[i] == hz && st.led_proto[i] == static_cast<uint8_t>(pr))) ++i;
  if (i == st.led_n) {
    if (st.led_n == 64) return;                     // a very long scan: later channels are still decoded, just not counted here
    st.led_hz[i] = hz; st.led_proto[i] = static_cast<uint8_t>(pr); st.led_level[i] = 0; st.led_new[i] = is_new; ++st.led_n;
  }
  if (level > st.led_level[i]) st.led_level[i] = level;
}

// The discovered-signals list (newest first, kScanHits long): one entry per (protocol, frequency). Evidence only ever rises, and every
// field comes from the event that produced it; a sync-only entry carries no identity at all.
void upsert_hit(State& st, Protocol pr, uint32_t hz, Evidence ev, const dv_radio::Event* e, bool is_new, uint32_t clock_utc) {
  Snapshot& s = st.snapshot;
  size_t i = 0;
  while (i < s.scan_hit_count && !(s.scan_hits[i].protocol == pr && s.scan_hits[i].frequency_hz == hz)) ++i;
  ScanHit h{};
  if (i < s.scan_hit_count) h = s.scan_hits[i];
  else {
    h.protocol = pr; h.frequency_hz = hz; h.has_frequency = true; h.is_new = is_new;
    if (clock_utc) format_utc(clock_utc, h.first_time, sizeof(h.first_time));
  }
  if (ev > h.evidence) h.evidence = ev;
  if (e) {
    if (e->colour_code_valid) { h.colour_code = e->colour_code; h.colour_code_valid = true; }
    if (e->slot_valid) { h.timeslot = e->slot; h.timeslot_valid = true; }
    if (e->ran_valid) { h.ran = e->ran; h.ran_valid = true; }
    if (e->nxdn_rf_valid) { h.nxdn_channel = nxdn_channel(e->nxdn_rf_channel); h.nxdn_channel_valid = true; }
    if (e->evidence == Evidence::identity_valid) {
      h.source_id = e->source_id; h.has_source = e->has_source; h.destination_id = e->destination_id; h.has_destination = e->has_destination;
      h.group_call = e->group_call;
    }
  }
  if (!st.visit_counted) { if (h.seen_count < 65535) ++h.seen_count; st.visit_counted = true; }   // once per visit to the channel
  if (clock_utc) format_utc(clock_utc, h.last_time, sizeof(h.last_time));
  size_t from = i < s.scan_hit_count ? i : (s.scan_hit_count < kScanHits ? s.scan_hit_count++ : kScanHits - 1);
  for (size_t k = from; k > 0; --k) s.scan_hits[k] = s.scan_hits[k - 1];
  s.scan_hits[0] = h;
  ledger_note(st, hz, pr, ev == Evidence::identity_valid ? 3 : ev == Evidence::integrity_valid ? 2 : 1, is_new);
}

// Rebuild the Systems table and totals from the database that matches what is on screen (live, or the replay's own).
void fill_systems(State& st) {
  Snapshot& s = st.snapshot;
  const dv::Discovery& d = st.show_replay_db ? st.replay_db : st.db;
  s.db_available = true;
  s.db_systems = static_cast<uint16_t>(d.system_count());
  s.db_talkgroups = static_cast<uint16_t>(d.talkgroup_count());
  s.db_subscribers = static_cast<uint16_t>(d.subscriber_count());
  s.observed_system_count = 0;
  const size_t first = st.surface.state().system_offset;   // the Systems page lists newest first, a page at a time
  for (size_t n = 0; n < kObservedSystems; ++n) {
    const size_t i = d.nth_newest_system(first + n);
    if (i == static_cast<size_t>(-1)) break;
    const dv::SystemRec& r = d.system(i);
    ObservedSystem& o = s.observed_systems[s.observed_system_count++];
    o = {};
    o.protocol = static_cast<Protocol>(r.protocol);
    o.evidence = static_cast<Evidence>(r.best_evidence);
    o.frequency_hz = r.frequency_hz; o.has_frequency = true;
    if (r.area_kind == dv::AreaKind::colour_code) { o.colour_code = r.area; o.colour_code_valid = true; }
    if (r.area_kind == dv::AreaKind::ran) { o.ran = r.area; o.ran_valid = true; }
    o.slots_seen = r.slots_seen;
    o.heard_count = r.count;
    o.talkgroup_count = static_cast<uint16_t>(d.talkgroups_of(r.uid));
    o.subscriber_count = static_cast<uint16_t>(d.subscribers_of(r.uid));
    for (size_t k = 0; k < kTopIds; ++k) {
      const size_t t = d.nth_newest_talkgroup(r.uid, k);
      if (t == static_cast<size_t>(-1)) break;
      o.top_talkgroups[o.top_talkgroup_n++] = d.talkgroup(t).id;
    }
    for (size_t k = 0; k < kTopIds; ++k) {
      const size_t u = d.nth_newest_subscriber(r.uid, k);
      if (u == static_cast<size_t>(-1)) break;
      o.top_subscribers[o.top_subscriber_n++] = d.subscriber(u).id;
    }
    if (r.last_utc) format_utc(r.last_utc, o.last_time, sizeof(o.last_time));
  }
}

void note_event(State& st, const dv_radio::Event& e) {
  Snapshot& s = st.snapshot;
  const uint32_t clock_utc = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0;
  const bool identity = e.evidence == Evidence::identity_valid;
  // Discovery: validated observations only. Replays go to their own RAM table, never the persistent one.
  const bool to_replay_table = e.replay && !st.learn_replay;
  dv::Discovery& table = to_replay_table ? st.replay_db : st.db;
  const dv::Observation obs = dv::observation_from(e, clock_utc);
  const bool existed = table.find_system(obs.protocol, obs.frequency_hz, obs.area, obs.area_kind) != static_cast<size_t>(-1);
  table.record(obs);
  if (st.disc_running && !e.replay && e.evidence >= Evidence::integrity_valid && e.frequency_hz) {
    uint8_t k = 0;
    while (k < st.disc_n && !(st.disc_hz[k] == e.frequency_hz && st.disc_proto[k] == static_cast<uint8_t>(e.protocol))) ++k;
    if (k == st.disc_n && st.disc_n < State::kDiscMax) {
      st.disc_hz[k] = e.frequency_hz; st.disc_proto[k] = static_cast<uint8_t>(e.protocol); st.disc_seen[k] = 0; ++st.disc_n;
    }
    if (k < st.disc_n && st.disc_seen[k] < 255) ++st.disc_seen[k];
  }
  st.show_replay_db = to_replay_table;
  upsert_hit(st, e.protocol, e.frequency_hz, e.evidence, &e, !existed, clock_utc);
  // A later control burst on the same channel must not wipe a verified call off the Live view for a few seconds.
  const uint32_t t = now();
  if (!identity && st.have_identity && e.frequency_hz == st.identity_freq && t - st.last_identity_ms < kIdentityStickyMs) {
    fill_systems(st);
    ++s.revision;
    return;
  }
  if (identity) { st.have_identity = true; st.last_identity_ms = t; st.identity_freq = e.frequency_hz; }
  // Live call = the newest verified event on the CURRENT channel visit; a retune clears it (no stale metadata).
  s.protocol = e.protocol;
  s.evidence = e.evidence;
  s.origin = e.replay ? Origin::real_iq_replay : Origin::live;
  s.frequency_hz = e.frequency_hz;
  s.has_frequency = true;
  s.source_id = e.source_id; s.has_source = e.has_source;
  s.destination_id = e.destination_id; s.has_destination = e.has_destination;
  s.group_call = e.group_call;
  s.colour_code = e.colour_code; s.colour_code_valid = e.colour_code_valid;
  s.timeslot = e.slot; s.timeslot_valid = e.slot_valid;
  s.ran = e.ran; s.ran_valid = e.ran_valid;
  s.nxdn = {};
  if (e.nxdn_rf_valid) {
    s.nxdn.channel = nxdn_channel(e.nxdn_rf_channel);
    s.nxdn.channel_valid = true;     // topology (Type-C / Type-D) stays unknown: never inferred from a RAN
  }
  st.have_call = true;
  st.call_epoch = e.epoch;
  // Last Heard: bounded, newest first, one row per distinct activity.
  size_t idx = kHistory;
  for (size_t i = 0; i < s.history_count; ++i)
    if (same_activity(s.history[i], e)) { idx = i; break; }
  if (idx == kHistory) {
    idx = s.history_count < kHistory ? s.history_count++ : kHistory - 1;
  }
  for (size_t i = idx; i > 0; --i) s.history[i] = s.history[i - 1];
  Heard& h = s.history[0];
  h = {};
  h.protocol = e.protocol; h.evidence = e.evidence; h.origin = e.replay ? Origin::real_iq_replay : Origin::live; h.frequency_hz = e.frequency_hz;
  h.source_id = e.source_id; h.destination_id = e.destination_id;
  h.has_source = e.has_source; h.has_destination = e.has_destination;
  h.ran = e.ran; h.ran_valid = e.ran_valid;
  h.colour_code = e.colour_code; h.colour_code_valid = e.colour_code_valid;
  h.timeslot = e.slot; h.timeslot_valid = e.slot_valid; h.group_call = e.group_call;
  if (clock_utc) { h.has_time = true; format_utc(clock_utc, h.time_utc, sizeof(h.time_utc)); }
  fill_systems(st);
  ++s.revision;
}

void clear_call(State& st) {
  Snapshot& s = st.snapshot;
  s.protocol = Protocol::unknown; s.evidence = Evidence::none;
  s.source_id = s.destination_id = 0; s.has_source = s.has_destination = s.group_call = false;
  s.colour_code = s.timeslot = s.ran = 0; s.colour_code_valid = s.timeslot_valid = s.ran_valid = false;
  s.nxdn = {};
  st.have_call = false;
  st.have_identity = false;
  st.visit_sync_logged = st.visit_counted = false;
  const auto rs0 = dv_radio::status();
  st.base_dmr = rs0.dmr_acquisitions; st.base_n48 = rs0.nxdn48_frames; st.base_n96 = rs0.nxdn96_frames;   // sync evidence is judged against these
  ++s.revision;
}

ScanPhase map_phase(dv::ScanState state) {
  switch (state) {
    case dv::ScanState::idle: return ScanPhase::idle;
    case dv::ScanState::tuning:
    case dv::ScanState::settling: return ScanPhase::settling;
    case dv::ScanState::sampling: return ScanPhase::scanning;
    case dv::ScanState::holding: return ScanPhase::holding;
    case dv::ScanState::paused: return ScanPhase::paused;
    case dv::ScanState::failed: return ScanPhase::failed;
  }
  return ScanPhase::unavailable;
}

// Rebuild only the facts the firmware can prove. Fields owned by event notes (live call, history, systems) are kept.
bool receiver_ok_for_discover(const State&) { return g_hooks.receiver_ready && g_hooks.receiver_ready(); }

void refresh(State& st) {
  Snapshot& s = st.snapshot;
  const bool ready = g_hooks.receiver_ready && g_hooks.receiver_ready();
  const auto ss = st.scanner.status();
  const auto rs = dv_radio::status();
  Receiver rx = ready ? Receiver::ready : Receiver::disconnected;
  if (ready && rs.running && rs.link == dv_radio::Link::receiving) rx = Receiver::receiving;
  if (st.replaying) rx = Receiver::receiving;
  if (ss.state == dv::ScanState::paused) rx = Receiver::paused;
  if (ss.state == dv::ScanState::failed && ready) rx = Receiver::error;
  const bool can_scan = ready && st.scanner.plan_count() > 0 && !st.replaying;
  const ScanPhase phase = ready || ss.state == dv::ScanState::idle ? map_phase(ss.state) : ScanPhase::unavailable;
  const bool active = st.scanner.active();
  ScanStatus scan;
  scan.phase = can_scan || active ? phase : ScanPhase::unavailable;
  scan.planned = static_cast<uint16_t>(ss.count);
  scan.visited = static_cast<uint16_t>(ss.count ? ss.visited % (ss.count + 1) : 0);
  scan.current_frequency_hz = ss.frequency_hz;
  scan.current_frequency_valid = active && ss.frequency_hz != 0;
  scan.validated_hits = ss.holds;
  scan.progress_valid = active && ss.count > 0;     // the position in a repeating plan; not a completion percentage
  // Plan facts: what the scanner is actually working through.
  scan.mode = ss.count == 0 ? ScanMode::unavailable : ss.kind == dv::PlanKind::range ? ScanMode::frequency_range : ScanMode::channel_list;
  scan.channel_index = active && ss.count ? static_cast<uint16_t>(ss.index + 1) : 0;
  scan.passes = ss.passes;
  scan.plan_option = st.plan_option;
  scan.plan_options = static_cast<uint8_t>(plan_option_count(st));
  if (ss.count == 0) std::snprintf(scan.plan_name, sizeof(scan.plan_name), "%s", st.plan_option == 0 ? "SAVED LIST" : "");
  else if (st.plan_option == 0) std::snprintf(scan.plan_name, sizeof(scan.plan_name), "SAVED LIST");
  else if (st.plan_option <= dv::kBandPresetCount) std::snprintf(scan.plan_name, sizeof(scan.plan_name), "%s", dv::kBandPresets[st.plan_option - 1].name);
  else if (st.plan_option == 1 + dv::kBandPresetCount) std::snprintf(scan.plan_name, sizeof(scan.plan_name), "MY BANDS (%u)", static_cast<unsigned>(my_bands_count(st)));
  else std::snprintf(scan.plan_name, sizeof(scan.plan_name), "CUSTOM RANGE");
  if (ss.kind == dv::PlanKind::range && ss.count) {
    scan.range_start_hz = ss.range_start_hz; scan.range_step_hz = ss.range_step_hz;
    scan.range_end_hz = ss.range_end_hz;
  }
  scan.est_pass_seconds = static_cast<uint32_t>(ss.count * (ss.kind == dv::PlanKind::range ? 700u + 600u : 700u + 1500u) / 1000u);   // settle + quick or full dwell per channel; an estimate (a busy channel takes longer)
  scan.saved_channels = st.saved_n;
  scan.saved_capacity = dv::kMaxScanChannels;
  // Evidence on the current channel: sync-level means a decoder reported synchronisation since this visit began (never an identity).
  bool synced = false;
  Protocol sync_protocol = Protocol::unknown;
  if (active && !st.have_call) {
    if (rs.dmr_acquisitions > st.base_dmr) { synced = true; sync_protocol = Protocol::dmr; }
    else if (rs.nxdn96_frames > st.base_n96) { synced = true; sync_protocol = Protocol::nxdn96; }
    else if (rs.nxdn48_frames > st.base_n48) { synced = true; sync_protocol = Protocol::nxdn48; }
  }
  if (synced && !st.visit_sync_logged && ss.frequency_hz) {
    st.visit_sync_logged = true;
    upsert_hit(st, sync_protocol, ss.frequency_hz, Evidence::sync, nullptr, true, g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0);
  }
  const Evidence cur_evidence = st.have_call ? s.evidence : synced ? Evidence::sync : Evidence::none;
  if (ss.state == dv::ScanState::failed) scan.stage = ScanStage::error;
  else if (ss.state == dv::ScanState::paused) scan.stage = ScanStage::paused;
  else if (ss.state == dv::ScanState::holding) scan.stage = ScanStage::hold;
  else if (active) scan.stage = !st.have_call ? (synced ? ScanStage::sync : ScanStage::scanning)
                                              : (s.evidence == Evidence::identity_valid ? ScanStage::identity : ScanStage::integrity);
  else scan.stage = can_scan ? ScanStage::ready : ScanStage::unavailable;
  for (uint8_t i = 0; i < st.led_n; ++i) {
    if (st.led_level[i] >= 2) { ++scan.validated; if (st.led_new[i]) ++scan.new_this_session; }
    else ++scan.candidates;
  }
  const bool diag = rs.running;
  const int32_t level = rs.level_milli > 0 ? static_cast<int32_t>(std::lround(200.0 * std::log10(rs.level_milli / 1000.0))) : 0;
  const bool saved_flags_changed = [&] {
    bool ch = false;
    for (size_t i = 0; i < s.scan_hit_count && i < kScanHits; ++i) {
      bool sv = false;
      for (uint8_t k = 0; k < st.saved_n; ++k) sv = sv || st.saved_hz[k] == s.scan_hits[i].frequency_hz;
      if (sv != s.scan_hits[i].saved) { s.scan_hits[i].saved = sv; ch = true; }
    }
    return ch;
  }();
  const bool changed = s.receiver != rx || s.scan.phase != scan.phase || s.scan.visited != scan.visited ||
      s.scan.current_frequency_hz != scan.current_frequency_hz || s.can_scan != can_scan || s.can_hold != active ||
      s.scan_on_hold != ss.manual_hold || s.sync_count != rs.dmr_acquisitions + rs.nxdn48_frames + rs.nxdn96_frames ||
      s.integrity_frames != rs.events || s.iq_drops != rs.loss_events || s.diagnostics_valid != diag ||
      s.relative_level_valid != (rs.level_milli > 0) || (rs.level_milli > 0 && std::abs(s.relative_level_tenths_db - level) >= 10) ||
      s.scan.stage != scan.stage || s.scan.mode != scan.mode || s.scan.channel_index != scan.channel_index ||
      s.scan.validated != scan.validated || s.scan.candidates != scan.candidates || s.scan.new_this_session != scan.new_this_session ||
      s.scan.passes != scan.passes || s.scan.plan_option != scan.plan_option || s.scan.plan_options != scan.plan_options ||
      s.scan.saved_channels != scan.saved_channels || std::strcmp(s.scan.plan_name, scan.plan_name) != 0 ||
      s.scan.planned != scan.planned || s.scan.est_pass_seconds != scan.est_pass_seconds || s.evidence != cur_evidence ||
      saved_flags_changed;
  s.receiver = rx;
  s.scan = scan;
  s.can_scan = can_scan;
  s.can_hold = s.can_skip = active;
  s.scan_on_hold = ss.manual_hold;
  s.sync_count = rs.dmr_acquisitions + rs.nxdn48_frames + rs.nxdn96_frames;
  s.integrity_frames = rs.events;
  s.iq_drops = rs.loss_events;
  s.crc_failures = 0;
  s.fec_corrected = 0;
  s.diagnostics_valid = diag;
  s.relative_level_valid = rs.level_milli > 0;
  s.relative_level_tenths_db = level;
  s.last_heard_available = true;
  s.systems_available = true;
  s.can_save_channel = g_hooks.save_plan != nullptr;     // the saved list is the existing `dv_plan` setting
  if (!st.have_call) { s.evidence = cur_evidence; if (synced) s.protocol = sync_protocol; }
  if (!st.have_call && ss.frequency_hz && active) {      // show the channel being examined; identity stays unknown
    s.frequency_hz = ss.frequency_hz; s.has_frequency = true;
  }
  if (st.surface.state().system_offset != st.filled_system_offset) {      // the Systems page was paged: refill its rows
    st.filled_system_offset = st.surface.state().system_offset;
    fill_systems(st);
    ++s.revision;
  }
  {
    uint16_t found = 0, cand = 0;
    for (uint8_t i = 0; i < st.disc_n; ++i) (st.disc_seen[i] >= State::kDiscFound ? found : cand)++;
    const bool run = st.disc_running && (st.disc_phase == 1 || st.scanner.active());
    if (!run) { st.disc_running = false; st.disc_phase = 0; }
    const bool can_disc = st.band_mask != 0 && receiver_ok_for_discover(st);
    const bool sv = st.disc_phase == 1;
    const uint16_t sv_done = sv ? static_cast<uint16_t>(std::min<uint32_t>(st.survey.windows_done(), 65535u)) : 0;
    const uint16_t sv_total = sv ? static_cast<uint16_t>(std::min<uint32_t>(st.survey.windows_total(), 65535u)) : 0;
    const uint16_t sv_hot = static_cast<uint16_t>(st.disc_phase ? (sv ? st.survey.hot_count() : st.hot_n) : 0);
    const uint16_t sv_rate = static_cast<uint16_t>(st.disc_phase ? st.survey.rate() / 1000u : 0);
    const uint32_t sv_center = sv ? st.survey.current_center_hz() : 0;
    const uint16_t sv_pass = static_cast<uint16_t>(std::min<uint32_t>(st.survey_passes, 65535u));
    if (s.discover_running != run || s.discover_found != found || s.discover_candidates != cand || s.discover_saved != st.disc_saved ||
        s.discover_skipped != st.disc_skipped || s.discover_can_start != can_disc || s.discover_phase != st.disc_phase ||
        s.survey_done != sv_done || s.survey_total != sv_total || s.survey_hot != sv_hot || s.survey_rate_khz != sv_rate ||
        s.survey_center_hz != sv_center || s.survey_passes != sv_pass)
      ++s.revision;
    s.discover_running = run; s.discover_found = found; s.discover_candidates = cand;
    s.discover_saved = st.disc_saved; s.discover_skipped = st.disc_skipped; s.discover_can_start = can_disc;
    s.discover_phase = st.disc_phase; s.survey_done = sv_done; s.survey_total = sv_total; s.survey_hot = sv_hot;
    s.survey_rate_khz = sv_rate; s.survey_center_hz = sv_center; s.survey_passes = sv_pass;
  }
  if (changed) ++s.revision;
  refresh_voice(s);
  {
    const bool can = g_hooks.file_save && g_hooks.file_save_result && g_hooks.flag_store;
    const bool on = dv_voice_service::save_speech();
    if (s.can_save_speech != can || s.save_speech != on || s.speech_files != st.speech_files) ++s.revision;
    if (s.save_iq != dv_radio::iq_enabled() || s.iq_files != st.iq_files) ++s.revision;
    s.save_iq = dv_radio::iq_enabled(); s.iq_files = st.iq_files;
    s.can_save_speech = can; s.save_speech = on; s.speech_files = st.speech_files;
    const uint16_t ch = static_cast<uint16_t>(std::min<size_t>(my_bands_channels(st), 65535));
    const uint16_t mins = static_cast<uint16_t>((my_bands_channels(st) * 1300u + 59999u) / 60000u);
    if (s.band_mask != st.band_mask || s.band_channels != ch || s.band_minutes != mins) ++s.revision;
    s.band_mask = st.band_mask; s.band_channels = ch; s.band_minutes = mins;
  }
}

void stop_scan(State& st) {
  st.scanner.stop();
  dv_radio::stop();                 // gives the sample rate back to the receiver owner
  clear_call(st);
}

void stop_replay(State& st);
bool start_scan(State& st) {
  stop_replay(st);
  if (!g_hooks.receiver_ready || !g_hooks.receiver_ready() || st.scanner.plan_count() == 0) return false;
  install_radio_hooks();
  if (!dv_radio::start()) return false;
  if (!st.scanner.start(now())) { dv_radio::stop(); return false; }
  clear_call(st);
  return true;
}

void stop_replay(State& st) {
  if (!st.replaying && !st.replay_done) return;
  if (g_hooks.replay_close) g_hooks.replay_close();
  st.replaying = false;
  st.replay_done = false;
  dv_radio::stop();
}

// Feed the recorded file at real-time pace (240 kS/s CU8 = 480 bytes/ms) from the UI task. Never touches the receiver.
void pump_replay(State& st) {
  if (!st.replaying) return;
  // Real-time pace, bounded work per tick. If the UI task ever falls behind, the clock is re-anchored instead of bursting
  // through the backlog: a replay must never starve the main loop.
  const uint32_t t = now();
  uint64_t due = static_cast<uint64_t>(t - st.replay_start_ms) * 480u;
  if (due > st.replay_fed + 65536u) {
    st.replay_start_ms = t - static_cast<uint32_t>(st.replay_fed / 480u);
    due = st.replay_fed;
  }
  for (int i = 0; i < 4 && st.replay_fed < due; ++i) {
    const size_t n = g_hooks.replay_read ? g_hooks.replay_read(st.replay_buf, sizeof(st.replay_buf)) : 0;
    if (n == 0) {                                   // end of file (or a read error: treated the same, visibly stopped)
      if (g_hooks.replay_close) g_hooks.replay_close();
      st.replaying = false;
      st.replay_done = true;
      return;
    }
    dv_radio::feed_replay(st.replay_buf, n);
    st.replay_fed += n;
  }
}

bool start_replay(State& st, const char* path, uint32_t channel_hz) {
  if (!g_hooks.replay_open || !g_hooks.replay_read || channel_hz == 0) return false;
  stop_replay(st);
  if (st.scanner.active()) { st.scanner.stop(); }
  dv_radio::stop();
  install_radio_hooks();
  if (!dv_radio::start()) return false;
  if (!g_hooks.replay_open(path)) { dv_radio::stop(); return false; }
  if (!dv_radio::begin_replay(channel_hz)) { g_hooks.replay_close(); dv_radio::stop(); return false; }
  clear_call(st);
  st.replay_db.clear();             // a replay starts from an empty RAM table; the persistent database is untouched
  st.show_replay_db = true;
  fill_systems(st);
  st.replay_fed = 0;
  st.replay_start_ms = now();
  st.replaying = true;
  st.replay_done = false;
  return true;
}

// ---- DISCOVER with the wideband pre-screen -------------------------------------------------------------------------------------
// Survey the picked bands at the highest loss-free sample rate (many channels per tune), keep the channels clearly above the noise
// floor, decode those in batches of the saved-list size, then survey again. The decoders only ever see occupied channels.
size_t my_bands_segments(const State& st, dv::Segment* seg) {
  size_t n = 0;
  for (size_t i = 0; i < dv::kBandPresetCount && n < dv::kMaxSegments; ++i)
    if ((st.band_mask >> i) & 1U) seg[n++] = {dv::kBandPresets[i].start_hz, dv::kBandPresets[i].step_hz, static_cast<uint32_t>(dv::channel_count(dv::kBandPresets[i]))};
  return n;
}

bool survey_pass(State& st) {
  dv::Segment seg[dv::kMaxSegments];
  const size_t n = my_bands_segments(st, seg);
  return n && st.sv_work && st.survey.start(seg, n, now(), st.sv_rate_idx);
}

bool begin_survey(State& st) {
  if (!g_hooks.tune || !g_hooks.lost_samples) return false;                 // no way to know a window was clean: use the plain sweep
  if (!st.sv_work) {
    void* mem = heap_caps_calloc(1, sizeof(dv_survey::Work), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) return false;
    st.sv_work = static_cast<dv_survey::Work*>(mem);
    dv_survey::init(*st.sv_work);
  }
  st.survey.attach(st.sv_work);
  dv_survey::install(&st.survey);
  st.sv_rate_idx = 0; st.survey_passes = 0; st.hot_n = 0; st.hot_at = 0; st.sv_failed = false;
  stop_replay(st);
  install_radio_hooks();
  if (!dv_radio::start()) return false;                                     // the decoders are ready when the first hot channel is found
  clear_call(st);
  return survey_pass(st);
}

bool start_batch(State& st) {
  uint32_t plan[dv::kMaxScanChannels];
  size_t n = 0;
  while (n < dv::kMaxScanChannels && st.hot_at + n < st.hot_n) { plan[n] = st.hot_hz[st.hot_at + n]; ++n; }
  st.hot_at = static_cast<uint8_t>(st.hot_at + n);
  std::memset(st.plan_mask, 0, sizeof(st.plan_mask));                       // every protocol decoder rotates through each dwell
  st.scanner.stop();
  if (!n || !st.scanner.set_plan(plan, n)) return false;
  install_radio_hooks();
  if (!dv_radio::start()) return false;                                     // a failed scan may have stopped it
  clear_call(st);
  return st.scanner.start(now());
}

void survey_release(State& st) {
  if (st.sv_rate_held && g_hooks.release) g_hooks.release();                // the survey's rate goes back to the normal one
  st.sv_rate_held = false;
}

void stop_discover(State& st) {
  st.survey.stop();
  st.disc_phase = 0;
  st.disc_running = false;
  st.scanner.stop();
  dv_radio::stop();
  survey_release(st);
  apply_plan_option(st);                                                    // the scan plan goes back to MY BANDS
}

// The survey could not run cleanly (no rate stayed loss-free): the plain sweep of the picked bands takes over, and the page says so.
void survey_fallback(State& st) {
  st.sv_failed = true;
  st.survey.stop();
  st.disc_phase = 0;
  st.scanner.stop();
  dv_radio::stop();
  survey_release(st);
  apply_plan_option(st);
  if (!start_scan(st)) st.disc_running = false;
}

void run_discover(State& st) {
  if (st.disc_phase == 1) {
    uint32_t hz = 0, rate = 0;
    if (st.survey.service(now(), g_hooks.lost_samples ? g_hooks.lost_samples() : 0, &hz, &rate)) {
      const bool ok = g_hooks.tune && g_hooks.tune(hz, rate);
      if (ok) st.sv_rate_held = true;
      st.survey.tune_result(ok, now());
    }
    if (st.survey.failed()) { survey_fallback(st); return; }
    if (st.survey.finished()) {
      st.sv_rate_idx = st.survey.rate_index();                              // later passes start at the rate that worked
      ++st.survey_passes;
      st.hot_n = static_cast<uint8_t>(std::min<size_t>(st.survey.hot_count(), dv_survey::kMaxHot));
      for (uint8_t i = 0; i < st.hot_n; ++i) st.hot_hz[i] = st.survey.hot(i).hz;
      st.hot_at = 0;
      if (st.hot_n == 0) { if (!survey_pass(st)) survey_fallback(st); return; }   // a quiet pass: look again
      st.disc_phase = 2;
      if (!start_batch(st)) { st.disc_phase = 1; if (!survey_pass(st)) survey_fallback(st); }
    }
  } else if (st.disc_phase == 2) {
    const bool batch_done = !st.scanner.active() || st.scanner.status().passes >= 1;     // every channel of this batch was examined once
    if (batch_done) {
      if (st.hot_at < st.hot_n && start_batch(st)) return;                  // the next batch of occupied channels
      st.scanner.stop();
      st.disc_phase = 1;                                                    // all of them checked: survey again
      if (!survey_pass(st)) survey_fallback(st);
    }
  }
}

// ---- DEVELOPMENT capture: tune, record the raw stream into 4 MB segments, write each through the shared SD writer, then the sidecar ----
// Returns nullptr when started, otherwise the reason it was refused.
const char* capture_begin(State& st, uint32_t center_hz, uint32_t rate, uint32_t seconds) {
  if (st.cap_phase || st.scanner.active() || st.disc_phase || st.replaying) return "busy: a scan, discovery, replay or capture is running";
  if (!g_hooks.tune || !g_hooks.file_save || !g_hooks.file_save_result) return "no tune or card hook";
  if (!g_hooks.receiver_ready || !g_hooks.receiver_ready()) return "no receiver";
  if (!st.cap_mem) st.cap_mem = static_cast<uint8_t*>(heap_caps_malloc(dv_capture::kMemBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!st.cap_mem) return "no memory for the 10 MB capture buffer";
  dv_radio::stop();                                                         // the decoders are not part of a raw capture
  if (!g_hooks.tune(center_hz, rate)) return "the receiver owner refused the tune or rate";
  st.sv_rate_held = true;                                                   // the rate is released at the end
  st.cap.attach(st.cap_mem);
  st.cap_center = center_hz; st.cap_rate = rate; st.cap_seconds = seconds;
  st.cap_saving = false;
  st.cap_settle_until = now() + 500;
  st.cap_phase = 1;
  dv_capture::install(&st.cap);
  return nullptr;
}

void capture_end(State& st) {
  st.cap.abort();
  dv_capture::install(nullptr);
  survey_release(st);
  st.cap_phase = 0;
  st.cap_saving = false;
}

void run_capture(State& st) {
  if (!st.cap_phase) return;
  if (st.cap_phase == 1 && static_cast<int32_t>(now() - st.cap_settle_until) >= 0) {
    dv_capture::Config c;
    c.center_hz = st.cap_center; c.rate = st.cap_rate; c.seconds = st.cap_seconds; c.utc = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0;
    if (!st.cap.start(c, now())) { capture_end(st); return; }
    st.cap_phase = 2;
  }
  if (st.cap_phase == 2) st.cap.service(now());
  if (st.cap_saving) {
    const int r = g_hooks.file_save_result();
    if (r == 0) return;
    if (st.cap_phase == 3) { capture_end(st); return; }                    // the sidecar is on the card: finished
    st.cap.write_done(r == 1);
    st.cap_saving = false;
  }
  if (st.db_saving || st.rec_saving) return;                                // the writer is shared: never two jobs at once
  if (st.cap_phase == 2 && st.cap.state() == dv_capture::State::done) {
    char path[96];
    const size_t n = st.cap.sidecar(st.cap_json, sizeof(st.cap_json));
    if (n && st.cap.sidecar_path(path, sizeof(path)) && g_hooks.file_save(path, nullptr, 0, st.cap_json, n)) { st.cap_phase = 3; st.cap_saving = true; }
    else if (!n) capture_end(st);
    return;
  }
  if (st.cap_phase == 2) {
    const uint8_t* d = nullptr; size_t n = 0; char path[96];
    if (st.cap.next(&d, &n, path, sizeof(path))) {
      if (g_hooks.file_save(path, nullptr, 0, d, n)) { st.cap.write_started(); st.cap_saving = true; }     // writer busy: asked again next tick
    }
  }
}

void run_scanner(State& st) {
  if (st.disc_phase) run_discover(st);
  if (st.cap_phase) run_capture(st);
  pump_replay(st);
  const auto rs = dv_radio::status();
  if (rs.loss_events != st.last_loss_events) {
    st.last_loss_events = rs.loss_events;
    st.scanner.stream_discontinuity(now());
  }
  dv::ScanInputs in;
  in.receiver_ready = g_hooks.receiver_ready && g_hooks.receiver_ready();
  in.verified_ms = rs.verified_ms;
  in.candidate = rs.candidate;
  in.identity_ms = rs.identity_ms;
  const dv::ScanStep step = st.scanner.service(now(), in);
  if (step.tune) {
    const bool ok = dv_radio::tune_channel(step.tune_hz);
    st.scanner.retune_result(ok, now());
    if (ok) {
      clear_call(st);                // new channel visit: nothing from the previous channel carries over
      const uint8_t entry = entry_mask(st);
      st.auto_phase = 0;
      st.auto_switch_ms = now();
      dv_radio::set_protocol_mask(st.mask_override ? st.mask_override : entry ? entry : dv_radio::kMaskDmr);
    }
  }
  // An "auto" entry cannot run every decoder at once (NXDN48 alone needs more than one core on the Tab5): it rotates them
  // through the dwell, restarting the decoder set at each switch so no decoder bridges a window it did not see.
  if (st.scanner.active() && !st.mask_override && entry_mask(st) == 0 &&
      st.scanner.status().state == dv::ScanState::sampling && now() - st.auto_switch_ms >= 400) {
    static constexpr uint8_t kRotation[3] = {dv_radio::kMaskDmr, dv_radio::kMaskNxdn96, dv_radio::kMaskNxdn48};
    st.auto_phase = static_cast<uint8_t>((st.auto_phase + 1) % 3);
    st.auto_switch_ms = now();
    dv_radio::set_protocol_mask(kRotation[st.auto_phase]);
    dv_radio::request_decoder_reset();
  }
  if (!st.scanner.active() && dv_radio::running() && st.scanner.status().state != dv::ScanState::idle) {
    dv_radio::stop();               // failed (receiver removed / retune refused): release the rate, keep the reason visible
  }
  dv_radio::Event e;
  const uint32_t channel_hz = st.replaying || st.replay_done ? rs.channel_hz : st.scanner.status().frequency_hz;
  while (dv_radio::pop_event(&e)) {
    if (e.frequency_hz == channel_hz) note_event(st, e);   // an event from a previous channel visit is stale: dropped
  }
}

void leave_to_home() {
  close();
  if (g_hooks.go_home) g_hooks.go_home();
}

bool full_draw() {
  g_state->surface.enter();
  M5.Display.fillScreen(TFT_BLACK);
  return g_state->surface.draw(M5.Display, g_state->snapshot, overlay(), g_hooks.header_controls);
}

// Adds a verified signal's frequency (with its protocol's decoder) to the saved channel list. The renderer only offers this for a
// selected signal with integrity-validated evidence and a frequency; it is checked again here because serial calls it too.
bool save_hit(State& st, uint8_t index) {
  Snapshot& s = st.snapshot;
  if (!g_hooks.save_plan || index >= s.scan_hit_count || index >= kScanHits) return false;
  const ScanHit& h = s.scan_hits[index];
  if (h.evidence < Evidence::integrity_valid || !h.has_frequency) return false;
  for (uint8_t i = 0; i < st.saved_n; ++i) if (st.saved_hz[i] == h.frequency_hz) return false;
  if (st.saved_n >= dv::kMaxScanChannels) return false;
  st.saved_hz[st.saved_n] = h.frequency_hz;
  st.saved_mask[st.saved_n] = mask_for_protocol(h.protocol);
  ++st.saved_n;
  save_list(st);
  if (st.plan_option == 0) apply_plan_option(st);        // a no-op while scanning: the new channel joins the next time the list is loaded
  ++s.revision;
  return true;
}

void step_plan(State& st, int dir) {
  if (st.scanner.active()) return;
  const size_t n = plan_option_count(st);
  st.plan_option = static_cast<uint8_t>((st.plan_option + n + (dir > 0 ? 1 : n - 1)) % n);
  apply_plan_option(st);
  ++st.snapshot.revision;
}

// SAVE TO LIST: channels found in the last discovery run (at least State::kDiscFound sightings) join the saved scan list, each with
// its protocol's decoder. Nothing is written unless the user taps SAVE TO LIST; a full list skips the rest and says so.
void save_found(State& st) {
  if (!g_hooks.save_plan) return;
  uint16_t added = 0, skipped = 0;
  for (uint8_t i = 0; i < st.disc_n; ++i) {
    if (st.disc_seen[i] < State::kDiscFound) continue;
    bool have = false;
    for (uint8_t k = 0; k < st.saved_n; ++k) have = have || st.saved_hz[k] == st.disc_hz[i];
    if (have) continue;
    const uint8_t mask = mask_for_protocol(static_cast<Protocol>(st.disc_proto[i]));
    if (st.saved_n >= dv::kMaxScanChannels || mask == 0) { ++skipped; continue; }
    st.saved_hz[st.saved_n] = st.disc_hz[i];
    st.saved_mask[st.saved_n] = mask;
    ++st.saved_n;
    ++added;
  }
  if (added) save_list(st);
  st.disc_saved = added;
  st.disc_skipped = skipped;
  if (added && st.plan_option == 0) apply_plan_option(st);
}

void apply_radio_action(State& st, const Action& a) {
  switch (a.kind) {
    case ActionKind::scan:
      if (st.disc_phase) stop_discover(st);
      else if (st.scanner.active()) stop_scan(st);
      else (void)start_scan(st);
      break;
    case ActionKind::toggle_hold: st.scanner.toggle_manual_hold(); break;
    case ActionKind::skip_frequency: st.scanner.skip(now()); break;
    case ActionKind::plan_prev: step_plan(st, -1); break;
    case ActionKind::plan_next: step_plan(st, +1); break;
    case ActionKind::discover_start: {
      if (st.band_mask == 0 || !receiver_ok_for_discover(st)) { ++g_unapplied_actions; return; }
      if (st.disc_phase) stop_discover(st);
      if (st.scanner.active()) stop_scan(st);
      st.plan_option = static_cast<uint8_t>(1 + dv::kBandPresetCount);      // MY BANDS: the bands picked in SETUP
      apply_plan_option(st);
      st.disc_n = 0; st.disc_saved = 0; st.disc_skipped = 0;
      if (begin_survey(st)) { st.disc_phase = 1; st.disc_running = true; break; }
      stop_scan(st);                                                         // the pre-screen could not start: plain sweep of the bands
      st.sv_failed = true;
      if (!start_scan(st)) { ++g_unapplied_actions; return; }
      st.disc_running = true;
      break;
    }
    case ActionKind::discover_stop: if (st.disc_phase) stop_discover(st); else { stop_scan(st); st.disc_running = false; } break;
    case ActionKind::discover_save: save_found(st); break;
    case ActionKind::toggle_band: {
      if (a.value >= dv::kBandPresetCount) { ++g_unapplied_actions; return; }
      st.band_mask = static_cast<uint16_t>(st.band_mask ^ (1u << a.value));
      if (g_hooks.u32_store) g_hooks.u32_store("dv_bands", st.band_mask);
      if (st.plan_option == 1 + dv::kBandPresetCount) apply_plan_option(st);      // the plan follows the picker (a no-op while scanning)
      ++st.snapshot.revision;
      break;
    }
    case ActionKind::toggle_save_iq: {
      if (!g_hooks.flag_store) { ++g_unapplied_actions; return; }
      const bool on = !dv_radio::iq_enabled();
      dv_radio::iq_set_enabled(on);
      g_hooks.flag_store("dv_iq", dv_radio::iq_enabled());
      ++st.snapshot.revision;
      break;
    }
    case ActionKind::toggle_save_speech: {
      if (!g_hooks.flag_store) { ++g_unapplied_actions; return; }
      const bool on = !dv_voice_service::save_speech();
      dv_voice_service::set_save_speech(on);
      g_hooks.flag_store("dv_rec", on);
      ++st.snapshot.revision;
      break;
    }
    case ActionKind::set_listen:
      if (a.value > 3) { ++g_unapplied_actions; return; }
      dv_voice_service::set_listen(static_cast<dv_voice::Listen>(a.value));
      st.snapshot.voice.listen = static_cast<ListenMode>(a.value);
      ++st.snapshot.audio_revision;
      return;
    case ActionKind::save_channel: if (!save_hit(st, a.value)) { ++g_unapplied_actions; return; } break;
    default: ++g_unapplied_actions; return;     // save / setup: no owner yet, never faked
  }
  ++st.actions_applied;
  ++st.snapshot.revision;
}
}  // namespace

void install(const Hooks& hooks) { g_hooks = hooks; }

void open() {
  if (g_state && g_state->background) {        // back from Home with the receiver still running: just show the screen again
    g_state->background = false;
    screens::begin_transition(screens::Id::digital_voice, now());
    screens::finish_transition();
    if (!full_draw()) {
      ++g_open_failed;
      leave_to_home();
    }
    return;
  }
  if (!ensure_state()) {
    ++g_open_failed;
    if (g_hooks.go_home) g_hooks.go_home();
    return;
  }
  new (&g_state->snapshot) Snapshot();
  g_state->scanner = dv::Scanner();
  if (g_hooks.u32_load) g_state->band_mask = static_cast<uint16_t>(g_hooks.u32_load("dv_bands", g_state->band_mask) & ((1u << dv::kBandPresetCount) - 1u));
  start_voice_service();
  load_saved_plan();
  g_state->db.clear();
  if (g_hooks.db_load) {
    if (!g_persist) g_persist = static_cast<char*>(heap_caps_malloc(dv::kMaxSerializedBytes + 64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_persist) {
      const size_t n = g_hooks.db_load(g_persist, dv::kMaxSerializedBytes);
      if (n && n <= dv::kMaxSerializedBytes) (void)g_state->db.parse(g_persist, n);     // a corrupt file leaves the table empty
    }
  }
  g_state->db_saved_revision = g_state->db.revision();
  g_state->db_saved_structure = g_state->db.structure_revision();
  fill_systems(*g_state);
  refresh(*g_state);
  g_state->snapshot.revision = 1;
  screens::begin_transition(screens::Id::digital_voice, now());
  screens::finish_transition();
  if (!full_draw()) {
    ++g_open_failed;
    leave_to_home();
  }
}

void hide() {
  if (!g_state) return;
  // Nothing to keep when the receiver is not running: leave exactly as before.
  if (!dv_radio::running()) { close(); return; }
  g_state->background = true;                  // tick() keeps scanning and decoding; drawing is skipped (the screen is not ours)
  g_state->surface.leave();                    // not an active screen any more (other screens ask active())
}

bool in_background() { return g_state != nullptr && g_state->background; }

void close() {
  if (!g_state) return;
  stop_replay(*g_state);
  service_persistence(*g_state, true);        // flush what was learned; the buffer outlives this call
  g_state->scanner.stop();
  g_state->survey.stop();
  dv_survey::install(nullptr);
  if (g_state->cap_phase) capture_end(*g_state);
  if (g_state->cap_mem) { heap_caps_free(g_state->cap_mem); g_state->cap_mem = nullptr; }
  for (uint32_t t0 = now(); now() - t0 < 20u;) {}                            // a block mid-copy in the DSP task finishes before the work memory goes
  if (g_state->sv_work) { heap_caps_free(g_state->sv_work); g_state->sv_work = nullptr; }
  for (uint32_t t0 = now(); g_state->rec_saving && now() - t0 < (g_state->rec_is_iq ? 20000u : 2500u);) {   // let a recording being written finish: the writer reads the buffer
    const int r = g_hooks.file_save_result ? g_hooks.file_save_result() : 2;
    if (r != 0) {
      if (g_state->rec_is_iq) dv_radio::iq_release(r == 1); else dv_voice_service::speech_release(r == 1);
      g_state->rec_saving = false;
    }
  }
  dv_voice_service::stop();                   // releases the analogue audio path before the receiver is given back
  dv_radio::stop();
  survey_release(*g_state);                   // the survey's wide rate, if it still holds one
  g_state->surface.leave();
  free_state();
}

namespace {
void service_persistence(State& st, bool final_flush) {
  if (!g_hooks.db_save || !g_hooks.db_save_result) return;
  if (st.db_saving) {
    const int r = g_hooks.db_save_result();
    if (r == 0) return;
    st.db_saving = false;
    if (r == 1) ++st.db_saves; else ++st.db_save_failures;
  }
  if (st.db.revision() == st.db_saved_revision) return;
  // A new system, talkgroup or unit is written within 15 s; plain re-hearing (counts, last-seen) at most every 10 minutes.
  const uint32_t since = now() - st.db_last_save_ms;
  const bool structural = st.db.structure_revision() != st.db_saved_structure;
  if (!final_flush && (structural ? since < 15000u : since < 600000u)) return;
  if (!g_persist) g_persist = static_cast<char*>(heap_caps_malloc(dv::kMaxSerializedBytes + 64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_persist) return;
  const size_t n = st.db.serialize(g_persist, dv::kMaxSerializedBytes + 64);
  if (n && g_hooks.db_save(g_persist, n)) {
    st.db_saving = true;
    st.db_saved_revision = st.db.revision();
    st.db_saved_structure = st.db.structure_revision();
    st.db_last_save_ms = now();
  }
}
}  // namespace

// Finished speech recordings go to the card through the same background writer as the discovery database (never both at once).
void service_speech_save(State& st) {
  if (!g_hooks.file_save || !g_hooks.file_save_result) return;
  if (st.rec_saving) {
    const int r = g_hooks.file_save_result();
    if (r == 0) return;
    if (st.rec_is_iq) { dv_radio::iq_release(r == 1); if (r == 1) ++st.iq_files; }
    else { dv_voice_service::speech_release(r == 1); if (r == 1) ++st.speech_files; }
    st.rec_saving = false;
    return;
  }
  if (st.db_saving) return;
  const int16_t* pcm = nullptr;
  size_t n = 0;
  char name[72];
  uint8_t header[dv_speech_rec::kHeaderBytes];
  if (dv_voice_service::speech_ready(&pcm, &n, name, sizeof(name), header)) {
    if (g_hooks.file_save(name, header, sizeof(header), pcm, n * sizeof(int16_t))) { st.rec_saving = true; st.rec_is_iq = false; }   // busy: retried on the next tick
    return;
  }
  const uint8_t* iq = nullptr;
  size_t iq_bytes = 0;
  if (dv_radio::iq_ready(&iq, &iq_bytes, name, sizeof(name)) && g_hooks.file_save(name, nullptr, 0, iq, iq_bytes)) {
    dv_radio::iq_saving();
    st.rec_saving = true; st.rec_is_iq = true;
  }
}

void tick() {
  if (!g_state) return;
  service_speech_save(*g_state);
  service_persistence(*g_state, false);
  run_scanner(*g_state);
  refresh(*g_state);
  // update() also returns true when it correctly drew nothing; count only real model changes as repaints.
  const uint32_t t_render = g_hooks.micros ? g_hooks.micros() : 0;
  const bool drew = g_state->snapshot.revision != g_painted_revision;
  if (g_state->surface.update(M5.Display, g_state->snapshot, overlay()) && drew) {
    if (g_hooks.micros) {
      const uint32_t dt = g_hooks.micros() - t_render;
      g_render_us_total += dt; ++g_render_n; if (dt > g_render_us_max) g_render_us_max = dt;
    }
    g_painted_revision = g_state->snapshot.revision;
    ++g_repaints;
    screens::note_visible_update(screens::Id::digital_voice);
  }
}

void draw(bool static_panel) {
  if (!g_state) return;
  if (static_panel) {
    screens::begin_transition(screens::Id::digital_voice, now());
    screens::finish_transition();
    if (!full_draw()) leave_to_home();
    return;
  }
  tick();
}

void touch(int32_t x, int32_t y) {
  if (!g_state) return;
  const Action action = g_state->surface.touch(x, y, g_state->snapshot, overlay());
  if (action.kind != ActionKind::none) apply_radio_action(*g_state, action);
}

bool active() { return g_state != nullptr && g_state->surface.active(); }

void describe(char* out, size_t size) {
  if (!out || size == 0) return;
  if (!g_state) {
    std::snprintf(out, size, "RTL_DV_UI open=0 open_failed=%lu", static_cast<unsigned long>(g_open_failed));
    return;
  }
  const UiState& ui = g_state->surface.state();
  const auto ss = g_state->scanner.status();
  std::snprintf(out, size,
                "RTL_DV_UI open=1 view=%u density=%u receiver=%s scan=%s plan=%u idx=%u visited=%lu holds=%lu passes=%lu "
                "manual_hold=%d can_scan=%u hist=%u systems=%u rev=%lu repaints=%lu applied=%lu unapplied=%lu "
                "db_sys=%u db_tg=%u db_units=%u db_saves=%lu db_save_fail=%lu db_saving=%d stage=%u mode=%u opt=%u/%u render_n=%lu render_avg_us=%lu render_max_us=%lu quick_skips=%lu control_releases=%lu segments=%u bands=0x%04x",
                static_cast<unsigned>(ui.view), static_cast<unsigned>(ui.density),
                receiver_name(g_state->snapshot.receiver), scan_phase_name(g_state->snapshot.scan.phase),
                static_cast<unsigned>(ss.count), static_cast<unsigned>(ss.index), static_cast<unsigned long>(ss.visited),
                static_cast<unsigned long>(ss.holds), static_cast<unsigned long>(ss.passes), ss.manual_hold ? 1 : 0,
                g_state->snapshot.can_scan ? 1u : 0u, static_cast<unsigned>(g_state->snapshot.history_count),
                static_cast<unsigned>(g_state->snapshot.observed_system_count),
                static_cast<unsigned long>(g_state->snapshot.revision), static_cast<unsigned long>(g_repaints),
                static_cast<unsigned long>(g_state->actions_applied), static_cast<unsigned long>(g_unapplied_actions),
                static_cast<unsigned>(g_state->db.system_count()), static_cast<unsigned>(g_state->db.talkgroup_count()),
                static_cast<unsigned>(g_state->db.subscriber_count()), static_cast<unsigned long>(g_state->db_saves),
                static_cast<unsigned long>(g_state->db_save_failures), g_state->db_saving ? 1 : 0,
                static_cast<unsigned>(g_state->snapshot.scan.stage), static_cast<unsigned>(g_state->snapshot.scan.mode),
                static_cast<unsigned>(g_state->plan_option), static_cast<unsigned>(plan_option_count(*g_state)),
                static_cast<unsigned long>(g_render_n), static_cast<unsigned long>(g_render_n ? g_render_us_total / g_render_n : 0),
                static_cast<unsigned long>(g_render_us_max), static_cast<unsigned long>(ss.quick_skips), static_cast<unsigned long>(ss.control_releases),
                static_cast<unsigned>(ss.segments), static_cast<unsigned>(g_state->band_mask));
}

bool handle_command(const char* command, bool authenticated, void (*write_line)(const char*)) {
  if (!command || !write_line || std::strncmp(command, "RTL_DV ", 7) != 0) return false;
  const char* arg = command + 7;
  char line[640];
  if (std::strncmp(arg, "AUDIO", 5) == 0) {
    return dv_voice_service::handle_command(arg, authenticated, write_line);
  }
  if (std::strcmp(arg, "STATUS") == 0) {
    describe(line, sizeof(line));
    write_line(line);
    dv_radio::format_status(line, sizeof(line));
    write_line(line);
    return true;
  }
  if (!authenticated) {
    write_line("RTL_DV_ERROR auth_required");
    return true;
  }
  if (!g_state) {
    write_line("RTL_DV_ERROR dashboard_not_open (RTL_UI OPEN DIGITAL_VOICE first)");
    return true;
  }
  State& st = *g_state;
  if (std::strncmp(arg, "CAPTURE", 7) == 0) {            // development: record the raw live stream to the card
    const char* a = arg + 7;
    while (*a == ' ') ++a;
    char line[300];
    if (std::strcmp(a, "STOP") == 0) { st.cap.stop(); write_line("RTL_DV_CAPTURE_OK stopping (remaining segments are written)"); return true; }
    if (*a == 0 || std::strcmp(a, "STATUS") == 0) {
      std::snprintf(line, sizeof(line), "RTL_DV_CAPTURE phase=%u center_hz=%lu rate=%lu captured=%llu dropped=%llu drop_events=%lu wrong_rate=%lu written=%lu failed=%lu writing=%d",
                    static_cast<unsigned>(st.cap_phase), static_cast<unsigned long>(st.cap.center_hz()), static_cast<unsigned long>(st.cap.rate()),
                    static_cast<unsigned long long>(st.cap.bytes_captured()), static_cast<unsigned long long>(st.cap.bytes_dropped()),
                    static_cast<unsigned long>(st.cap.drop_events()), static_cast<unsigned long>(st.cap.wrong_rate_blocks()),
                    static_cast<unsigned long>(st.cap.segments_written()), static_cast<unsigned long>(st.cap.segments_failed()), st.cap_saving ? 1 : 0);
      write_line(line);
      return true;
    }
    char* end = nullptr;
    const double mhz = std::strtod(a, &end);
    const long ksps = std::strtol(end, &end, 10);
    const long secs = std::strtol(end, &end, 10);
    const uint32_t rate = static_cast<uint32_t>(ksps * 1000);
    const bool rate_ok = rate == 240000 || rate == 1024000 || rate == 1536000 || rate == 2048000 || rate == 2400000 || rate == 2560000;
    if (!rate_ok || mhz < 24.0 || mhz > 1766.0 || secs < 1 || secs > static_cast<long>(dv_capture::kMaxSeconds)) {
      write_line("RTL_DV_CAPTURE_INVALID use RTL_DV CAPTURE <MHz> <ksps: 240|1024|1536|2048|2400|2560> <seconds 1-120> | STOP | STATUS  (scan and discovery stopped)");
      return true;
    }
    const char* why = capture_begin(st, static_cast<uint32_t>(std::llround(mhz * 1e6)), rate, static_cast<uint32_t>(secs));
    if (why) std::snprintf(line, sizeof(line), "RTL_DV_CAPTURE_REFUSED %s", why);
    else std::snprintf(line, sizeof(line), "RTL_DV_CAPTURE_OK started (files /orcsdr/cap_*.cu8 + .json; status: RTL_DV CAPTURE STATUS)");
    write_line(line);
    ++st.snapshot.revision;
    return true;
  }
  if (std::strcmp(arg, "DB CLEAR") == 0) {      // owner action: forget everything learned (and the file at the next save)
    st.db.clear(); st.replay_db.clear(); fill_systems(st); ++st.snapshot.revision;
    write_line("RTL_DV_DB_OK cleared");
    return true;
  }
  if (std::strcmp(arg, "DB LEARN_REPLAY ON") == 0 || std::strcmp(arg, "DB LEARN_REPLAY OFF") == 0) {
    st.learn_replay = std::strcmp(arg, "DB LEARN_REPLAY ON") == 0;
    write_line(st.learn_replay ? "RTL_DV_DB_OK replay_learning_on (recorded captures now feed the persistent database)"
                               : "RTL_DV_DB_OK replay_learning_off");
    return true;
  }
  if (std::strcmp(arg, "DB SAVE") == 0) {
    service_persistence(st, true);
    write_line(st.db_saving ? "RTL_DV_DB_OK save_started" : "RTL_DV_DB_OK nothing_to_save_or_busy");
    return true;
  }
  if (std::strncmp(arg, "PLAN ", 5) == 0) {
    uint32_t plan[dv::kMaxScanChannels];
    uint8_t masks[dv::kMaxScanChannels];
    const size_t n = parse_plan(arg + 5, plan, masks, dv::kMaxScanChannels);
    if (n == 0 || st.scanner.active() || !st.scanner.set_plan(plan, n)) {
      write_line("RTL_DV_PLAN_INVALID use RTL_DV PLAN <MHz[:dmr|nxdn48|nxdn96|auto],... 1-16 entries, 24 MHz..1.766 GHz, scan stopped>");
      return true;
    }
    std::memcpy(st.plan_mask, masks, sizeof(masks));
    std::memcpy(st.saved_hz, plan, sizeof(plan));          // PLAN replaces the saved channel list and selects it
    std::memcpy(st.saved_mask, masks, sizeof(masks));
    st.saved_n = static_cast<uint8_t>(n);
    st.plan_option = 0;
    if (g_hooks.save_plan) g_hooks.save_plan(arg + 5);
    std::snprintf(line, sizeof(line), "RTL_DV_PLAN_OK channels=%u", static_cast<unsigned>(n));
    write_line(line);
    ++st.snapshot.revision;
    return true;
  }
  if (std::strncmp(arg, "RANGE ", 6) == 0) {         // RTL_DV RANGE <start MHz> <end MHz> <step kHz>: a custom scan range, selected immediately
    double a = 0, b = 0, k = 0;
    if (std::sscanf(arg + 6, "%lf %lf %lf", &a, &b, &k) != 3 || st.scanner.active() || k < 6.25 || b < a) {
      write_line("RTL_DV_RANGE_INVALID use RTL_DV RANGE <start MHz> <end MHz> <step kHz>, step >= 6.25, scan stopped");
      return true;
    }
    const uint32_t start = static_cast<uint32_t>(std::llround(a * 1e6)), step = static_cast<uint32_t>(std::llround(k * 1e3));
    const size_t count = static_cast<size_t>(std::llround((b - a) * 1e6 / step)) + 1;
    if (!st.scanner.set_range(start, step, count)) { write_line("RTL_DV_RANGE_INVALID outside 24 MHz..1.766 GHz or more than 8192 channels"); return true; }
    st.custom_start = start; st.custom_step = step; st.custom_count = count;
    st.plan_option = static_cast<uint8_t>(2 + dv::kBandPresetCount);
    std::snprintf(line, sizeof(line), "RTL_DV_RANGE_OK channels=%u est_pass_s=%u", static_cast<unsigned>(count), static_cast<unsigned>(count * 2200u / 1000u));
    write_line(line);
    ++st.snapshot.revision;
    return true;
  }
  if (std::strncmp(arg, "PRESET ", 7) == 0) {       // RTL_DV PRESET <n>: 0 = saved list, 1..N = built-in band range (same as the PLAN buttons)
    const int n = std::atoi(arg + 7);
    if (st.scanner.active() || n < 0 || static_cast<size_t>(n) >= plan_option_count(st)) { write_line("RTL_DV_PRESET_INVALID stop the scan; 0..N"); return true; }
    st.plan_option = static_cast<uint8_t>(n);
    apply_plan_option(st);
    std::snprintf(line, sizeof(line), "RTL_DV_PRESET_OK option=%d channels=%u", n, static_cast<unsigned>(st.scanner.plan_count()));
    write_line(line);
    ++st.snapshot.revision;
    return true;
  }
  if (std::strncmp(arg, "SAVE ", 5) == 0) {         // RTL_DV SAVE <discovered-signal index>: same handler as the SAVE button
    const bool ok = save_hit(st, static_cast<uint8_t>(std::atoi(arg + 5)));
    write_line(ok ? "RTL_DV_SAVE_OK added to the saved channel list" : "RTL_DV_SAVE_REFUSED needs a verified signal with a frequency, not already saved, list not full");
    return true;
  }
  if (std::strncmp(arg, "MASK ", 5) == 0) {
    const int m = std::atoi(arg + 5);
    if (m < 1 || m > 7) { write_line("RTL_DV_MASK_INVALID 1..7 (1=DMR 2=NXDN48 4=NXDN96)"); return true; }
    st.mask_override = static_cast<uint8_t>(m);
    dv_radio::set_protocol_mask(static_cast<uint8_t>(m));
    dv_radio::request_decoder_reset();
    write_line("RTL_DV_MASK_OK (override: forces these decoders on every channel)");
    return true;
  }
  if (std::strncmp(arg, "IQ ", 3) == 0) {          // development IQ capture: RTL_DV IQ ON|OFF|SNAP|STATUS
    const char* op = arg + 3;
    if (std::strcmp(op, "ON") == 0 || std::strcmp(op, "OFF") == 0) {
      dv_radio::iq_set_enabled(op[1] == 'N');
      if (g_hooks.flag_store) g_hooks.flag_store("dv_iq", dv_radio::iq_enabled());
      ++st.snapshot.revision;
    } else if (std::strcmp(op, "SNAP") == 0) {
      if (!dv_radio::iq_enabled()) { write_line("RTL_DV_IQ_REFUSED turn it on first: RTL_DV IQ ON"); return true; }
      dv_radio::iq_snap();
    }
    const auto q = dv_radio::iq_stats();
    std::snprintf(line, sizeof(line), "RTL_DV_IQ enabled=%d triggers=%lu saved=%lu failed=%lu skipped_busy=%lu skipped_cooldown=%lu skipped_limit=%lu files_this_visit=%u",
                  dv_radio::iq_enabled() ? 1 : 0, static_cast<unsigned long>(q.triggers), static_cast<unsigned long>(q.saved), static_cast<unsigned long>(q.failed),
                  static_cast<unsigned long>(q.skipped_busy), static_cast<unsigned long>(q.skipped_cooldown), static_cast<unsigned long>(q.skipped_limit),
                  static_cast<unsigned>(st.iq_files));
    write_line(line);
    return true;
  }
  if (std::strncmp(arg, "SCAN ", 5) == 0) {
    const char* op = arg + 5;
    if (std::strcmp(op, "START") == 0) {
      write_line(start_scan(st) ? "RTL_DV_SCAN_OK started" : "RTL_DV_SCAN_REFUSED (needs receiver + plan)");
    } else if (std::strcmp(op, "STOP") == 0) { stop_scan(st); write_line("RTL_DV_SCAN_OK stopped");
    } else if (std::strcmp(op, "PAUSE") == 0) { st.scanner.pause(); write_line("RTL_DV_SCAN_OK paused");
    } else if (std::strcmp(op, "RESUME") == 0) { st.scanner.resume(now()); write_line("RTL_DV_SCAN_OK resumed");
    } else if (std::strcmp(op, "HOLD") == 0) { st.scanner.toggle_manual_hold(); write_line("RTL_DV_SCAN_OK hold_toggled");
    } else if (std::strcmp(op, "SKIP") == 0) { st.scanner.skip(now()); write_line("RTL_DV_SCAN_OK skipped");
    } else { write_line("RTL_DV_SCAN_INVALID"); }
    ++st.snapshot.revision;
    return true;
  }
  if (std::strncmp(arg, "REPLAY ", 7) == 0) {
    const char* a = arg + 7;
    if (std::strcmp(a, "STOP") == 0) { stop_replay(st); write_line("RTL_DV_REPLAY_OK stopped"); return true; }
    char path[96];
    unsigned long hz = 0;
    if (std::sscanf(a, "%95s %lu", path, &hz) != 2 || hz < 24000000ul || hz > 1766000000ul) {
      write_line("RTL_DV_REPLAY_INVALID use RTL_DV REPLAY <sd path of 240kS/s CU8, channel 60 kHz above centre> <channel Hz> | STOP");
      return true;
    }
    write_line(start_replay(st, path, static_cast<uint32_t>(hz)) ? "RTL_DV_REPLAY_OK started (events are labelled REPLAY)"
                                                               : "RTL_DV_REPLAY_REFUSED (file or memory)");
    ++st.snapshot.revision;
    return true;
  }
  write_line("RTL_DV_UNKNOWN");
  return true;
}

}  // namespace orcsdr::digital_voice_app
