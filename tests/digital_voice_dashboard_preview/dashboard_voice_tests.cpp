// LIVE page voice-audio panel: every state renders its name, audio-only changes repaint ONE rectangle, idle repaints nothing,
// controls honour availability, protected/unknown voice never shows speech facts. Recording stand-in display (not pixels).
#include "digital_voice_dashboard.hpp"
#include <cstdio>
#include <cstring>
using namespace orcsdr::digital_voice_ui;
using orcsdr::digital_voice_ui::layout::Rect;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
static bool inside(host_stub::R e, Rect r) { return e.w == 0 || e.h == 0 || (e.x >= r.x && e.y >= r.y && e.x + e.w <= r.x + r.w && e.y + e.h <= r.y + r.h); }
static bool all_inside(Rect r) {
  for (const auto& p : host_stub::calls) {
    if (p.kind == host_stub::Prim::line) continue;
    if (!inside(host_stub::eff(p), r)) { const auto e = host_stub::eff(p); std::printf("   outside: kind=%d (%d,%d,%d,%d) '%s'\n", p.kind, e.x, e.y, e.w, e.h, p.str.c_str()); return false; }
  }
  return true;
}
int main() {
  static lgfx::LovyanGFX display;
  Dashboard dash;
  UiState ui; ui.view = View::live;
  Snapshot s; s.revision = 1; s.audio_revision = 1;
  s.receiver = Receiver::receiving; s.protocol = Protocol::dmr; s.evidence = Evidence::identity_valid;
  s.has_source = s.has_destination = true; s.source_id = 4106011; s.destination_id = 1000; s.group_call = true;
  s.voice.available = true; s.voice.phase = VoicePhase::listening;
  const struct { Density d; Rect vr; } kinds[] = {{Density::discovery_first, {32, 530, 778, 100}}, {Density::analysis_first, {654, 488, 594, 100}}};
  for (const auto& k : kinds) {
    ui.density = k.d;
    s.voice = VoiceAudioView(); s.voice.available = true; s.voice.phase = VoicePhase::listening; ++s.audio_revision; ++s.revision;
    dash = Dashboard();
    host_stub::reset();
    CHECK(dash.present(display, s, ui, true, false));
    CHECK(host_stub::contains("LISTENING") && host_stub::contains("AUTO") && !host_stub::out_of_bounds);
    // idle: nothing drawn
    host_stub::reset();
    CHECK(dash.present(display, s, ui, true, false));
    CHECK(host_stub::calls.empty());
    const VoicePhase phases[] = {VoicePhase::acquiring, VoicePhase::buffering, VoicePhase::decoding, VoicePhase::silence_frame,
                                 VoicePhase::degraded, VoicePhase::call_ended, VoicePhase::protected_voice, VoicePhase::privacy_unknown,
                                 VoicePhase::muted, VoicePhase::unavailable};
    for (VoicePhase ph : phases) {
      s.voice.phase = ph; s.voice.active_slot = (ph == VoicePhase::decoding) ? 2 : 0; s.voice.level = 5; ++s.audio_revision;
      host_stub::reset();
      CHECK(dash.present(display, s, ui, true, false));
      CHECK(host_stub::contains(voice_phase_name(ph)));
      CHECK(all_inside(k.vr));                       // ONLY the voice panel was touched
      CHECK(host_stub::full_clear_count == 0 && !host_stub::out_of_bounds);
      CHECK(!host_stub::contains("SOURCE"));        // the rest of the page was not repainted
    }
    s.voice.phase = VoicePhase::decoding; s.voice.active_slot = 2; s.voice.source_id = 4106011; s.voice.destination_id = 1000; s.voice.group_call = true;
    ++s.audio_revision;
    host_stub::reset(); CHECK(dash.present(display, s, ui, true, false));
    CHECK(host_stub::contains("SLOT 2") && host_stub::contains("4106011>1000") && host_stub::contains("GROUP"));
    s.voice.phase = VoicePhase::protected_voice; s.voice.privacy_ts2 = 2; s.voice.active_slot = 0; ++s.audio_revision;
    host_stub::reset(); CHECK(dash.present(display, s, ui, true, false));
    CHECK(host_stub::contains("PROTECTED VOICE") && host_stub::contains("AUDIO WITHHELD") && !host_stub::contains("4106011"));
    // controls: four listening buttons, only while the audio service is available
    for (int i = 0; i < 4; ++i) {
      const auto a = dash.handle_touch(k.vr.x + k.vr.w - 400 + i * 100 + 48, k.vr.y + 28, s, ui);
      CHECK(a.kind == ActionKind::set_listen && a.value == i);
    }
    s.voice.available = false;
    CHECK(dash.handle_touch(k.vr.x + k.vr.w - 400 + 48, k.vr.y + 28, s, ui).kind == ActionKind::none);
    s.voice.available = true;
    // a full-page revision repaints the panel too, inside the page
    ++s.revision; s.voice.phase = VoicePhase::decoding; ++s.audio_revision;
    host_stub::reset(); CHECK(dash.present(display, s, ui, true, false));
    CHECK(host_stub::contains("DECODING SPEECH") && host_stub::contains("VERIFIED IDENTITY") == false);
    CHECK(host_stub::full_clear_count == 0);
    // LIVE with an unknown time slot must not claim one anywhere on the page
    s.voice.active_slot = 0; s.voice.privacy_ts1 = s.voice.privacy_ts2 = 0; s.timeslot_valid = false;
    ++s.revision; ++s.audio_revision;
    host_stub::reset(); CHECK(dash.present(display, s, ui, true, false));
    CHECK(!host_stub::contains("TS1") && !host_stub::contains("TS2"));
  }
  std::printf("dashboard_voice_tests: %s\n", g_fail ? "FAILED" : "ok");
  return g_fail ? 1 : 0;
}
