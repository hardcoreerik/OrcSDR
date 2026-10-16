// Host tests that run the real ACARS renderer and app glue against a recording M5GFX stand-in.
// They prove geometry and ownership rules (status-bar clearance, clipping, header font state, no idle
// redraw, ScreenController ownership). They do NOT prove how the display looks or how touch feels:
// that needs a physical Tab5.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <M5Unified.h>

#include "acars_app.hpp"
#include "acars_dashboard.hpp"
#include "acars_dashboard_layout.hpp"
#include "acars_dashboard_model.hpp"
#include "acars_dashboard_ui.hpp"
#include "screen_controller.hpp"

using namespace orcsdr::acars_ui;
using orcsdr::acars_ui::layout::Rect;
namespace screens = orcsdr::screens;
namespace dash = orcsdr::acars_dashboard;

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) \
  do { if (!(expression)) fail(#expression, __LINE__); } while (false)

int g_header_calls = 0;
const fonts::Font* g_font_at_header = reinterpret_cast<const fonts::Font*>(1);
size_t g_prims_at_header = 0;
void header_hook() {
  ++g_header_calls;
  g_font_at_header = host_display::font;           // the shared helpers assume the built-in font
  g_prims_at_header = host_display::prims.size();
}

orcsdr::acars::Message make_message(const char* tail, const char* text, bool corrected = false) {
  orcsdr::acars::Message m;
  std::snprintf(m.tail, sizeof(m.tail), "%s", tail);
  std::snprintf(m.flight, sizeof(m.flight), "XX0001");
  std::snprintf(m.label, sizeof(m.label), "H1");
  m.mode = '2'; m.block_id = '1'; m.frequency_hz = 131550000u; m.has_text = true; m.end_marker = 0x03;
  m.corrected = corrected; m.corrected_bits = corrected ? 1 : 0;
  m.text_length = static_cast<uint16_t>(std::strlen(text));
  std::memcpy(m.text, text, m.text_length);
  m.raw_bytes = m.text_length + 16;
  return m;
}

bool has_text(const std::string& needle) {
  for (const Prim& p : host_display::prims)
    if (p.kind == Prim::text && p.str.find(needle) != std::string::npos) return true;
  return false;
}

// Every painted primitive obeys the shared header rules and the clipping rules.
void check_prims(View view, bool detail) {
  Rect allowed[512];
  const size_t n = layout::collect_rects(view, detail, allowed, 512);
  CHECK(n < 512);
  const Rect header_rule{20, layout::kHeaderH - 1, 1240, 1};
  const Rect tabs_rule{0, layout::kTabsY, 1280, 1};
  for (const Prim& p : host_display::prims) {
    int x, y, w, h;
    if (!host_display::effective(p, &x, &y, &w, &h)) continue;
    const Rect eff{x, y, w, h};
    CHECK(x >= 0 && y >= 0 && x + w <= layout::kScreenW && y + h <= layout::kScreenH);
    if (eff.intersects(layout::kStatusBar)) {
      // Only a background clear (black) may touch the shared status-bar reserve; no text, card, line or shape.
      CHECK(p.kind == Prim::fill && p.color == TFT_BLACK);
    }
    if (p.kind == Prim::text) {
      CHECK(p.clipped);                            // all text is drawn inside a card's clip rectangle
      CHECK(!eff.intersects(layout::kStatusBar));
      CHECK(eff.y >= layout::kHeaderH);            // never inside the shared header
    }
    if (p.kind != Prim::fill) {
      CHECK(eff.y >= layout::kHeaderH - 1);
      if (eff.inside(header_rule) || eff.inside(tabs_rule)) continue;
      bool inside_some = false;
      for (size_t i = 0; i < n && !inside_some; ++i) inside_some = eff.inside(allowed[i]);
      if (!inside_some) {
        std::fprintf(stderr, "outside layout: kind=%d (%d,%d %dx%d) '%s'\n", p.kind, x, y, w, h, p.str.c_str());
      }
      CHECK(inside_some);
    }
  }
}

struct Rig {
  std::unique_ptr<Model> model{new Model()};
  std::unique_ptr<Controller> ui{new Controller()};
  Rig() {
    model->reset();
    ui->reset();
    dash::install(model.get(), ui.get(), header_hook);
  }
  void full_draw() {
    host_display::reset();
    g_header_calls = 0;
    dash::leave();
    dash::enter();
  }
};

void test_every_view_every_state_obeys_header_rules() {
  struct Scenario { bool demo; DemoScenario sc; };
  const Scenario scenarios[] = {{false, DemoScenario::mixed},        {true, DemoScenario::mixed},
                                {true, DemoScenario::full},          {true, DemoScenario::empty},
                                {true, DemoScenario::disconnected},  {true, DemoScenario::paused},
                                {true, DemoScenario::no_time}};
  for (const Scenario& sc : scenarios) {
    Rig rig;
    if (sc.demo) rig.model->set_demo(true, sc.sc);
    if (!sc.demo) {                                // a populated LIVE store, including a repaired block
      for (int i = 0; i < 20; ++i) {
        char tail[16];
        std::snprintf(tail, sizeof(tail), "N%03uLV", static_cast<unsigned>(i % 1000));
        rig.model->post_message(make_message(tail, "LIVE TEXT", i % 5 == 0), 1000 + i, i % 2 ? 1791900000u + i : 0);
        if (i % 10 == 9) rig.model->service();
      }
      rig.model->service();
      rig.model->post_receiver_state(ReceiverState::running);
      rig.model->service();
    }
    for (int v = 0; v < static_cast<int>(View::count); ++v) {
      rig.ui->select_view(static_cast<View>(v));
      rig.full_draw();
      CHECK(!host_display::prims.empty());
      CHECK(g_header_calls == 1);
      check_prims(static_cast<View>(v), false);
      // The aircraft view with a selected aircraft, and the message detail page from every view.
      if (v == static_cast<int>(View::aircraft) && rig.model->snapshot().aircraft_count) {
        orcsdr::acars_ui::layout::Rect r = layout::ac_row(0);
        (void)rig.ui->touch(r.x + 4, r.y + 4, *rig.model);
        rig.full_draw();
        check_prims(View::aircraft, false);
      }
      if (rig.model->snapshot().message_count) {
        CHECK(rig.ui->open_detail(rig.model->snapshot().messages[0].seq, *rig.model));
        rig.full_draw();
        check_prims(static_cast<View>(v), true);
        // Hidden raw text and plain format render too.
        rig.model->set_show_raw_text(false);
        rig.full_draw();
        check_prims(static_cast<View>(v), true);
        rig.model->set_show_raw_text(true);
        rig.model->set_text_format(TextFormat::plain);
        rig.ui->refresh_detail_text(*rig.model);
        rig.full_draw();
        check_prims(static_cast<View>(v), true);
        rig.model->set_text_format(TextFormat::escaped);
        rig.ui->close_detail();
      }
    }
  }
}

void test_shared_header_font_contract() {
  Rig rig;
  rig.model->set_demo(true, DemoScenario::mixed);
  for (int v = 0; v < static_cast<int>(View::count); ++v) {
    rig.ui->select_view(static_cast<View>(v));
    rig.full_draw();
    CHECK(g_header_calls == 1);
    CHECK(g_font_at_header == nullptr);            // built-in font state when the shared controls draw
    CHECK(!host_display::clip_on);                 // no clip leaks out of a draw
  }
}

void test_honest_text() {
  Rig rig;
  rig.full_draw();                                 // LIVE, nothing connected
  CHECK(has_text("RECEIVER FEED NOT CONNECTED"));
  CHECK(has_text("NO MESSAGES YET"));
  CHECK(has_text("NOT TUNED - NO RECEIVER RUNTIME"));
  CHECK(!has_text("DEMO DATA") && !has_text("SYNTHETIC"));
  rig.ui->select_view(View::activity);
  rig.full_draw();
  CHECK(has_text("NOT AVAILABLE"));                // signal diagnostics
  CHECK(has_text("NONE YET"));                     // no decoder report
  CHECK(!has_text("DEMO DATA") && !has_text("SYNTHETIC"));
  rig.ui->select_view(View::archive);
  rig.full_draw();
  CHECK(has_text("ARCHIVE NOT AVAILABLE"));
  CHECK(!has_text("SYNTHETIC"));
  rig.ui->select_view(View::channels);
  rig.full_draw();
  CHECK(has_text("SINGLE-CHANNEL DECODER"));
  CHECK(has_text("131.550 PRIMARY"));
  CHECK(has_text("REQUESTED"));
  CHECK(!has_text("RECEIVING NOW") && !has_text("TUNED TO"));
  rig.ui->select_view(View::aircraft);
  rig.full_draw();
  CHECK(has_text("NO AIRCRAFT YET"));
  CHECK(has_text("ADS-B CORRELATION: NOT AVAILABLE"));
  // No invented signal numbers anywhere, in any view or mode.
  rig.model->set_demo(true, DemoScenario::mixed);
  for (int v = 0; v < static_cast<int>(View::count); ++v) {
    rig.ui->select_view(static_cast<View>(v));
    rig.full_draw();
    CHECK(!has_text(" dB") && !has_text("dBFS") && !has_text("RSSI:") && !has_text("SNR:"));
    CHECK(has_text("DEMO DATA"));                  // DEMO is visible in the content area (context strip)
    CHECK(has_text("SYNTHETIC"));
  }
  rig.ui->select_view(View::live);
  rig.full_draw();
  CHECK(has_text("FIX"));                          // repaired block marked
  CHECK(has_text("CRC OK"));
  rig.model->set_highlight_corrected(false);
  rig.full_draw();
  CHECK(!has_text("FIX"));                         // indicator preference respected
}

void test_no_idle_redraw_and_throttle() {
  Rig rig;
  rig.full_draw();
  const size_t drawn = host_display::prims.size();
  for (uint32_t t = 1000; t < 100000; t += 1000) dash::update(t);
  CHECK(host_display::prims.size() == drawn);      // nothing changed: nothing painted
  // A data-only change repaints, but no more often than every 250 ms.
  rig.model->post_message(make_message("N1", "x"), 5, 0);
  rig.model->service();
  dash::update(200000);
  const size_t after_first = host_display::prims.size();
  CHECK(after_first > drawn);
  rig.model->post_message(make_message("N2", "y"), 6, 0);
  rig.model->service();
  dash::update(200100);                            // 100 ms later: throttled
  CHECK(host_display::prims.size() == after_first);
  dash::update(200300);
  CHECK(host_display::prims.size() > after_first);
  // A UI change (tab) is never throttled.
  const size_t before_tab = host_display::prims.size();
  const Rect t = layout::tab(3);
  (void)rig.ui->touch(t.x + 10, t.y + 10, *rig.model);
  dash::update(200310);
  CHECK(host_display::prims.size() > before_tab);
  CHECK(rig.ui->view() == View::activity);
}

void test_interaction_check_roundtrip() {
  Rig rig;
  rig.full_draw();
  CHECK(dash::interaction_check());
  CHECK(rig.ui->view() == View::live);
}


// ---------------------------------------------------------------- flicker rules

bool g_overlay = false;
bool overlay_probe() { return g_overlay; }

// With no pixel buffers (this stand-in models none) every change is a whole-frame change, which the dashboard draws
// straight onto the display (a whole-sprite push measured 0.8 s on the panel). What is checked here is that repaints
// happen exactly when something visible changed, never for header-only state, and that a frame starts with the
// full-area background fill. Partial pushes of changed tiles are covered by acars_dirty_rects_tests and the device.
void test_repaints_happen_only_for_visible_changes() {
  Rig rig;
  dash::set_overlay_probe(nullptr);
  rig.full_draw();
  CHECK(host_display::direct_ops > 0);
  // A data update repaints once.
  rig.model->post_message(make_message("N1", "x"), 5, 0);
  rig.model->service();
  host_display::direct_ops = 0;
  dash::update(500000);
  CHECK(host_display::direct_ops > 0);
  // Nothing changed: nothing is drawn, however often update() runs.
  host_display::direct_ops = 0;
  for (uint32_t t = 500100; t < 502000; t += 50) dash::update(t);
  CHECK(host_display::direct_ops == 0);
  // A tab change repaints, and the header is not redrawn (no header hook call).
  g_header_calls = 0;
  host_display::direct_ops = 0;
  const Rect t = layout::tab(2);
  (void)rig.ui->touch(t.x + 10, t.y + 10, *rig.model);
  dash::update(500010 + 2000);
  CHECK(host_display::direct_ops > 0 && g_header_calls == 0);
  // Battery / mute changes do not repaint anything.
  host_display::direct_ops = 0;
  rig.model->set_device_state(33, false);
  dash::update(600000);
  CHECK(host_display::direct_ops == 0);
  // Every frame is complete: it starts with a full-area background fill, never a partial wipe.
  rig.full_draw();
  bool saw_background = false;
  for (const Prim& p : host_display::prims)
    if (p.kind == Prim::fill && p.x == 0 && p.y == 108 && p.w == 1280 && p.h == 612) saw_background = true;
  CHECK(saw_background);
}

void test_overlay_defers_repaint() {
  Rig rig;
  dash::set_overlay_probe(overlay_probe);
  rig.full_draw();
  g_overlay = true;                                // the shared volume tray is open over the content
  rig.model->post_message(make_message("N9", "tray"), 5, 0);
  rig.model->service();
  host_display::direct_ops = 0;
  host_display::pushes = 0;
  dash::update(700000);
  CHECK(host_display::pushes == 0 && host_display::direct_ops == 0);   // tray not erased
  g_overlay = false;
  dash::update(700100);
  CHECK(host_display::direct_ops > 0);             // the pending change is painted once the tray closes
  dash::set_overlay_probe(nullptr);
}

void test_fallback_without_sprite_still_draws_correctly() {
  Rig rig;
  dash::set_overlay_probe(nullptr);
  host_display::fail_sprite_alloc = true;
  rig.model->set_demo(true, DemoScenario::mixed);
  for (int v = 0; v < static_cast<int>(View::count); ++v) {
    rig.ui->select_view(static_cast<View>(v));
    rig.full_draw();
    CHECK(host_display::pushes == 0 && host_display::direct_ops > 0);
    check_prims(static_cast<View>(v), false);      // header rules hold when drawing straight to the display
  }
  host_display::fail_sprite_alloc = false;
}

// ---------------------------------------------------------------- app lifecycle and ScreenController ownership

void test_app_lifecycle() {
  using namespace orcsdr;
  dash::leave();                                   // earlier tests drove the dashboard directly
  host_display::reset();
  g_header_calls = 0;
  acars_app::Hooks hooks;
  hooks.header_controls = header_hook;
  hooks.sound_enabled = [] { return true; };
  hooks.overlay_open = [] { return false; };
  static uint32_t requested = 0;
  hooks.request_tune = [](uint32_t hz) { requested = hz; };
  acars_app::install(hooks);
  CHECK(acars_app::model() == nullptr);            // allocated on first open
  CHECK(!acars_app::active());
  acars_app::tick();                               // harmless while closed
  acars_app::touch(100, 400);

  host_millis = 5000;
  acars_app::open();
  CHECK(acars_app::active() && acars_app::model() != nullptr);
  CHECK(screens::owns(screens::Id::acars));
  CHECK(g_header_calls == 1 && !host_display::prims.empty());
  Model* model = acars_app::model();
  CHECK(model->snapshot().battery_percent == 77);

  // A runtime posts through the producer seam; the UI shows it after a tick.
  const size_t before = host_display::prims.size();
  CHECK(model->post_message(make_message(".N55AB", "hello"), 5100, 0));
  host_millis = 6000;
  acars_app::tick();
  CHECK(model->snapshot().message_count == 1 && host_display::prims.size() > before);
  CHECK(has_text("N55AB"));

  // Touch routing: a tab, then a channel choice that reaches the (future) tune hook as a request only.
  const Rect ch = layout::tab(2);
  acars_app::touch(ch.x + 10, ch.y + 10);
  const Rect row = layout::ch_row(1);
  acars_app::touch(row.x + 10, row.y + 10);
  CHECK(requested == 130025000u);
  CHECK(model->snapshot().tune == TuneStatus::unavailable);
  const Rect live_tab = layout::tab(0);
  acars_app::touch(live_tab.x + 10, live_tab.y + 10);
  // Taps in the shared header region never reach the dashboard.
  const size_t prims = host_display::prims.size();
  acars_app::touch(500, 30);
  acars_app::touch(1100, 30);
  CHECK(host_display::prims.size() == prims);

  // Settings (Mute / Visualizer / Settings buttons belong to the shared header): ACARS must not draw while it
  // is not the owner, and it must come back on request.
  screens::begin_transition(screens::Id::settings, 7000, true);
  screens::finish_transition();
  CHECK(!screens::owns(screens::Id::acars));
  model->post_message(make_message("N66CD", "later"), 7100, 0);
  const size_t quiet = host_display::prims.size();
  acars_app::tick();
  acars_app::draw(false);
  CHECK(host_display::prims.size() == quiet);      // not the active screen: no drawing at all
  CHECK(model->snapshot().message_count == 2);     // but data still drains
  const screens::Id back = screens::close_settings(7200);
  screens::finish_transition();
  CHECK(back == screens::Id::acars);
  acars_app::draw(true);                           // navigation_restore_screen path
  CHECK(host_display::prims.size() > quiet && has_text("N66CD"));

  // Visualizer round trip restores through the same static draw.
  screens::begin_transition(screens::Id::visualizer, 8000);
  screens::finish_transition();
  const size_t viz = host_display::prims.size();
  acars_app::tick();
  CHECK(host_display::prims.size() == viz);
  screens::begin_transition(screens::Id::acars, 8100);
  screens::finish_transition();
  acars_app::draw(true);
  CHECK(host_display::prims.size() > viz);

  // Demo mode is left behind when the screen closes (Home).
  const Rect setup = layout::tab(5);
  acars_app::touch(setup.x + 10, setup.y + 10);
  const Rect demo = layout::setup_item(1, 1);
  acars_app::touch(demo.x + 10, demo.y + 10);
  CHECK(model->demo());
  acars_app::close();
  CHECK(!acars_app::active() && !model->demo());
  screens::begin_transition(screens::Id::home, 9000);
  screens::finish_transition();
  const size_t closed = host_display::prims.size();
  acars_app::tick();
  acars_app::touch(100, 400);
  CHECK(host_display::prims.size() == closed);     // closed: inert
  acars_app::close();                              // idempotent

  // Re-opening starts LIVE with preferences intact and a fresh controller.
  acars_app::open();
  CHECK(acars_app::active() && !model->demo() && model->snapshot().source == SourceKind::live);
  CHECK(has_text("LIVE"));
  acars_app::close();
}

}  // namespace

int main() {
  CHECK(screens::self_check());
  test_every_view_every_state_obeys_header_rules();
  test_shared_header_font_contract();
  test_honest_text();
  test_no_idle_redraw_and_throttle();
  test_interaction_check_roundtrip();
  test_repaints_happen_only_for_visible_changes();
  test_overlay_defers_repaint();
  test_fallback_without_sprite_still_draws_correctly();
  test_app_lifecycle();
  std::puts("acars_dashboard_render_tests: all passed");
  return 0;
}
