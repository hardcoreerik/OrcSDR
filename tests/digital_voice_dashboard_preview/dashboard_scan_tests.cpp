// SCAN screen: scenario manifest (state -> labels -> enabled actions -> draw hash) and the flicker regression suite.
// Runs the real renderer against the recording stand-in display (NOT a pixel renderer: it proves what is drawn, where, and what is not
// redrawn; physical flicker acceptance is a separate hardware step). Usage: dv_dashboard_scan [manifest.json]
#include "digital_voice_dashboard.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace orcsdr::digital_voice_ui;
using orcsdr::digital_voice_ui::layout::Rect;
namespace { int g_fail = 0; bool g_quiet = false; }
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static bool contains(const char* s) { return host_stub::contains(s); }
static bool inside(host_stub::R e, Rect r) { return e.w == 0 || e.h == 0 || (e.x >= r.x && e.y >= r.y && e.x + e.w <= r.x + r.w && e.y + e.h <= r.y + r.h); }
// Every drawing primitive since reset() must lie inside one of the allowed rectangles (the clip it was drawn under counts).
static bool all_inside(std::initializer_list<Rect> allowed) {
  for (const auto& p : host_stub::calls) {
    if (p.kind == host_stub::Prim::line) continue;                 // clip bookkeeping
    const host_stub::R e = host_stub::eff(p);
    bool ok = false;
    for (const Rect& r : allowed) ok = ok || inside(e, r);
    if (!ok) { if (!g_quiet) std::printf("   outside: kind=%d (%d,%d,%d,%d) '%s'\n", p.kind, e.x, e.y, e.w, e.h, p.str.c_str()); return false; }
  }
  return true;
}
static bool erased(Rect r) {       // an opaque fill covering the widget precedes anything drawn into it
  for (const auto& p : host_stub::calls)
    if ((p.kind == host_stub::Prim::fill || p.kind == host_stub::Prim::round_fill) && p.x <= r.x && p.y <= r.y && p.x + p.w >= r.x + r.w && p.y + p.h >= r.y + r.h) return true;
  return false;
}
static bool chrome_redrawn(const scan_layout::Geometry& g) {
  if (contains("SCAN / DISCOVER") || contains("DISCOVERED SIGNALS") || contains("SELECTED SIGNAL")) return true;
  for (const auto& p : host_stub::calls) {
    if (p.kind == host_stub::Prim::round_line && ((p.x == g.a.x && p.y == g.a.y) || (p.x == g.b.x && p.y == g.b.y))) return true;   // card borders
    if (p.kind == host_stub::Prim::fill && p.w >= 1200) return true;                                                                  // page-wide clears
  }
  return false;
}
static uint32_t draw_hash() {
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { for (int i = 0; i < 4; ++i) { h ^= (v >> (8 * i)) & 0xffu; h *= 16777619u; } };
  for (const auto& p : host_stub::calls) { mix(p.kind); mix(p.x); mix(p.y); mix(p.w); mix(p.h); for (char c : p.str) { h ^= (uint8_t)c; h *= 16777619u; } }
  return h;
}

// ---- snapshot builders (inputs only; the app is what fills these from the receiver) ----
static Snapshot base() {
  Snapshot s;
  s.receiver = Receiver::ready; s.origin = Origin::live; s.last_heard_available = s.systems_available = true;
  s.can_scan = true; s.can_save_channel = true;
  s.scan.mode = ScanMode::channel_list; s.scan.stage = ScanStage::ready; s.scan.phase = ScanPhase::idle; s.scan.planned = 5;
  std::strcpy(s.scan.plan_name, "SAVED LIST"); s.scan.plan_option = 0; s.scan.plan_options = 7; s.scan.est_pass_seconds = 11;
  s.scan.saved_capacity = 16; s.scan.saved_channels = 5;
  return s;
}
static void scanning(Snapshot& s, unsigned idx, uint32_t hz) {
  s.receiver = Receiver::receiving; s.scan.phase = ScanPhase::scanning; s.scan.stage = ScanStage::scanning; s.can_hold = s.can_skip = true;
  s.scan.channel_index = idx; s.scan.progress_valid = true; s.scan.visited = idx; s.scan.current_frequency_hz = hz; s.scan.current_frequency_valid = true;
}
static ScanHit dmr_hit(uint32_t hz, uint8_t cc, Evidence e, bool ident) {
  ScanHit h; h.protocol = Protocol::dmr; h.evidence = e; h.frequency_hz = hz; h.has_frequency = true; h.colour_code = cc; h.colour_code_valid = true;
  h.timeslot = 2; h.timeslot_valid = true; h.seen_count = 14; std::strcpy(h.first_time, "23:19:56"); std::strcpy(h.last_time, "23:21:12");
  if (ident) { h.source_id = 4106011; h.destination_id = 6011885; h.has_source = h.has_destination = h.group_call = true; }
  return h;
}
static ScanHit nxdn_hit(uint32_t hz, uint8_t ran) {
  ScanHit h; h.protocol = Protocol::nxdn96; h.evidence = Evidence::integrity_valid; h.frequency_hz = hz; h.has_frequency = true; h.ran = ran; h.ran_valid = true;
  h.nxdn_channel = NxdnChannel::rdch; h.nxdn_channel_valid = true; h.seen_count = 3; std::strcpy(h.last_time, "23:20:01"); h.is_new = true;
  return h;
}

struct Scenario {
  const char* id; const char* what;
  Snapshot (*build)();
  std::vector<const char*> expect;          // labels that must be drawn
  std::vector<const char*> absent;          // labels that must not be drawn
  bool start, hold, skip, save, prev, next;  // expected enabled controls
  unsigned selected = 0;
};
static Snapshot sc_disconnected() { Snapshot s = base(); s.receiver = Receiver::disconnected; s.can_scan = false; s.can_save_channel = false; s.scan.mode = ScanMode::unavailable;
  s.scan.stage = ScanStage::unavailable; s.scan.planned = 0; s.scan.plan_options = 0; s.scan.est_pass_seconds = 0; std::memset(s.scan.plan_name, 0, sizeof(s.scan.plan_name)); return s; }
static Snapshot sc_ready() { return base(); }
static Snapshot sc_list() { Snapshot s = base(); scanning(s, 3, 152157500); return s; }
static Snapshot sc_range() { Snapshot s = base(); s.scan.mode = ScanMode::frequency_range; std::strcpy(s.scan.plan_name, "VHF BUSINESS 150-158"); s.scan.planned = 641;
  s.scan.est_pass_seconds = 1410; s.scan.range_start_hz = 150007500; s.scan.range_end_hz = 157995000; s.scan.range_step_hz = 12500; scanning(s, 345, 154320000); return s; }
static Snapshot sc_sync() { Snapshot s = base(); scanning(s, 2, 153068000); s.scan.stage = ScanStage::sync; s.evidence = Evidence::sync; s.scan.candidates = 1; return s; }
static Snapshot sc_dmr() { Snapshot s = base(); scanning(s, 1, 152157500); s.scan.stage = ScanStage::integrity; s.evidence = Evidence::integrity_valid; s.protocol = Protocol::dmr;
  s.scan.validated = 1; s.scan.new_this_session = 1; s.scan_hit_count = 1; s.scan_hits[0] = dmr_hit(152157500, 13, Evidence::integrity_valid, false); return s; }
static Snapshot sc_nxdn() { Snapshot s = base(); scanning(s, 4, 454225000); s.scan.stage = ScanStage::integrity; s.evidence = Evidence::integrity_valid; s.protocol = Protocol::nxdn96;
  s.scan.validated = 1; s.scan_hit_count = 1; s.scan_hits[0] = nxdn_hit(454225000, 0); return s; }
static Snapshot sc_multi() { Snapshot s = base(); scanning(s, 3, 152685000); s.scan.validated = 3; s.scan.candidates = 1; s.scan.new_this_session = 2; s.scan.passes = 2; s.scan_hit_count = 4;
  s.scan_hits[0] = dmr_hit(152157500, 13, Evidence::identity_valid, true); s.scan_hits[1] = dmr_hit(152685000, 8, Evidence::integrity_valid, false);
  s.scan_hits[2] = nxdn_hit(454225000, 0); s.scan_hits[3] = dmr_hit(153068000, 8, Evidence::sync, false); s.scan_hits[3].seen_count = 1; return s; }
static Snapshot sc_hold() { Snapshot s = sc_multi(); s.scan.phase = ScanPhase::holding; s.scan.stage = ScanStage::hold; s.scan_on_hold = true; return s; }
static Snapshot sc_paused() { Snapshot s = sc_multi(); s.receiver = Receiver::paused; s.scan.phase = ScanPhase::paused; s.scan.stage = ScanStage::paused; return s; }
static Snapshot sc_error() { Snapshot s = base(); s.receiver = Receiver::error; s.scan.phase = ScanPhase::failed; s.scan.stage = ScanStage::error; s.can_hold = s.can_skip = false; return s; }
static Snapshot sc_empty() { Snapshot s = base(); scanning(s, 1, 152157500); return s; }
static Snapshot sc_full() { Snapshot s = sc_multi(); s.scan.stage = ScanStage::identity; s.evidence = Evidence::identity_valid; s.protocol = Protocol::dmr; return s; }
static Snapshot sc_ident() { Snapshot s = base(); scanning(s, 1, 152157500); s.scan.stage = ScanStage::identity; s.evidence = Evidence::identity_valid; s.protocol = Protocol::dmr;
  s.scan.validated = 1; s.scan_hit_count = 1; s.scan_hits[0] = dmr_hit(152157500, 13, Evidence::identity_valid, true); return s; }

static const std::vector<Scenario>& scenarios() {
  static const std::vector<Scenario> v = {
    {"S01_disconnected", "no receiver", sc_disconnected, {"SCANNER NOT CONNECTED", "NO SCAN PLAN", "NOTHING DISCOVERED YET"}, {"READY"}, false, false, false, false, false, false},
    {"S02_ready", "scanner ready, saved list of 5", sc_ready, {"READY", "CHANNEL LIST  SAVED LIST  5 CH", "PASS ~0:11", "START SCAN"}, {"SCANNING"}, true, false, false, false, true, true},
    {"S03_channel_list", "channel list scanning, position 3 of 5", sc_list, {"SCANNING", "CHANNEL LIST  SAVED LIST  CH 3 / 5", "152.157500 MHz", "STOP SCAN"}, {"READY"}, true, true, true, false, false, false},
    {"S04_frequency_range", "frequency range, position 345 of 641", sc_range, {"FREQUENCY RANGE  VHF BUSINESS 150-158  CH 345 / 641", "154.320000 MHz", "PASS ~23:30"}, {}, true, true, true, false, false, false},
    {"S05_sync_acquired", "sync-level activity, nothing verified", sc_sync, {"SYNC ACQUIRED", "153.068000 MHz"}, {"INTEGRITY VALIDATED", "IDENTITY VERIFIED"}, true, true, true, false, false, false},
    {"S06_dmr_integrity", "DMR integrity-validated, identity unknown", sc_dmr, {"INTEGRITY VALIDATED", "DMR", "CC13", "SRC UNKNOWN", "DST UNKNOWN"}, {"4106011"}, true, true, true, true, false, false},
    {"S07_nxdn_integrity", "NXDN96 integrity-validated, RAN 0", sc_nxdn, {"INTEGRITY VALIDATED", "NXDN96", "RAN 0", "RDCH / TRAFFIC", "NEW"}, {"IDENTITY VERIFIED"}, true, true, true, true, false, false},
    {"S08_multiple", "four discovered signals, selected 2", sc_multi, {"DMR", "NXDN96", "RAN 0", "CC8", "SYNC", "INTEGRITY", "IDENTITY"}, {}, true, true, true, true, false, false, 1},
    {"S09_hold", "hold on a channel", sc_hold, {"HOLD", "RESUME"}, {"SCANNING"}, true, true, true, true, false, false},
    {"S10_paused", "scan paused", sc_paused, {"PAUSED", "RECEPTION PAUSED"}, {}, true, true, true, true, false, false},
    {"S11_error", "receiver error", sc_error, {"ERROR", "RECEIVER ERROR"}, {}, true, false, false, false, true, true},
    {"S12_empty_list", "scanning, nothing discovered", sc_empty, {"NOTHING DISCOVERED YET", "SELECT A DISCOVERED SIGNAL"}, {"CC13"}, true, true, true, false, false, false},
    {"S13_fully_populated", "four signals, identity verified, detail of the DMR call", sc_full, {"IDENTITY VERIFIED", "SRC 4106011", "TG 6011885", "CC13  TS2", "OBSERVED 14 TIMES", "FIRST 23:19:56  LAST 23:21:12"}, {}, true, true, true, true, false, false, 0},
    {"S14_dmr_identity", "one DMR call with decoded identity", sc_ident, {"IDENTITY VERIFIED", "SRC 4106011", "TG 6011885", "GROUP"}, {}, true, true, true, true, false, false},
  };
  return v;
}
static const char* kActionNames[6] = {"start", "hold", "skip", "save", "plan_prev", "plan_next"};

static void run_scenarios(FILE* manifest) {
  if (manifest) std::fputs("{\n  \"kind\": \"host recording stand-in, not pixels and not a physical capture\",\n  \"scenarios\": [\n", manifest);
  bool first = true;
  for (const Scenario& sc : scenarios()) {
    for (Density d : {Density::discovery_first, Density::analysis_first}) {
      lgfx::LovyanGFX display; Dashboard dash; UiState ui; ui.density = d; ui.view = View::explore; ui.selected_scan_hit = static_cast<uint8_t>(sc.selected);
      const Snapshot s = sc.build();
      host_stub::reset();
      CHECK(dash.present(display, s, ui, true, false));
      CHECK(!host_stub::out_of_bounds && host_stub::full_clear_count == 0);
      for (const char* e : sc.expect) { if (!contains(e)) { std::printf("   %s/%d missing '%s'\n", sc.id, int(d), e); ++g_fail; } }
      for (const char* e : sc.absent) { if (contains(e)) { std::printf("   %s/%d must not show '%s'\n", sc.id, int(d), e); ++g_fail; } }
      const bool en[6] = {action_state(ActionKind::scan, s, ui).enabled, action_state(ActionKind::toggle_hold, s, ui).enabled,
                          action_state(ActionKind::skip_frequency, s, ui).enabled, action_state(ActionKind::save_channel, s, ui).enabled,
                          action_state(ActionKind::plan_prev, s, ui).enabled, action_state(ActionKind::plan_next, s, ui).enabled};
      const bool want[6] = {sc.start, sc.hold, sc.skip, sc.save, sc.prev, sc.next};
      for (int i = 0; i < 6; ++i) if (en[i] != want[i]) { std::printf("   %s/%d action %s enabled=%d expected %d\n", sc.id, int(d), kActionNames[i], en[i], want[i]); ++g_fail; }
      // a disabled control must not produce an action when touched
      const scan_layout::Geometry g = scan_layout::geometry(d);
      const Rect rects[6] = {g.start, g.hold, g.skip, g.save, g.plan_prev, g.plan_next};
      for (int i = 0; i < 6; ++i) {
        const Action a = dash.handle_touch(rects[i].x + 4, rects[i].y + 4, s, ui);
        if ((a.kind != ActionKind::none) != want[i]) { std::printf("   %s/%d touch on %s gave kind %d\n", sc.id, int(d), kActionNames[i], int(a.kind)); ++g_fail; }
      }
      if (manifest) {
        std::fprintf(manifest, "%s    {\"id\": \"%s\", \"density\": \"%s\", \"what\": \"%s\", \"enabled\": {", first ? "" : ",\n", sc.id,
                     d == Density::discovery_first ? "discovery" : "analysis", sc.what);
        for (int i = 0; i < 6; ++i) std::fprintf(manifest, "%s\"%s\": %s", i ? ", " : "", kActionNames[i], want[i] ? "true" : "false");
        std::fputs("}, \"labels\": [", manifest);
        for (size_t i = 0; i < sc.expect.size(); ++i) std::fprintf(manifest, "%s\"%s\"", i ? ", " : "", sc.expect[i]);
        std::fprintf(manifest, "], \"draw_calls\": %zu, \"draw_hash\": \"%08x\"}", host_stub::calls.size(), draw_hash());
        first = false;
      }
    }
  }
  if (manifest) std::fputs("\n  ],\n  \"not_available\": [\"window sweep: needs a wideband survey stage that is not built\", \"RF-activity (energy) stage: no energy detector in the decoder chain\"]\n}\n", manifest);
}

// ---- flicker regression ----
static void flicker(Density d) {
  const scan_layout::Geometry g = scan_layout::geometry(d);
  lgfx::LovyanGFX display; Dashboard dash; UiState ui; ui.density = d; ui.view = View::explore;
  Snapshot s = sc_multi(); s.revision = 1;
  host_stub::reset();
  CHECK(dash.present(display, s, ui, true, false));                                  // entry: whole layout once
  CHECK(host_stub::full_clear_count == 0 && !host_stub::out_of_bounds);
  CHECK(contains("SCAN / DISCOVER") && contains("DISCOVERED SIGNALS") && contains("SELECTED SIGNAL"));
  const size_t entry_calls = host_stub::calls.size();
  std::printf("  draw calls (%s): screen entry %zu", d == Density::discovery_first ? "discovery" : "analysis", entry_calls);
  // 1. repeated frequency updates: only the frequency widget (and the position/progress it implies) is touched
  const uint32_t freqs[5] = {152157500, 152345000, 152685000, 153068000, 454225000};
  char prev[32] = "";
  for (int i = 0; i < 5; ++i) {
    s.scan.current_frequency_hz = freqs[i]; ++s.revision;
    host_stub::reset();
    CHECK(dash.present(display, s, ui, true, false));
    char now[32]; std::snprintf(now, sizeof(now), "%lu.%06lu MHz", (unsigned long)freqs[i] / 1000000ul, (unsigned long)freqs[i] % 1000000ul);
    CHECK(contains(now)); if (prev[0]) CHECK(!contains(prev));                      // new value drawn, old one not drawn again
    CHECK(erased(g.freq));                                                           // and the old pixels were covered first
    if (i == 1) std::printf("  frequency update %zu", host_stub::calls.size());
    CHECK(all_inside({g.freq, g.ladder}));                                           // the ladder's RECORDED chip depends on the frequency
    CHECK(!chrome_redrawn(g) && host_stub::full_clear_count == 0 && host_stub::calls.size() < entry_calls / 4);
    std::strcpy(prev, now);
  }
  // 2. scan progress: position text and bar only
  for (unsigned idx = 4; idx <= 5; ++idx) {
    s.scan.channel_index = idx; s.scan.visited = idx; ++s.revision;
    host_stub::reset();
    CHECK(dash.present(display, s, ui, true, false));
    char t[48]; std::snprintf(t, sizeof(t), "CH %u / 5", idx);
    CHECK(contains(t)); CHECK(erased(g.plan) && erased({g.bar.x + 1, g.bar.y + 1, g.bar.w - 2, g.bar.h - 2}));
    if (idx == 4) std::printf("  progress update %zu", host_stub::calls.size());
    CHECK(all_inside({g.plan, g.bar})); CHECK(!chrome_redrawn(g));
  }
  // 3. a new signal is discovered: its list row, the counts, the detail panel if it is the selected one
  s.scan_hit_count = 3; ++s.revision; dash.present(display, s, ui, true, false);
  s.scan_hits[3] = nxdn_hit(453950000, 5); s.scan_hit_count = 4; s.scan.validated = 4; s.scan.new_this_session = 3; ++s.revision;
  host_stub::reset();
  CHECK(dash.present(display, s, ui, true, false));
  CHECK(contains("453.950000 MHz") && contains("RAN 5"));
  CHECK(all_inside({g.row[3], g.counts, g.reason, g.save}));
  std::printf("  new signal %zu", host_stub::calls.size());
  CHECK(!chrome_redrawn(g) && !contains("152.157500 MHz  x14"));                       // the other rows were not redrawn
  // 4. selection change: the two rows concerned, the detail panel and the SAVE state
  ui.selected_scan_hit = 2; ++s.revision;
  host_stub::reset();
  CHECK(dash.present(display, s, ui, true, false));
  CHECK(contains("RAN 0") && contains("RDCH / TRAFFIC"));
  std::printf("  selection change %zu", host_stub::calls.size());
  CHECK(all_inside({g.row[0], g.row[1], g.row[2], g.row[3], g.detail, g.reason, g.save}) && !chrome_redrawn(g));
  // 5. evidence transitions on the current channel: status banner and evidence ladder
  const ScanStage stages[4] = {ScanStage::sync, ScanStage::integrity, ScanStage::identity, ScanStage::scanning};
  const Evidence evs[4] = {Evidence::sync, Evidence::integrity_valid, Evidence::identity_valid, Evidence::none};
  for (int i = 0; i < 4; ++i) {
    s.scan.stage = stages[i]; s.evidence = evs[i]; ++s.revision;
    host_stub::reset();
    CHECK(dash.present(display, s, ui, true, false));
    CHECK(contains(scan_stage_name(stages[i]))); CHECK(erased(g.status) && erased(g.ladder));
    CHECK(all_inside({g.status, g.ladder, g.row[0], g.row[1], g.row[2], g.row[3]}) && !chrome_redrawn(g));
  }
  // 6. start / stop, hold / resume
  Snapshot idle = s; idle.scan.phase = ScanPhase::idle; idle.scan.stage = ScanStage::ready; idle.scan.channel_index = 0; idle.scan.progress_valid = false; idle.can_hold = idle.can_skip = false; ++idle.revision;
  host_stub::reset(); CHECK(dash.present(display, idle, ui, true, false));
  CHECK(contains("READY") && contains("START SCAN") && all_inside({g.status, g.freq, g.plan, g.bar, g.counts, g.ladder, g.reason, g.start, g.hold, g.skip, g.save, g.plan_prev, g.plan_next, g.row[0], g.row[1], g.row[2], g.row[3], g.detail}));
  CHECK(!chrome_redrawn(g));
  Snapshot run = s; run.scan_on_hold = false; ++run.revision; run.revision += 10;
  host_stub::reset(); CHECK(dash.present(display, run, ui, true, false)); CHECK(!chrome_redrawn(g) && host_stub::full_clear_count == 0);
  Snapshot held = run; held.scan.phase = ScanPhase::holding; held.scan.stage = ScanStage::hold; held.scan_on_hold = true; ++held.revision;
  host_stub::reset(); CHECK(dash.present(display, held, ui, true, false));
  CHECK(contains("RESUME") && contains("HOLD") && all_inside({g.status, g.hold, g.ladder, g.plan, g.bar, g.counts, g.reason, g.start, g.skip, g.save, g.plan_prev, g.plan_next, g.row[0], g.row[1], g.row[2], g.row[3], g.detail}) && !chrome_redrawn(g));
  Snapshot resumed = held; resumed.scan_on_hold = false; resumed.scan.phase = ScanPhase::scanning; resumed.scan.stage = ScanStage::scanning; ++resumed.revision;
  host_stub::reset(); CHECK(dash.present(display, resumed, ui, true, false)); CHECK(contains("SCANNING") && !chrome_redrawn(g));
  // 7. changes that are not visible on SCAN draw nothing at all, whatever the revision counter says
  Snapshot quiet = resumed; quiet.iq_drops = 7; quiet.sync_count += 3; quiet.integrity_frames += 5; quiet.relative_level_tenths_db = -140; ++quiet.revision;
  host_stub::reset(); CHECK(dash.present(display, quiet, ui, true, false)); CHECK(host_stub::calls.empty());
  Snapshot rev_only = quiet; rev_only.revision += 50; host_stub::reset(); CHECK(dash.present(display, rev_only, ui, true, false)); CHECK(host_stub::calls.empty());
  // 8. overlay and ownership protection
  Snapshot other = quiet; other.scan.current_frequency_hz = 152157500; ++other.revision;
  host_stub::reset(); CHECK(!dash.present(display, other, ui, true, true) && host_stub::calls.empty());      // volume tray open
  host_stub::reset(); CHECK(!dash.present(display, other, ui, false, false) && host_stub::calls.empty());    // screen not ours
  host_stub::reset(); CHECK(dash.present(display, other, ui, true, false) && contains("152.157500 MHz") && all_inside({g.freq}));   // nothing was lost meanwhile
  // 9. leaving and re-entering repaints the whole layout once
  dash.leave(); ++other.revision;
  host_stub::reset(); CHECK(dash.present(display, other, ui, true, false));
  CHECK(contains("SCAN / DISCOVER") && host_stub::full_clear_count == 0 && host_stub::calls.size() >= entry_calls / 2);
  std::printf("  re-entry %zu\n", host_stub::calls.size());
}


// Negative control: the harness must be able to see a whole-page repaint. Forcing screen re-entry on every update (what a generic revision
// counter used to cause) has to trip the same detectors the regression relies on.
static void negative_control(Density d) {
  const scan_layout::Geometry g = scan_layout::geometry(d);
  lgfx::LovyanGFX display; Dashboard dash; UiState ui; ui.density = d; ui.view = View::explore;
  Snapshot s = sc_multi(); s.revision = 1;
  dash.present(display, s, ui, true, false);
  s.scan.current_frequency_hz = 152345000; ++s.revision;
  dash.leave();                                              // the old behaviour: repaint everything
  host_stub::reset();
  CHECK(dash.present(display, s, ui, true, false));
  CHECK(chrome_redrawn(g));                                   // static chrome painted again
  g_quiet = true; const bool escaped = !all_inside({g.freq}); g_quiet = false;
  CHECK(escaped);                               // and the draw escaped the frequency widget
}

// Regression for a collision seen on the device: a row with a count, a last time and a SAVED/NEW tag must not overlap text on its second line.
static void row_collision(Density d) {
  const scan_layout::Geometry g = scan_layout::geometry(d);
  lgfx::LovyanGFX display; Dashboard dash; UiState ui; ui.density = d; ui.view = View::explore;
  Snapshot s = sc_multi(); s.scan_hits[0].saved = true; s.scan_hits[1].is_new = true; s.revision = 1;
  host_stub::reset(); CHECK(dash.present(display, s, ui, true, false));
  for (size_t row = 0; row < 2; ++row)
    for (const auto& a : host_stub::calls) {
      if (a.kind != host_stub::Prim::text || a.str.empty()) continue;
      const host_stub::R ea = host_stub::eff(a);
      if (!inside(ea, g.row[row])) continue;
      for (const auto& b : host_stub::calls) {
        if (&a == &b || b.kind != host_stub::Prim::text || b.str.empty()) continue;
        const host_stub::R eb = host_stub::eff(b);
        if (!inside(eb, g.row[row])) continue;
        const bool overlap = ea.x < eb.x + eb.w && eb.x < ea.x + ea.w && ea.y < eb.y + eb.h && eb.y < ea.y + ea.h;
        if (overlap) { std::printf("   row %zu: '%s' overlaps '%s'\n", row, a.str.c_str(), b.str.c_str()); ++g_fail; }
      }
    }
}

int main(int argc, char** argv) {
  FILE* manifest = argc > 1 ? std::fopen(argv[1], "w") : nullptr;
  run_scenarios(manifest);
  if (manifest) std::fclose(manifest);
  flicker(Density::discovery_first);
  flicker(Density::analysis_first);
  row_collision(Density::discovery_first);
  row_collision(Density::analysis_first);
  negative_control(Density::discovery_first);
  negative_control(Density::analysis_first);
  if (g_fail) { std::printf("FAIL dv_dashboard_scan: %d failures\n", g_fail); return 1; }
  std::printf("PASS dv_dashboard_scan: %zu scenarios x 2 densities, flicker regression x 2 densities (recording stand-in only)\n", scenarios().size());
  return 0;
}
