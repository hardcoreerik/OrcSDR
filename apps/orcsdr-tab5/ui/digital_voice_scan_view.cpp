#include "digital_voice_scan_view.hpp"
#include "digital_voice_dashboard_paint.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::digital_voice_ui {
namespace scan_layout {
Geometry geometry(Density d) {
  Geometry g{};
  const bool basic = d == Density::discovery_first;
  g.a = basic ? layout::kBasicHero : layout::kAdvancedMain;
  g.b = basic ? layout::kBasicFacts : layout::kAdvancedFacts;
  const Rect a = g.a, b = g.b;
  g.status = {a.x + 14, a.y + 46, a.w - 28, 50};
  g.freq = {a.x + 14, a.y + 100, a.w - 28, 64};
  g.plan = {a.x + 14, a.y + 168, a.w - 28, 30};
  g.bar = {a.x + 18, a.y + 206, a.w - 36, 16};
  g.counts = {a.x + 14, a.y + 230, a.w - 28, 34};
  g.ladder = {a.x + 14, a.y + 272, a.w - 28, 54};
  g.reason = {a.x + 14, a.y + 334, a.w - 28, 30};
  if (basic) {
    g.start = {a.x + 24, a.y + 419, 170, 44};
    g.hold = {a.x + 202, a.y + 419, 132, 44};
    g.skip = {a.x + 342, a.y + 419, 106, 44};
    g.save = {a.x + 456, a.y + 419, 112, 44};
    g.plan_prev = {a.x + 584, a.y + 419, 100, 44};
    g.plan_next = {a.x + 692, a.y + 419, 100, 44};
  } else {
    g.start = {a.x + 18, a.y + 376, 170, 42};
    g.hold = {a.x + 196, a.y + 376, 122, 42};
    g.skip = {a.x + 326, a.y + 376, 98, 42};
    g.save = {a.x + 18, a.y + 426, 142, 42};
    g.plan_prev = {a.x + 168, a.y + 426, 120, 42};
    g.plan_next = {a.x + 296, a.y + 426, 120, 42};
  }
  const int row_h = 50;
  for (size_t i = 0; i < kScanHits; ++i) g.row[i] = {b.x + 10, b.y + 54 + static_cast<int>(i) * (row_h + 4), b.w - 20, row_h};
  g.list_title_y = b.y + 32;
  g.detail_title_y = b.y + 54 + static_cast<int>(kScanHits) * (row_h + 4) + 14;
  g.detail = {b.x + 10, g.detail_title_y + 14, b.w - 20, b.y + b.h - 36 - (g.detail_title_y + 14)};
  g.footer_y = b.y + b.h - 18;
  return g;
}
}  // namespace scan_layout

namespace {
using namespace paint;
using scan_layout::Geometry;

enum W { w_status, w_freq, w_plan, w_bar, w_counts, w_ladder, w_reason, w_start, w_hold, w_skip, w_save, w_prev, w_next,
         w_row0, w_detail = w_row0 + static_cast<int>(kScanHits), w_count };
static_assert(w_count <= ScanView::kWidgets, "signature table too small");

struct Hash {
  uint32_t h = 2166136261u;
  void u32(uint32_t v) { for (int i = 0; i < 4; ++i) { h ^= (v >> (8 * i)) & 0xffu; h *= 16777619u; } }
  void str(const char* s) { while (s && *s) { h ^= static_cast<uint8_t>(*s++); h *= 16777619u; } h ^= 0xffu; h *= 16777619u; }
};

bool running(const Snapshot& s) {
  return s.scan.phase == ScanPhase::settling || s.scan.phase == ScanPhase::scanning || s.scan.phase == ScanPhase::holding ||
         s.scan.phase == ScanPhase::paused;
}
uint16_t stage_color(ScanStage st) {
  switch (st) {
    case ScanStage::ready:return kWhite;
    case ScanStage::scanning:return kCyan;
    case ScanStage::candidate:case ScanStage::sync:return kAmber;
    case ScanStage::integrity:case ScanStage::identity:return kGreen;
    case ScanStage::hold:return kCyan;
    case ScanStage::paused:return kAmber;
    case ScanStage::error:return kRed;
    default:return kMuted;
  }
}
const char* short_evidence(Evidence e) {
  switch (e) {
    case Evidence::energy:return "ENERGY";
    case Evidence::candidate:return "CANDIDATE";
    case Evidence::sync:return "SYNC";
    case Evidence::integrity_valid:return "INTEGRITY";
    case Evidence::identity_valid:return "IDENTITY";
    default:return "NONE";
  }
}
uint16_t evidence_color(Evidence e) {
  switch (e) {
    case Evidence::identity_valid:return kGreen;
    case Evidence::integrity_valid:return kCyan;
    case Evidence::candidate:case Evidence::sync:case Evidence::energy:return kAmber;
    default:return kMuted;
  }
}
void mhz(char (&out)[32], uint32_t hz) {
  std::snprintf(out, sizeof(out), "%lu.%06lu MHz", static_cast<unsigned long>(hz / 1000000u), static_cast<unsigned long>(hz % 1000000u));
}
void area(char (&out)[16], Protocol pr, uint8_t cc, bool ccv, uint8_t ran, bool ranv) {
  out[0] = 0;
  if (pr == Protocol::dmr && ccv) std::snprintf(out, sizeof(out), "CC%u", static_cast<unsigned>(cc));
  else if ((pr == Protocol::nxdn48 || pr == Protocol::nxdn96) && ranv) std::snprintf(out, sizeof(out), "RAN %u", static_cast<unsigned>(ran));
}
uint32_t current_frequency(const Snapshot& s, bool* known) {
  if (s.scan.current_frequency_valid && (running(s) || !s.has_frequency)) { *known = true; return s.scan.current_frequency_hz; }
  *known = s.has_frequency;
  return s.frequency_hz;
}
bool recorded_here(const Snapshot& s, uint32_t hz) {
  for (size_t i = 0; i < s.scan_hit_count && i < kScanHits; ++i)
    if (s.scan_hits[i].has_frequency && s.scan_hits[i].frequency_hz == hz && s.scan_hits[i].evidence >= Evidence::integrity_valid) return true;
  return false;
}

// ---- widgets: signature (what it shows) + draw (inside its own rect only) ----
uint32_t sig_status(const Snapshot& s) { Hash h; h.u32(static_cast<uint32_t>(s.scan.stage)); h.u32(static_cast<uint32_t>(s.receiver)); return h.h; }
void draw_status(Paint& p, Rect r, const Snapshot& s) {
  p.g.fillRect(r.x, r.y, r.w, r.h, kPanel);
  p.text(scan_stage_name(s.scan.stage), r.x + 6, r.y + r.h / 2, stage_color(s.scan.stage), 3, r);
  p.text(receiver_name(s.receiver), r.x + r.w - 6 - static_cast<int>(std::strlen(receiver_name(s.receiver))) * 12, r.y + r.h / 2,
         s.receiver == Receiver::error ? kRed : kMuted, 2, r);
}
uint32_t sig_freq(const Snapshot& s) { bool k; Hash h; const uint32_t hz = current_frequency(s, &k); h.u32(hz); h.u32(k); return h.h; }
void draw_freq(Paint& p, Rect r, const Snapshot& s) {
  bool known; const uint32_t hz = current_frequency(s, &known);
  p.g.fillRect(r.x, r.y, r.w, r.h, kPanel);
  if (known) { char f[32]; mhz(f, hz); p.text(f, r.x + 6, r.y + r.h / 2, kWhite, 4, r); }
  else p.text("FREQUENCY UNKNOWN", r.x + 6, r.y + r.h / 2, kAmber, 3, r);
}
uint32_t sig_plan(const Snapshot& s) {
  Hash h; h.u32(static_cast<uint32_t>(s.scan.mode)); h.str(s.scan.plan_name); h.u32(s.scan.channel_index); h.u32(s.scan.planned);
  h.u32(s.scan.est_pass_seconds); h.u32(s.scan.passes); h.u32(s.scan.range_start_hz); h.u32(s.scan.range_end_hz); h.u32(s.scan.range_step_hz); return h.h;
}
void draw_plan(Paint& p, Rect r, const Snapshot& s) {
  p.g.fillRect(r.x, r.y, r.w, r.h, kPanel);
  char t[96];
  if (s.scan.mode == ScanMode::unavailable) { p.label("NO SCAN PLAN", r.x + 6, r.y + r.h / 2, r); return; }
  if (s.scan.channel_index && s.scan.planned) std::snprintf(t, sizeof(t), "%s  %s  CH %u / %u", scan_mode_name(s.scan.mode), s.scan.plan_name,
        static_cast<unsigned>(s.scan.channel_index), static_cast<unsigned>(s.scan.planned));
  else std::snprintf(t, sizeof(t), "%s  %s  %u CH", scan_mode_name(s.scan.mode), s.scan.plan_name, static_cast<unsigned>(s.scan.planned));
  p.label(t, r.x + 6, r.y + r.h / 2, r);
  if (s.scan.est_pass_seconds) {       // an estimate (channels x settle + dwell), labelled as one
    const unsigned sec = s.scan.est_pass_seconds;
    char e[40]; std::snprintf(e, sizeof(e), "PASS ~%u:%02u", sec / 60, sec % 60);
    p.text(e, r.x + r.w - 6 - static_cast<int>(std::strlen(e)) * 12, r.y + r.h / 2, kMuted, 2, r);
  }
}
int bar_fill(const Snapshot& s, int w) {
  if (!s.scan.progress_valid || !s.scan.planned || !s.scan.channel_index) return 0;
  const long done = static_cast<long>(s.scan.channel_index) * w / static_cast<long>(s.scan.planned);
  return static_cast<int>(std::min<long>(w, std::max<long>(0, done)));
}
uint32_t sig_bar(const Snapshot& s, Rect r) { Hash h; h.u32(static_cast<uint32_t>(bar_fill(s, r.w - 2))); return h.h; }
void draw_bar(Paint& p, Rect r, const Snapshot& s) {
  p.g.fillRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, kBg);
  const int f = bar_fill(s, r.w - 2);
  if (f > 0) p.g.fillRect(r.x + 1, r.y + 1, f, r.h - 2, kCyan);
}
uint32_t sig_counts(const Snapshot& s) {
  Hash h; h.u32(s.scan.validated); h.u32(s.scan.candidates); h.u32(s.scan.new_this_session); h.u32(s.scan.passes); h.u32(s.scan.progress_valid); return h.h;
}
void draw_counts(Paint& p, Rect r, const Snapshot& s, Density d) {
  p.g.fillRect(r.x, r.y, r.w, r.h, kPanel);
  const int items = d == Density::discovery_first ? 4 : 3;
  const int cw = r.w / items;
  const char* names[4] = {"VERIFIED", "SYNC", "NEW", "PASSES"};
  const unsigned vals[4] = {s.scan.validated, s.scan.candidates, s.scan.new_this_session, s.scan.passes};
  const uint16_t cols[4] = {kGreen, kAmber, kCyan, kWhite};
  for (int i = 0; i < items; ++i) {
    const Rect cell{r.x + i * cw, r.y, cw, r.h};
    char v[12]; std::snprintf(v, sizeof(v), "%u", vals[i]);
    p.text(names[i], cell.x + 6, cell.y + 9, kMuted, 1, cell);
    p.text(v, cell.x + 6, cell.y + 24, vals[i] ? cols[i] : kMuted, 2, cell);
  }
}
Evidence ladder_level(const Snapshot& s) { return s.scan.stage == ScanStage::unavailable || s.scan.stage == ScanStage::ready ? Evidence::none : s.evidence; }
uint32_t sig_ladder(const Snapshot& s) {
  bool k; Hash h; h.u32(static_cast<uint32_t>(ladder_level(s))); h.u32(recorded_here(s, current_frequency(s, &k))); return h.h;
}
void draw_ladder(Paint& p, Rect r, const Snapshot& s) {
  p.g.fillRect(r.x, r.y, r.w, r.h, kPanel);
  static const char* names[5] = {"CANDIDATE", "SYNC", "INTEGRITY", "IDENTITY", "RECORDED"};
  const Evidence lvl = ladder_level(s);
  bool k; const bool rec = recorded_here(s, current_frequency(s, &k));
  const bool reached[5] = {lvl >= Evidence::candidate, lvl >= Evidence::sync, lvl >= Evidence::integrity_valid, lvl >= Evidence::identity_valid, rec};
  const uint16_t col[5] = {kAmber, kAmber, kCyan, kGreen, kGreen};
  const int cw = r.w / 5;
  for (int i = 0; i < 5; ++i) {
    const Rect c{r.x + i * cw + 2, r.y + 2, cw - 6, r.h - 4};
    p.g.fillRoundRect(c.x, c.y, c.w, c.h, 5, reached[i] ? kSelected : kPanel);
    p.g.drawRoundRect(c.x, c.y, c.w, c.h, 5, reached[i] ? col[i] : kEdge);
    p.text(names[i], c.x + 8, c.y + c.h / 2, reached[i] ? col[i] : kMuted, 1, c);
  }
}
struct Reason { Disabled d; const char* what; };
Reason pick_reason(const Snapshot& s, const UiState& u) {
  const ActionState start = action_state(ActionKind::scan, s, u);
  if (!start.enabled) return {start.reason, "START"};
  if (!running(s)) {
    const ActionState sv = action_state(ActionKind::save_channel, s, u);
    if (!sv.enabled && s.scan_hit_count) return {sv.reason, "SAVE"};
    return {Disabled::none, ""};
  }
  const ActionState sv = action_state(ActionKind::save_channel, s, u);
  if (!sv.enabled && s.scan_hit_count && sv.reason != Disabled::no_storage) return {sv.reason, "SAVE"};
  return {Disabled::none, ""};
}
uint32_t sig_reason(const Snapshot& s, const UiState& u) {
  const Reason r = pick_reason(s, u); Hash h; h.u32(static_cast<uint32_t>(r.d)); h.str(r.what); return h.h;
}
void draw_reason(Paint& p, Rect r, const Snapshot& s, const UiState& u) {
  p.g.fillRect(r.x, r.y, r.w, r.h, kPanel);
  const Reason why = pick_reason(s, u);
  char t[64];
  if (why.d != Disabled::none) { std::snprintf(t, sizeof(t), "%s: %s", why.what, disabled_text(why.d)); p.text(t, r.x + 6, r.y + r.h / 2, kAmber, 2, r); }
  else p.label("Sync is not identity: only a validated event counts", r.x + 6, r.y + r.h / 2, r);
}
struct BtnSpec { const char* label; bool enabled; bool selected; };
BtnSpec button_spec(int w, const Snapshot& s, const UiState& u) {
  const bool run = running(s);
  switch (w) {
    case w_start: return {run && s.scan.phase != ScanPhase::paused ? "STOP SCAN" : run ? "STOP SCAN" : "START SCAN",
                          action_state(ActionKind::scan, s, u).enabled, false};
    case w_hold: return {s.scan_on_hold ? "RESUME" : "HOLD", action_state(ActionKind::toggle_hold, s, u).enabled, s.scan_on_hold};
    case w_skip: return {"SKIP", action_state(ActionKind::skip_frequency, s, u).enabled, false};
    case w_save: return {"SAVE", action_state(ActionKind::save_channel, s, u).enabled, false};
    case w_prev: return {"< PLAN", action_state(ActionKind::plan_prev, s, u).enabled, false};
    default:     return {"PLAN >", action_state(ActionKind::plan_next, s, u).enabled, false};
  }
}
uint32_t sig_button(int w, const Snapshot& s, const UiState& u) {
  const BtnSpec b = button_spec(w, s, u); Hash h; h.str(b.label); h.u32(b.enabled); h.u32(b.selected); return h.h;
}
void draw_button(Paint& p, Rect r, int w, const Snapshot& s, const UiState& u) {
  const BtnSpec b = button_spec(w, s, u);
  p.button(b.label, r, b.enabled, b.selected);
}
uint32_t sig_row(size_t i, const Snapshot& s, const UiState& u) {
  Hash h; const bool sel = i < s.scan_hit_count && u.selected_scan_hit == i;
  h.u32(sel); h.u32(s.scan_hit_count == 0 && i == 0);
  if (i < s.scan_hit_count && i < kScanHits) {
    const ScanHit& x = s.scan_hits[i];
    h.u32(static_cast<uint32_t>(x.protocol)); h.u32(static_cast<uint32_t>(x.evidence)); h.u32(x.frequency_hz); h.u32(x.has_frequency);
    h.u32(x.colour_code | (x.colour_code_valid ? 0x100u : 0u) | (x.ran << 9) | (x.ran_valid ? 0x20000u : 0u));
    h.u32(x.seen_count); h.u32(x.is_new); h.u32(x.saved); h.str(x.last_time);
  }
  return h.h;
}
void draw_row(Paint& p, Rect r, size_t i, const Snapshot& s, const UiState& u) {
  const bool has = i < s.scan_hit_count && i < kScanHits;
  const bool sel = has && u.selected_scan_hit == i;
  p.g.fillRoundRect(r.x, r.y, r.w, r.h, 5, sel ? kSelected : kPanel);
  if (!has) { if (i == 0 && s.scan_hit_count == 0) p.label("NOTHING DISCOVERED YET", r.x + 10, r.y + r.h / 2, r); return; }
  const ScanHit& h = s.scan_hits[i];
  char ar[16]; area(ar, h.protocol, h.colour_code, h.colour_code_valid, h.ran, h.ran_valid);
  p.text(protocol_name(h.protocol), r.x + 10, r.y + 14, evidence_color(h.evidence), 2, r);
  if (ar[0]) p.text(ar, r.x + 150, r.y + 14, kWhite, 2, r);
  const char* ev = short_evidence(h.evidence);
  p.text(ev, r.x + r.w - 10 - static_cast<int>(std::strlen(ev)) * 12, r.y + 14, evidence_color(h.evidence), 2, r);
  char f[32], line[64];
  if (h.has_frequency) mhz(f, h.frequency_hz); else std::snprintf(f, sizeof(f), "FREQUENCY UNKNOWN");
  if (h.seen_count > 1) std::snprintf(line, sizeof(line), "%s  x%u%s%s", f, static_cast<unsigned>(h.seen_count), h.last_time[0] ? "  " : "", h.last_time);
  else std::snprintf(line, sizeof(line), "%s%s%s", f, h.last_time[0] ? "  " : "", h.last_time);
  p.label(line, r.x + 10, r.y + 36, r);
  if (h.is_new) p.text("NEW", r.x + r.w - 10 - 36, r.y + 36, kCyan, 2, r);
  else if (h.saved) p.text("SAVED", r.x + r.w - 10 - 60, r.y + 36, kGreen, 2, r);
}
uint32_t sig_detail(const Snapshot& s, const UiState& u) {
  Hash h; h.u32(u.selected_scan_hit); h.u32(u.selected_scan_hit < s.scan_hit_count);
  if (u.selected_scan_hit < s.scan_hit_count && u.selected_scan_hit < kScanHits) {
    const ScanHit& x = s.scan_hits[u.selected_scan_hit];
    h.u32(static_cast<uint32_t>(x.protocol)); h.u32(static_cast<uint32_t>(x.evidence)); h.u32(x.frequency_hz);
    h.u32(x.colour_code | (x.colour_code_valid ? 0x100u : 0u) | (x.ran << 9) | (x.ran_valid ? 0x20000u : 0u) | (x.timeslot << 18) | (x.timeslot_valid ? 0x1000000u : 0u));
    h.u32(x.source_id); h.u32(x.destination_id); h.u32(x.has_source | (x.has_destination << 1) | (x.group_call << 2));
    h.u32(static_cast<uint32_t>(x.nxdn_channel) | (x.nxdn_channel_valid ? 0x100u : 0u)); h.u32(x.seen_count); h.str(x.first_time); h.str(x.last_time);
  }
  return h.h;
}
void draw_detail(Paint& p, Rect r, const Snapshot& s, const UiState& u) {
  p.g.fillRect(r.x, r.y, r.w, r.h, kPanel);
  if (u.selected_scan_hit >= s.scan_hit_count || u.selected_scan_hit >= kScanHits) {
    p.label("SELECT A DISCOVERED SIGNAL", r.x + 8, r.y + 18, r); return;
  }
  const ScanHit& h = s.scan_hits[u.selected_scan_hit];
  const bool ident = h.evidence == Evidence::identity_valid;
  int y = r.y + 14;
  char t[64];
  p.text(evidence_name(h.evidence), r.x + 8, y, evidence_color(h.evidence), 2, r); y += 28;
  if (h.protocol == Protocol::dmr) {
    char ar[16]; area(ar, h.protocol, h.colour_code, h.colour_code_valid, h.ran, h.ran_valid);
    std::snprintf(t, sizeof(t), "%s  %s", ar[0] ? ar : "CC UNKNOWN", h.timeslot_valid && (h.timeslot == 1 || h.timeslot == 2) ? (h.timeslot == 1 ? "TS1" : "TS2") : "TS UNKNOWN");
    p.text(t, r.x + 8, y, kWhite, 2, r); y += 28;
    if (ident && h.has_source) std::snprintf(t, sizeof(t), "SRC %lu", static_cast<unsigned long>(h.source_id)); else std::snprintf(t, sizeof(t), "SRC UNKNOWN");
    p.text(t, r.x + 8, y, ident && h.has_source ? kGreen : kAmber, 2, r); y += 28;
    if (ident && h.has_destination) std::snprintf(t, sizeof(t), "%s %lu  %s", h.group_call ? "TG" : "DST", static_cast<unsigned long>(h.destination_id), h.group_call ? "GROUP" : "PRIVATE");
    else std::snprintf(t, sizeof(t), "DST UNKNOWN");
    p.text(t, r.x + 8, y, ident && h.has_destination ? kGreen : kAmber, 2, r); y += 28;
  } else if (h.protocol == Protocol::nxdn48 || h.protocol == Protocol::nxdn96) {
    char ar[16]; area(ar, h.protocol, h.colour_code, h.colour_code_valid, h.ran, h.ran_valid);
    std::snprintf(t, sizeof(t), "%s  %s", ar[0] ? ar : "RAN UNKNOWN", protocol_name(h.protocol));
    p.text(t, r.x + 8, y, kWhite, 2, r); y += 28;
    p.text(h.nxdn_channel_valid && h.evidence >= Evidence::integrity_valid ? nxdn_channel_name(h.nxdn_channel) : "CHANNEL TYPE UNKNOWN", r.x + 8, y, kWhite, 2, r); y += 28;
  } else {
    p.label("NO PROTOCOL FIELDS VERIFIED", r.x + 8, y, r); y += 28;
  }
  if (h.first_time[0] || h.last_time[0]) {
    std::snprintf(t, sizeof(t), "FIRST %s  LAST %s", h.first_time[0] ? h.first_time : "?", h.last_time[0] ? h.last_time : "?");
    p.label(t, r.x + 8, y, r); y += 26;
  }
  if (h.seen_count) { std::snprintf(t, sizeof(t), "OBSERVED %u TIMES", static_cast<unsigned>(h.seen_count)); p.label(t, r.x + 8, y, r); }
}

Rect rect_of(const Geometry& g, int w) {
  switch (w) {
    case w_status:return g.status; case w_freq:return g.freq; case w_plan:return g.plan; case w_bar:return g.bar;
    case w_counts:return g.counts; case w_ladder:return g.ladder; case w_reason:return g.reason;
    case w_start:return g.start; case w_hold:return g.hold; case w_skip:return g.skip; case w_save:return g.save;
    case w_prev:return g.plan_prev; case w_next:return g.plan_next; case w_detail:return g.detail;
    default:return g.row[w - w_row0];
  }
}
uint32_t signature(int w, const Snapshot& s, const UiState& u, const Geometry& g) {
  switch (w) {
    case w_status:return sig_status(s); case w_freq:return sig_freq(s); case w_plan:return sig_plan(s); case w_bar:return sig_bar(s, g.bar);
    case w_counts:return sig_counts(s); case w_ladder:return sig_ladder(s); case w_reason:return sig_reason(s, u);
    case w_start:case w_hold:case w_skip:case w_save:case w_prev:case w_next:return sig_button(w, s, u);
    case w_detail:return sig_detail(s, u);
    default:return sig_row(static_cast<size_t>(w - w_row0), s, u);
  }
}
void draw_widget(int w, Paint& p, const Geometry& g, const Snapshot& s, const UiState& u) {
  const Rect r = rect_of(g, w);
  switch (w) {
    case w_status:draw_status(p, r, s); break;
    case w_freq:draw_freq(p, r, s); break;
    case w_plan:draw_plan(p, r, s); break;
    case w_bar:draw_bar(p, r, s); break;
    case w_counts:draw_counts(p, r, s, u.density); break;
    case w_ladder:draw_ladder(p, r, s); break;
    case w_reason:draw_reason(p, r, s, u); break;
    case w_start:case w_hold:case w_skip:case w_save:case w_prev:case w_next:draw_button(p, r, w, s, u); break;
    case w_detail:draw_detail(p, r, s, u); break;
    default:draw_row(p, r, static_cast<size_t>(w - w_row0), s, u); break;
  }
}
}  // namespace

int ScanView::paint(lgfx::LovyanGFX& display, const Snapshot& s, const UiState& u, bool force) {
  Paint p{display};
  const Geometry g = scan_layout::geometry(u.density);
  int painted = 0;
  if (force) {   // static layout: cards, titles, the progress bar outline, the footer note. Painted once per entry.
    p.card(g.a); p.card(g.b);
    p.title("SCAN / DISCOVER", g.a.x + 18, g.a.y + 28, g.a);
    p.title("DISCOVERED SIGNALS", g.b.x + 18, g.list_title_y, g.b);
    p.title("SELECTED SIGNAL", g.b.x + 18, g.detail_title_y, g.b);
    p.label("Candidates are never identities.", g.b.x + 18, g.footer_y, g.b);
    display.drawRoundRect(g.bar.x, g.bar.y, g.bar.w, g.bar.h, 3, kEdge);
  }
  for (int w = 0; w < w_count; ++w) {
    const uint32_t sg = signature(w, s, u, g);
    if (!force && sg == sig_[w]) continue;
    sig_[w] = sg;
    draw_widget(w, p, g, s, u);
    ++painted;
  }
  return painted;
}
int ScanView::enter(lgfx::LovyanGFX& g, const Snapshot& s, const UiState& u) { entered_ = true; return paint(g, s, u, true); }
int ScanView::update(lgfx::LovyanGFX& g, const Snapshot& s, const UiState& u) { return entered_ ? paint(g, s, u, false) : enter(g, s, u); }

Action ScanView::touch(int32_t x, int32_t y, const Snapshot& s, const UiState& u) const {
  using layout::Rect;
  const Geometry g = scan_layout::geometry(u.density);
  struct B { Rect r; ActionKind k; };
  const B buttons[] = {{g.start, ActionKind::scan}, {g.hold, ActionKind::toggle_hold}, {g.skip, ActionKind::skip_frequency},
                       {g.save, ActionKind::save_channel}, {g.plan_prev, ActionKind::plan_prev}, {g.plan_next, ActionKind::plan_next}};
  for (const B& b : buttons)
    if (b.r.contains(x, y)) {
      if (!action_state(b.k, s, u).enabled) return {};      // a disabled control does nothing; the screen says why
      return {b.k, b.k == ActionKind::save_channel ? u.selected_scan_hit : static_cast<uint8_t>(0)};
    }
  const unsigned n = std::min<unsigned>(s.scan_hit_count, kScanHits);
  for (unsigned i = 0; i < n; ++i)
    if (g.row[i].contains(x, y)) return {ActionKind::select_scan_hit, static_cast<uint8_t>(i)};
  return {};
}

}  // namespace orcsdr::digital_voice_ui
