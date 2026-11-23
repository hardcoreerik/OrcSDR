#include "digital_voice_app.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "digital_voice_dashboard_surface.hpp"
#include "dv_radio.hpp"
#include "dv_scanner.hpp"
#include "screen_controller.hpp"

namespace orcsdr::digital_voice_app {
namespace {
using namespace digital_voice_ui;

// Everything lives in PSRAM for the lifetime of a visit: the Tab5 internal boot pool is nearly full.
struct State {
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
  uint8_t mask_override = 0;                   // RTL_DV MASK: forces one mask on every channel
  uint8_t auto_phase = 0;
  uint32_t auto_switch_ms = 0;
};
State* g_state = nullptr;
Hooks g_hooks{};
uint32_t g_open_failed = 0, g_unapplied_actions = 0, g_repaints = 0, g_painted_revision = 0;

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

void install_radio_hooks() {
  dv_radio::Hooks h;
  h.device_ready = g_hooks.receiver_ready;
  h.tune = g_hooks.tune;
  h.release = g_hooks.release;
  h.now_ms = g_hooks.now_ms;
  h.micros = g_hooks.micros;
  h.lost_samples = g_hooks.lost_samples;
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

void load_saved_plan() {
  if (!g_state || !g_hooks.load_plan) return;
  char csv[160];
  if (!g_hooks.load_plan(csv, sizeof(csv))) return;
  uint32_t plan[dv::kMaxScanChannels];
  uint8_t masks[dv::kMaxScanChannels];
  const size_t n = parse_plan(csv, plan, masks, dv::kMaxScanChannels);
  if (n && g_state->scanner.set_plan(plan, n)) std::memcpy(g_state->plan_mask, masks, sizeof(masks));
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

void note_event(State& st, const dv_radio::Event& e) {
  Snapshot& s = st.snapshot;
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
  const uint32_t utc = g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0;
  if (utc) { h.has_time = true; format_utc(utc, h.time_utc, sizeof(h.time_utc)); }
  // Systems: distinct (protocol, frequency, RAN/CC) observations, bounded.
  bool found = false;
  for (size_t i = 0; i < s.observed_system_count; ++i) {
    const ObservedSystem& o = s.observed_systems[i];
    if (o.protocol == e.protocol && o.frequency_hz == e.frequency_hz && o.ran == e.ran) { found = true; break; }
  }
  if (!found && s.observed_system_count < kObservedSystems) {
    ObservedSystem& o = s.observed_systems[s.observed_system_count++];
    o = {};
    o.protocol = e.protocol; o.evidence = e.evidence; o.frequency_hz = e.frequency_hz; o.has_frequency = true;
    o.ran = e.ran; o.ran_valid = e.ran_valid;
  }
  ++s.revision;
}

void clear_call(State& st) {
  Snapshot& s = st.snapshot;
  s.protocol = Protocol::unknown; s.evidence = Evidence::none;
  s.source_id = s.destination_id = 0; s.has_source = s.has_destination = s.group_call = false;
  s.colour_code = s.timeslot = s.ran = 0; s.colour_code_valid = s.timeslot_valid = s.ran_valid = false;
  s.nxdn = {};
  st.have_call = false;
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
  const bool diag = rs.running;
  const int32_t level = rs.level_milli > 0 ? static_cast<int32_t>(std::lround(200.0 * std::log10(rs.level_milli / 1000.0))) : 0;
  const bool changed = s.receiver != rx || s.scan.phase != scan.phase || s.scan.visited != scan.visited ||
      s.scan.current_frequency_hz != scan.current_frequency_hz || s.can_scan != can_scan || s.can_hold != active ||
      s.scan_on_hold != ss.manual_hold || s.sync_count != rs.dmr_acquisitions + rs.nxdn48_frames + rs.nxdn96_frames ||
      s.integrity_frames != rs.events || s.iq_drops != rs.loss_events || s.diagnostics_valid != diag ||
      s.relative_level_valid != (rs.level_milli > 0) || (rs.level_milli > 0 && std::abs(s.relative_level_tenths_db - level) >= 10);
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
  s.can_save_channel = false;      // save-as-profile needs the storage flow (not wired yet): never offered
  if (!st.have_call && ss.frequency_hz && active) {      // show the channel being examined; identity stays unknown
    s.frequency_hz = ss.frequency_hz; s.has_frequency = true;
  }
  if (ss.holds != st.last_hold_count) {                   // a hold = a verified scan hit
    st.last_hold_count = ss.holds;
    if (s.scan_hit_count < kScanHits && st.have_call) {
      ScanHit& hit = s.scan_hits[s.scan_hit_count++];
      hit.protocol = s.protocol; hit.evidence = s.evidence; hit.frequency_hz = s.frequency_hz; hit.has_frequency = true;
      hit.ran = s.ran; hit.ran_valid = s.ran_valid;
    }
  }
  if (changed) ++s.revision;
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
  st.replay_fed = 0;
  st.replay_start_ms = now();
  st.replaying = true;
  st.replay_done = false;
  return true;
}

void run_scanner(State& st) {
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
  const dv::ScanStep step = st.scanner.service(now(), in);
  if (step.tune) {
    const bool ok = dv_radio::tune_channel(step.tune_hz);
    st.scanner.retune_result(ok, now());
    if (ok) {
      clear_call(st);                // new channel visit: nothing from the previous channel carries over
      const uint8_t entry = st.plan_mask[st.scanner.status().index];
      st.auto_phase = 0;
      st.auto_switch_ms = now();
      dv_radio::set_protocol_mask(st.mask_override ? st.mask_override : entry ? entry : dv_radio::kMaskDmr);
    }
  }
  // An "auto" entry cannot run every decoder at once (NXDN48 alone needs more than one core on the Tab5): it rotates them
  // through the dwell, restarting the decoder set at each switch so no decoder bridges a window it did not see.
  if (st.scanner.active() && !st.mask_override && st.plan_mask[st.scanner.status().index] == 0 &&
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

void apply_radio_action(State& st, const Action& a) {
  switch (a.kind) {
    case ActionKind::scan:
      if (st.scanner.active()) stop_scan(st);
      else (void)start_scan(st);
      break;
    case ActionKind::toggle_hold: st.scanner.toggle_manual_hold(); break;
    case ActionKind::skip_frequency: st.scanner.skip(now()); break;
    default: ++g_unapplied_actions; return;     // save / setup: no owner yet, never faked
  }
  ++st.actions_applied;
  ++st.snapshot.revision;
}
}  // namespace

void install(const Hooks& hooks) { g_hooks = hooks; }

void open() {
  if (!ensure_state()) {
    ++g_open_failed;
    if (g_hooks.go_home) g_hooks.go_home();
    return;
  }
  new (&g_state->snapshot) Snapshot();
  g_state->scanner = dv::Scanner();
  load_saved_plan();
  refresh(*g_state);
  g_state->snapshot.revision = 1;
  screens::begin_transition(screens::Id::digital_voice, now());
  screens::finish_transition();
  if (!full_draw()) {
    ++g_open_failed;
    leave_to_home();
  }
}

void close() {
  if (!g_state) return;
  stop_replay(*g_state);
  g_state->scanner.stop();
  dv_radio::stop();
  g_state->surface.leave();
  free_state();
}

void tick() {
  if (!g_state) return;
  run_scanner(*g_state);
  refresh(*g_state);
  // update() also returns true when it correctly drew nothing; count only real model changes as repaints.
  if (g_state->surface.update(M5.Display, g_state->snapshot, overlay()) && g_state->snapshot.revision != g_painted_revision) {
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
                "manual_hold=%d can_scan=%u hist=%u systems=%u rev=%lu repaints=%lu applied=%lu unapplied=%lu",
                static_cast<unsigned>(ui.view), static_cast<unsigned>(ui.density),
                receiver_name(g_state->snapshot.receiver), scan_phase_name(g_state->snapshot.scan.phase),
                static_cast<unsigned>(ss.count), static_cast<unsigned>(ss.index), static_cast<unsigned long>(ss.visited),
                static_cast<unsigned long>(ss.holds), static_cast<unsigned long>(ss.passes), ss.manual_hold ? 1 : 0,
                g_state->snapshot.can_scan ? 1u : 0u, static_cast<unsigned>(g_state->snapshot.history_count),
                static_cast<unsigned>(g_state->snapshot.observed_system_count),
                static_cast<unsigned long>(g_state->snapshot.revision), static_cast<unsigned long>(g_repaints),
                static_cast<unsigned long>(g_state->actions_applied), static_cast<unsigned long>(g_unapplied_actions));
}

bool handle_command(const char* command, bool authenticated, void (*write_line)(const char*)) {
  if (!command || !write_line || std::strncmp(command, "RTL_DV ", 7) != 0) return false;
  const char* arg = command + 7;
  char line[640];
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
  if (std::strncmp(arg, "PLAN ", 5) == 0) {
    uint32_t plan[dv::kMaxScanChannels];
    uint8_t masks[dv::kMaxScanChannels];
    const size_t n = parse_plan(arg + 5, plan, masks, dv::kMaxScanChannels);
    if (n == 0 || st.scanner.active() || !st.scanner.set_plan(plan, n)) {
      write_line("RTL_DV_PLAN_INVALID use RTL_DV PLAN <MHz[:dmr|nxdn48|nxdn96|auto],... 1-16 entries, 24 MHz..1.766 GHz, scan stopped>");
      return true;
    }
    std::memcpy(st.plan_mask, masks, sizeof(masks));
    if (g_hooks.save_plan) g_hooks.save_plan(arg + 5);
    std::snprintf(line, sizeof(line), "RTL_DV_PLAN_OK channels=%u", static_cast<unsigned>(n));
    write_line(line);
    ++st.snapshot.revision;
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
