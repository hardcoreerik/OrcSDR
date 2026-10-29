// Compiles the actual Dead Air M5GFX renderer with the existing ACARS
// recording stand-in; no hardware or simulated live RF data.
#include <M5Unified.h>
#include "dead_air_dashboard.hpp"
#include "dead_air_envhistory.hpp"
#include "dead_air_fx.hpp"
#include "screen_controller.hpp"
#include <cassert>
#include <cmath>
#include <string>
#include <cstdio>
using namespace orcsdr;
namespace da = orcsdr::dead_air;
namespace screen = orcsdr::screens;

static bool displayed(const char* text) {
  for (const Prim& p : host_display::prims)
    if (p.kind == Prim::text && p.str.find(text) != std::string::npos) return true;
  return false;
}
static void validate_bounds() {
  for (const Prim& p : host_display::prims) {
    int x, y, w, h;
    if (!host_display::effective(p, &x, &y, &w, &h)) continue;
    assert(x >= 0 && y >= 0 && x + w <= 1280 && y + h <= 720);
  }
}
int main() {
  da::EventModel model;
  model.reset();
  da::attach_events(&model);
  da::Snapshot snap{};
  snap.revision = 1;
  snap.battery_percent = -1;
  // Never admit cached values from an absent ENV III or detached receiver.
  snap.temperature_c = {99.0f, 0, 0, true, true, true};
  snap.environment_sensor_attached = false;
  snap.rf_level_dbfs = {-14.0f, 0, 0, true, true, true};
  snap.receiver_ready = false;
  screen::begin_transition(screen::Id::dead_air, 100);
  da::enter(snap);
  screen::finish_transition();
  assert(da::active() && da::page() == da::Page::overview);
  assert(displayed("MODULE NOT CONNECTED"));
  assert(!displayed("99.0") && !displayed("-14.0"));
  assert(displayed("RECEIVER UNAVAILABLE"));
  validate_bounds();
  // Overview RF strip: no receiver says so; a measured history draws a line that breaks at gaps, dashed baseline and
  // limit lines only once a baseline exists, and a tick at each event. Repeated samples never clear the whole screen.
  assert(displayed("NO LIVE RF LEVEL") && displayed("RF 60 s") && displayed("STANDARD"));
  {
    const auto in_strip = [](const Prim& p) { return p.x >= 670 && p.x < 1236 && p.y >= 236 && p.y < 324; };
    da::Snapshot ov = snap;
    ov.receiver_ready = true;
    ov.receiver_settled = true;
    ov.frequency_hz = 98700000;
    ov.rf_level_dbfs = {-31.0f, -40.0f, 0, true, true, true};
    ov.rf_history_count = 20;
    for (int i = 0; i < 20; ++i) ov.rf_history[i] = (i == 7 || i == 8) ? da::kHistGap : static_cast<int16_t>(-350 + i * 3);
    ov.rf_history_event[12] = static_cast<uint8_t>(da::HistMark::rf_rise);
    ov.rf_history_seq = 20;
    ov.revision = 2;
    host_display::reset();
    da::update(ov);
    assert(displayed("LIMIT") && !displayed("NO LIVE RF LEVEL") && !displayed("BASELINE WARMING"));
    validate_bounds();
    bool tick = false, dashes = false;
    for (const Prim& p : host_display::prims) {
      if (!in_strip(p) || p.kind != Prim::line) continue;
      assert(p.w < 8);                          // no segment spans the two-sample gap
      if (p.w == 1 && p.h == 82) tick = true;   // the event tick
      if (p.w == 5 && p.h == 1) dashes = true;  // a dash of the baseline or limit line
    }
    assert(tick && dashes);
    for (int step = 0; step < 5; ++step) {      // repeated samples: values move, the whole screen is never cleared
      ov.rf_history_count = static_cast<uint8_t>(21 + step);
      ov.rf_history[20 + step] = static_cast<int16_t>(-300 - step * 20);
      ov.rf_history_seq = 21 + step;
      ov.revision = 3 + step;
      host_display::reset();
      da::update(ov);
      validate_bounds();
      for (const Prim& p : host_display::prims) assert(!(p.kind == Prim::fill && p.w >= 1280 && p.h >= 700));
    }
    ov.rf_level_dbfs.baseline_valid = false;    // baseline lost (retune): no lines, and the strip says why
    ov.revision = 20;
    host_display::reset();
    da::update(ov);
    assert(displayed("BASELINE WARMING"));
    ov.rf_history_count = 0;
    ov.revision = 21;
    host_display::reset();
    da::update(ov);
    assert(displayed("WAITING FOR A SETTLED LEVEL"));
    validate_bounds();
    snap.revision = 22;                         // back to the no-receiver snapshot the rest of this test expects
    da::update(snap);
  }
  // Environment readings that change: the displayed value follows, and the whole screen is never cleared.
  {
    da::Snapshot env = snap;
    env.environment_sensor_attached = true;
    env.temperature_c = {21.0f, 0, 0, true, true, true};
    env.humidity_rh = {40.0f, 0, 0, true, true, true};
    env.pressure_hpa = {1013.0f, 0, 0, true, true, true};
    env.revision = 30;
    host_display::reset();
    da::update(env);
    assert(displayed("MODULE DETECTED") && displayed("21.0") && displayed("1013.0"));
    for (int step = 1; step <= 4; ++step) {
      env.temperature_c.value = 21.0f + step * 1.5f;
      env.humidity_rh.value = 40.0f + step * 2.0f;
      env.pressure_hpa.value = 1013.0f - step * 0.5f;
      env.revision = 30 + step;
      host_display::reset();
      da::update(env);
      char want[16];
      std::snprintf(want, sizeof(want), "%.1f", static_cast<double>(env.temperature_c.value));
      assert(displayed(want));
      char want_rh[16];
      std::snprintf(want_rh, sizeof(want_rh), "%.1f", static_cast<double>(env.humidity_rh.value));
      assert(displayed(want_rh));
      char want_hpa[16];
      std::snprintf(want_hpa, sizeof(want_hpa), "%.1f", static_cast<double>(env.pressure_hpa.value));
      assert(displayed(want_hpa));
      validate_bounds();
      for (const Prim& p : host_display::prims) assert(!(p.kind == Prim::fill && p.w >= 1280 && p.h >= 700));
    }
    snap.revision = 40;                         // back to the absent-sensor snapshot
    da::update(snap);
  }
  // Environment trend charts: the history is drawn as lines that break at gaps, the window buttons and the tap cursor
  // repaint only what changed, and the whole screen is never cleared.
  {
    static da::EnvHistory hist;
    hist.reset(0);
    for (uint32_t m = 0; m < 90; ++m) {
      hist.add(0, 20.0f + 0.02f * static_cast<float>(m), m * 60000u + 10u);
      hist.add(1, 50.0f + static_cast<float>(m % 7), m * 60000u + 10u);
      if (m < 40 || m > 50) hist.add(2, 1000.0f + 0.1f * static_cast<float>(m), m * 60000u + 10u);  // minutes 41..49 are a gap
    }
    da::Snapshot ch = snap;
    ch.environment_sensor_attached = true;
    ch.temperature_c = {21.8f, 0, 0, true, true, true};
    ch.humidity_rh = {53.0f, 0, 0, true, true, true};
    ch.pressure_hpa = {1008.9f, 0, 0, true, true, true};
    ch.env_history = &hist;
    ch.env_history_seq = hist.seq();
    ch.revision = 50;
    da::update(ch);
    da::handle_touch(20 + 3 * 210 + 40, 673, 5000);  // Environment tab
    assert(da::page() == da::Page::environment);
    const auto in_chart = [](const Prim& p, int ch_index) {
      return p.x >= 666 && p.x < 1238 && p.y >= 238 + ch_index * 122 && p.y < 238 + ch_index * 122 + 88;
    };
    const auto no_full_clear = [] {
      for (const Prim& p : host_display::prims) assert(!(p.kind == Prim::fill && p.w >= 1280 && p.h >= 700));
    };
    ch.revision = 51;
    host_display::reset();
    da::update(ch);
    assert(displayed("TREND") && displayed("TEMPERATURE (C)") && displayed("HUMIDITY (%RH)") && displayed("PRESSURE (hPa)"));
    assert(displayed("-6 h") && displayed("NOW"));  // 90 minutes recorded: the 6 H window is chosen
    int lines[3] = {0, 0, 0};
    for (const Prim& p : host_display::prims)
      for (int i = 0; i < 3; ++i)
        if (p.kind == Prim::line && in_chart(p, i)) ++lines[i];
    assert(lines[0] > 0 && lines[1] > 0 && lines[2] > 0);
    validate_bounds();
    no_full_clear();
    for (int step = 0; step < 4; ++step) {        // a new minute average arrives: values move, nothing is cleared
      hist.add(0, 22.0f + static_cast<float>(step), (90u + static_cast<uint32_t>(step)) * 60000u + 10u);
      ch.env_history_seq = hist.seq();
      ch.revision = 52 + step;
      host_display::reset();
      da::update(ch);
      validate_bounds();
      no_full_clear();
    }
    da::handle_touch(1010 + 20, 154 + 20, 5010);  // 1 H
    assert(displayed("-60 m") && displayed("-30 m"));
    validate_bounds();
    no_full_clear();
    host_display::reset();
    da::handle_touch(900, 260, 5020);             // tap a chart: a cursor and its moment
    assert(displayed("CURSOR") && displayed("AT CURSOR"));
    validate_bounds();
    no_full_clear();
    host_display::reset();
    da::handle_touch(905, 400, 5030);             // tap near the cursor: it clears
    assert(displayed("Tap a chart") && !displayed("AT CURSOR"));
    da::handle_touch(1086 + 20, 154 + 20, 5040);  // 6 H
    assert(displayed("-3 h"));
    ch.env_history = nullptr;                     // no history: the charts say so and nothing crashes
    ch.revision = 60;
    host_display::reset();
    da::update(ch);
    assert(displayed("NO READINGS IN THIS WINDOW YET"));
    validate_bounds();
    da::handle_touch(40, 673, 5050);              // back to Overview for the rest
    snap.revision = 70;
    da::update(snap);
  }
  // The orb: a dim still ring with nothing measured, a breathing glow when a level is measured, a burst when an event
  // is logged. It paints a big orb on the Overview and a small one in the header, in bounds, and is throttled.
  {
    const auto in = [](const Prim& p, int x0, int y0, int x1, int y1) { return p.x >= x0 && p.y >= y0 && p.x < x1 && p.y < y1; };
    da::fx_idle(false, 0.0f);
    host_display::reset();
    assert(da::draw_fx(30000));                 // the body was just repainted, so the orb is drawn
    bool beacon = false, big = false;
    for (const Prim& p : host_display::prims) {
      beacon = beacon || in(p, 800, 13, 852, 65);
      big = big || in(p, 500, 138, 620, 258);
    }
    assert(beacon && big);
    validate_bounds();
    assert(!da::draw_fx(30500));                // still and unchanged: nothing is repainted
    da::fx_idle(true, 0.7f);
    host_display::reset();
    assert(da::draw_fx(30800));                 // a measurement starts: the glow is drawn
    assert(!da::draw_fx(30900));                // idle glow is limited to a few frames a second
    da::Event hit_event{};
    hit_event.sequence = 5; hit_event.kind = da::Kind::rf_rise; hit_event.value = -30.0f; hit_event.reference = -40.0f;
    da::fx_event(hit_event, 31000);
    host_display::reset();
    assert(da::draw_fx(31020));
    const size_t burst_prims = host_display::prims.size();
    assert(burst_prims > 0);
    validate_bounds();
    assert(!da::draw_fx(31040));                // a burst runs at about 15 frames a second
    assert(da::draw_fx(31100));
    assert(da::draw_fx(31000 + da::Fx::kBurstMs + 300));  // the burst is over; the idle glow carries on
    // Overview extras: SESSION opens the session card, which shows only counts of what was measured and logged.
    snap.summary.elapsed_s = 3725; snap.summary.measured_s = 3000;
    snap.summary.events_rf = 3; snap.summary.events_audio = 2; snap.summary.events_marks = 1;
    snap.summary.have_level = true; snap.summary.level_max_dbfs = -12.3f; snap.summary.level_min_dbfs = -71.0f;
    snap.summary.longest_quiet_s = 1500; snap.summary.channels = 4; snap.summary.sweeps = 1; snap.summary.clips = 2;
    snap.revision = 23;
    da::update(snap);
    assert(da::overlay() == da::Overlay::none);
    assert(da::handle_touch(498 + 20, 305 + 20, 3000).kind == da::ActionKind::none);
    assert(da::overlay() == da::Overlay::session);
    assert(displayed("SESSION CARD") && displayed("1h 02m") && displayed("3 RF   2 AUDIO   1 MARKS"));
    assert(displayed("-12.3 dBFS") && displayed("-71.0 dBFS") && displayed("25m 00s") && displayed("4.8 findings per hour"));
    assert(displayed("No score, no interpretation."));
    validate_bounds();
    snap.summary.elapsed_s = 120; snap.revision = 24;   // a short session gives no rate
    host_display::reset();
    da::update(snap);
    assert(displayed("after 10 minutes") && !displayed("findings per hour"));
    host_display::reset();
    assert(!displayed("INVESTIGATION"));
    assert(da::handle_touch(1070 + 20, 128 + 20, 3010).kind == da::ActionKind::none);   // CLOSE
    assert(da::overlay() == da::Overlay::none && displayed("INVESTIGATION"));
    da::handle_touch(498 + 20, 305 + 20, 3020);          // reopen, then a tab tap closes it too
    assert(da::overlay() == da::Overlay::session);
    da::handle_touch(20 + 1 * 210 + 40, 673, 3030);
    assert(da::overlay() == da::Overlay::none && da::page() == da::Page::spectrum);
    da::handle_touch(40, 673, 3040);                    // back to Overview for the rest
    assert(da::page() == da::Page::overview);
    // On another tab the big orb is not drawn (it would paint over that page); the header one still is.
    da::handle_touch(20 + 1 * 210 + 40, 673, 3050);
    da::fx_idle(true, 0.2f);
    host_display::reset();
    assert(da::draw_fx(40000));
    bool big_on_spectrum = false, beacon_on_spectrum = false;
    for (const Prim& p : host_display::prims) {
      big_on_spectrum = big_on_spectrum || in(p, 500, 138, 620, 258);
      beacon_on_spectrum = beacon_on_spectrum || in(p, 800, 13, 852, 65);
    }
    assert(!big_on_spectrum && beacon_on_spectrum);
    da::handle_touch(40, 673, 3060);
    snap.revision = 25;
    da::update(snap);
  }
  for (int i = 0; i < 6; ++i) {
    host_display::reset();
    const int x = 20 + i * 210 + 40;
    auto action = da::handle_touch(x, 673, 1000 + i);
    assert(action.kind == da::ActionKind::none);
    assert(da::page() == static_cast<da::Page>(i));
    // An already active tab must not cause a redundant screen repaint.
    if (i == 0) assert(host_display::prims.empty());
    else assert(!host_display::prims.empty());
    validate_bounds();
  }
  // Sensitivity buttons/marker are real touch actions only on Events.
  da::handle_touch(20 + 4*210 + 40, 673, 2000);
  assert(da::page() == da::Page::events);
  da::handle_touch(1140, 400, 2100);
  assert(model.sensitivity() == da::Sensitivity::sensitive);
  da::handle_touch(900, 470, 2200);
  assert(model.count() == 1 && model.newest(0)->source == da::Source::manual);
  assert(displayed("MARK no receiver"));
  // Mode and step buttons act only while a receiver is actually present, and only on the Spectrum tab.
  da::handle_touch(20 + 1 * 210 + 40, 673, 2300);
  assert(da::page() == da::Page::spectrum);
  assert(da::handle_touch(145, 370, 2310).kind == da::ActionKind::none);
  snap.receiver_ready = true;
  snap.receiver_settled = true;
  snap.frequency_hz = 98700000;
  std::snprintf(snap.demod, sizeof(snap.demod), "WFM");
  snap.revision = 5;
  da::update(snap);
  auto mode = da::handle_touch(145, 370, 2320);
  assert(mode.kind == da::ActionKind::set_demod && mode.value == 4 && std::string(da::kModeNames[4]) == "LSB");
  mode = da::handle_touch(40 + 95 + 10, 290 + 20, 2330);
  assert(mode.kind == da::ActionKind::set_demod && mode.value == 1);
  assert(da::handle_touch(200, 470, 2340).kind == da::ActionKind::tune_up);
  assert(da::handle_touch(60, 470, 2350).kind == da::ActionKind::tune_down);
  // A measured spectrum draws the ectoplasm trace, waterfall and legend inside the Spectrum panel; a retune, a new
  // frame and a loss of the receiver all stay in bounds and never draw a trace without valid data.
  for (size_t i = 0; i < da::kBins; ++i) snap.spectrum_dbfs[i] = -100.0f + static_cast<float>(i % 40);
  snap.spectrum_valid = true;
  snap.spectrum_seq = 1;
  snap.revision = 20;
  host_display::reset();
  da::update(snap);
  assert(displayed("-110") && displayed("-10 dBFS") && !displayed("NO LIVE SPECTRUM"));
  validate_bounds();
  snap.spectrum_seq = 2;
  snap.frequency_hz = 98900000;
  snap.revision = 21;
  da::update(snap);
  validate_bounds();
  snap.spectrum_valid = false;
  snap.revision = 22;
  host_display::reset();
  da::update(snap);
  assert(displayed("SETTLING"));
  validate_bounds();
  snap.spectrum_valid = true;
  snap.revision = 23;
  da::update(snap);
  // Live frames are drawn straight onto the panel on the Spectrum tab only, and stay inside the screen.
  float pts[da::kTracePoints];
  for (size_t i = 0; i < da::kTracePoints; ++i) pts[i] = -95.0f + static_cast<float>(i % 60);
  host_display::reset();
  assert(da::draw_spectrum_frame(pts, da::kTracePoints, 1, 5000));
  assert(!host_display::prims.empty());
  validate_bounds();
  assert(da::draw_spectrum_frame(pts, da::kTracePoints, 1, 5033));
  assert(da::draw_spectrum_frame(pts, da::kTracePoints, 2, 5200));  // retune: marker row, still bounded
  validate_bounds();
  assert(!da::draw_spectrum_frame(nullptr, 0, 1, 5300));
  assert(da::paint_stats().frames == 3);
  // Audio tab: static frame plus a directly drawn envelope; nothing is drawn off the tab, and "no audio" is explicit.
  assert(!da::draw_audio_frame({}, 7000));  // Spectrum tab is showing
  da::handle_touch(20 + 2 * 210 + 40, 673, 6990);
  assert(da::page() == da::Page::audio);
  assert(displayed("PEAK ENVELOPE") && displayed("RMS LEVEL") && displayed("Level meter only"));
  host_display::reset();
  assert(da::draw_audio_frame({}, 7000) && displayed("NO DEMODULATED AUDIO"));
  validate_bounds();
  float env[da::kBins];
  for (size_t i = 0; i < da::kBins; ++i) env[i] = static_cast<float>(i % 10) / 10.0f;
  da::AudioView view{};
  view.envelope = env; view.count = da::kBins; view.flowing = true;
  view.rms_dbfs = -31.2f; view.peak_dbfs = -12.5f; view.baseline_ready = false;
  host_display::reset();
  assert(da::draw_audio_frame(view, 7100));
  assert(displayed("-31.2 dBFS") && displayed("-12.5 dBFS") && displayed("WARMING UP") && displayed("AUDIO: FLOWING"));
  assert(!displayed("NO DEMODULATED AUDIO"));
  validate_bounds();
  view.baseline_ready = true; view.baseline_dbfs = -33.0f;
  float fresh[2] = {0.4f, 0.9f};
  view.fresh = fresh; view.fresh_count = 2;
  host_display::reset();
  assert(da::draw_audio_frame(view, 7500) && displayed("-33.0 dBFS"));  // numbers refresh at 4 Hz
  validate_bounds();
  assert(da::draw_audio_frame(view, 7550));  // between text refreshes: only the scroll and new bars
  validate_bounds();
  da::handle_touch(20 + 1 * 210 + 40, 673, 7300);  // back to Spectrum for the sweep checks below
  assert(da::page() == da::Page::spectrum);
  // Sweep controls: start/stop, preset and dwell are real actions; tuning and mode buttons yield while it runs.
  assert(da::handle_touch(366 + 20, 530 + 20, 2360).kind == da::ActionKind::start_scan);
  assert(da::handle_touch(606 + 20, 530 + 20, 2361).kind == da::ActionKind::sweep_preset);
  assert(da::handle_touch(916 + 20, 530 + 20, 2362).kind == da::ActionKind::sweep_dwell);
  snap.sweeping = true;
  snap.sweep_hops = 3;
  snap.sweep_dwell_ms = 500;
  snap.revision = 6;
  da::update(snap);
  assert(displayed("STOP SWEEP") && displayed("RF DETECTOR OFF") && displayed("HOP 3"));
  assert(da::handle_touch(366 + 20, 530 + 20, 2370).kind == da::ActionKind::stop_scan);
  assert(da::handle_touch(200, 470, 2371).kind == da::ActionKind::none);
  assert(da::handle_touch(145, 370, 2372).kind == da::ActionKind::none);
  assert(da::handle_touch(606 + 20, 530 + 20, 2373).kind == da::ActionKind::none);
  snap.sweeping = false;
  snap.revision = 7;
  da::update(snap);
  validate_bounds();
  // Peak marker and readout (the tuner's centre spike is skipped), and tap-to-tune on the live plot and waterfall.
  {
    snap.sample_rate_sps = 2400000;
    snap.frequency_hz = 98900000;
    snap.spectrum_valid = true;
    snap.revision = 8;
    da::update(snap);
    float peaks[da::kTracePoints];
    for (size_t i = 0; i < da::kTracePoints; ++i) peaks[i] = -90.0f;
    peaks[108] = -10.0f;                        // the DC spike at the centre: never the peak
    peaks[150] = -30.0f;
    host_display::reset();
    assert(da::draw_spectrum_frame(peaks, da::kTracePoints, 9, 9000));
    assert(displayed("PEAK 99.374 MHz  -30.0 dBFS"));
    validate_bounds();
    const da::Action plot_tap = da::handle_touch(800, 200, 9010);   // a tap on the trace, just left of the centre
    assert(plot_tap.kind == da::ActionKind::tune_to && plot_tap.value == 98889000);
    assert(da::handle_touch(800, 380, 9011).value == 98889000);     // and on the waterfall
    assert(da::handle_touch(1240, 200, 9012).kind == da::ActionKind::none);  // outside the plot
    snap.receiver_ready = false; snap.revision = 9;
    da::update(snap);
    assert(da::handle_touch(800, 200, 9013).kind == da::ActionKind::none);   // no receiver: nothing to tune
    snap.receiver_ready = true; snap.revision = 10;
    da::update(snap);
    // Band map: a sweep's measured channels replace the waterfall; unmeasured channels are dashes, a tap tunes, and the
    // chip toggles back to the live waterfall.
    snap.map.start_hz = 88100000u; snap.map.step_hz = 200000u; snap.map.count = 100; snap.map.visited = 40;
    for (size_t i = 0; i < 100; ++i) snap.map.level[i] = i < 40 ? static_cast<int16_t>(-400) : da::kHistGap;
    snap.map.level[12] = -250;
    snap.revision = 11;
    host_display::reset();
    da::update(snap);
    assert(displayed("SWEEP MAP  40/100 CHANNELS") && displayed("PEAK 90.500 MHz  -25.0 dBFS") && displayed("LIVE"));
    validate_bounds();
    const auto waterfall_row = []() {
      for (const Prim& p : host_display::prims) if (p.kind == Prim::fill && p.w == 860 && p.h == 1 && p.y == 453) return true;
      return false;
    };
    host_display::reset();
    assert(da::draw_spectrum_frame(peaks, da::kTracePoints, 9, 9300));
    assert(!waterfall_row());                                       // the map is showing: no waterfall rows are drawn
    const da::Action map_tap = da::handle_touch(480, 400, 9020);
    assert(map_tap.kind == da::ActionKind::tune_to && map_tap.value == 90500000);
    host_display::reset();
    assert(da::handle_touch(1140, 138, 9021).kind == da::ActionKind::none);   // chip: back to the live waterfall
    assert(displayed("MAP"));
    host_display::reset();
    assert(da::draw_spectrum_frame(peaks, da::kTracePoints, 9, 9600));
    assert(waterfall_row());
    assert(da::handle_touch(480, 400, 9022).value == 98900000 + static_cast<int>(std::floor(((480.0 - 374.0) / 860.0 - 0.5) * 2400000.0 / 1000.0 + 0.5)) * 1000);
    da::handle_touch(1140, 138, 9023);                              // chip again: the map
    assert(da::handle_touch(480, 400, 9024).value == 90500000);
    snap.sweeping = true; snap.revision = 12;                       // while sweeping the schedule owns tuning
    da::update(snap);
    assert(da::handle_touch(480, 400, 9025).kind == da::ActionKind::none);
    snap.sweeping = false; snap.map = da::BandMapView{}; snap.revision = 13;
    da::update(snap);
    host_display::reset();
    snap.revision = 14;
    da::update(snap);
    assert(!displayed("SWEEP MAP"));
  }
  // Events tab: rows carry time and level, and an event that has a clip says so.
  da::handle_touch(20 + 4 * 210 + 40, 673, 8000);
  assert(da::page() == da::Page::events);
  snap.clip_event_seqs[0] = model.newest(0)->sequence;
  snap.clip_event_seq_count = 1;
  snap.revision = 30;
  host_display::reset();
  da::update(snap);
  assert(displayed("MARK no receiver") && displayed("CLIP") && displayed("RF RISE + AUDIO RISE"));
  validate_bounds();
  // The sensitivity buttons use the same names as the Overview.
  assert(displayed("QUIET") && displayed("STANDARD") && displayed("SENSITIVE"));
  // Detail: with nothing tapped, the newest event is described. Its clip is not among the clips Review lists.
  assert(displayed("EVENT #1  INVESTIGATOR MARK") && displayed("Marked by you") && displayed("1-1 OF 1"));
  assert(displayed("Older clip not listed") && displayed("SCRUB"));
  assert(da::handle_touch(264 + 20, 506 + 20, 8001).kind == da::ActionKind::none);  // no listed clip: VIEW CLIP does nothing
  assert(da::page() == da::Page::events);
  for (uint32_t i = 0; i < 9; ++i) model.mark_manual(9000 + i, 0, 98900000);
  snap.revision = 301;
  host_display::reset();
  da::update(snap);
  assert(displayed("1-5 OF 10") && displayed("#10 ") && displayed("#6 ") && !displayed("#5 "));
  assert(displayed("EVENT #10  INVESTIGATOR MARK"));
  host_display::reset();
  assert(da::handle_touch(300, 166 + 2 * 40 + 19, 8010).kind == da::ActionKind::none);  // third row = event #8
  assert(displayed("EVENT #8  INVESTIGATOR MARK") && displayed("1-5 OF 10"));
  validate_bounds();
  host_display::reset();
  da::handle_touch(206 + 20, 568 + 20, 8020);  // OLDER
  assert(displayed("6-10 OF 10") && displayed("#5 ") && displayed("#1 ") && !displayed("#10 "));
  assert(displayed("EVENT #8  INVESTIGATOR MARK"));  // the tapped event stays selected off-page
  host_display::reset();
  da::handle_touch(206 + 20, 568 + 20, 8021);  // already the oldest page: nothing moves
  assert(host_display::prims.empty());
  model.mark_manual(9100, 0, 0);               // a new event while reading older ones: the page keeps its place
  snap.revision = 302;
  host_display::reset();
  da::update(snap);
  assert(displayed("7-11 OF 11") && displayed("#5 ") && displayed("#1 ") && !displayed("#11 "));
  host_display::reset();
  da::handle_touch(40 + 20, 568 + 20, 8030);   // NEWER
  assert(displayed("2-6 OF 11"));
  da::handle_touch(40 + 20, 568 + 20, 8031);   // NEWER again: back to the newest page
  assert(displayed("1-5 OF 11"));
  // VIEW CLIP: the clip that covers the selected event (here the second in the list) is opened on Review.
  snap.clip_count = 2;
  std::snprintf(snap.clips[0].name, sizeof(snap.clips[0].name), "s1_e3.wav");
  snap.clips[0].event_seqs[0] = 3; snap.clips[0].seq_count = 1;
  std::snprintf(snap.clips[1].name, sizeof(snap.clips[1].name), "s1_e8.wav");
  snap.clips[1].event_seqs[0] = 8; snap.clips[1].event_seqs[1] = 9; snap.clips[1].seq_count = 2;
  snap.revision = 303;
  host_display::reset();
  da::update(snap);
  assert(displayed("Clip: s1_e8.wav") && displayed("VIEW CLIP"));
  const da::Action jump = da::handle_touch(264 + 20, 506 + 20, 8040);
  assert(jump.kind == da::ActionKind::select_clip && jump.value == 1);
  assert(da::page() == da::Page::review && displayed("EVIDENCE REVIEW"));
  validate_bounds();
  snap.clip_count = 0;                         // the Review checks below start from their own clips
  snap.clips[0] = da::ClipInfo{};
  snap.clips[1] = da::ClipInfo{};
  snap.selected_clip = 0;
  da::handle_touch(20 + 4 * 210 + 40, 673, 8050);  // back to Events for the rest of the flow
  assert(da::page() == da::Page::events);
  // Chime toggle.
  assert(da::handle_touch(1082 + 20, 510 + 20, 8051).kind == da::ActionKind::toggle_chime);
  snap.chime_on = true; snap.revision = 304;
  host_display::reset();
  da::update(snap);
  assert(displayed("CHIME ON"));
  // Scrub: opens from the detail box for the event shown there; no trace says so; a collected trace is drawn with the
  // cursor readout, a tap moves the cursor, playback follows the clip, and nothing under the overlay can be tapped.
  {
    const size_t marks_before = model.count();
    const uint32_t total_before = model.total();
    const da::Action open = da::handle_touch(50 + 20, 506 + 20, 8060);
    assert(open.kind == da::ActionKind::scrub_open && open.value == 8 && da::overlay() == da::Overlay::scrub);
    assert(displayed("No trace was kept for this event."));
    snap.scrub.seq = 8; snap.scrub.found = true; snap.scrub.post_filled = 12;
    for (size_t i = 0; i < da::kTraceSamples; ++i) {
      snap.scrub.rf[i] = i < da::kTracePre + 12 ? static_cast<int16_t>(-400 + static_cast<int>(i % 7) * 10) : da::kHistGap;
      snap.scrub.audio[i] = i % 11 == 0 ? da::kHistGap : static_cast<int16_t>(-600 + static_cast<int>(i % 5) * 20);
    }
    snap.revision = 305;
    host_display::reset();
    da::update(snap);
    assert(displayed("EVENT #8  /  RF AND AUDIO AROUND IT") && displayed("COLLECTING +3 s OF +10 s"));
    assert(displayed("CURSOR +0.00 s") && displayed("-10 s") && displayed("+10 s") && displayed("A mark you made"));
    validate_bounds();
    host_display::reset();
    assert(da::handle_touch(925, 250, 8061).kind == da::ActionKind::none);      // tap the plot at about +4.75 s
    assert(displayed("CURSOR +4.75 s"));
    assert(da::handle_touch(50 + 20, 560 + 20, 8062).kind == da::ActionKind::none);  // no clip for this event: PLAY does nothing
    assert(displayed("No clip for this event."));
    // A clip covers it (second in the list): PLAY asks for it, and while it plays the cursor follows the clip.
    snap.clip_count = 2;
    snap.clips[0].event_seqs[0] = 3; snap.clips[0].seq_count = 1;
    snap.clips[1].event_seqs[0] = 8; snap.clips[1].seq_count = 1;
    snap.revision = 306;
    da::update(snap);
    const da::Action play = da::handle_touch(50 + 20, 560 + 20, 8063);
    assert(play.kind == da::ActionKind::play_event_clip && play.value == 8);
    snap.replay_state = 2; snap.scrub.cursor_valid = true; snap.scrub.cursor_ms = -1500; snap.revision = 307;
    host_display::reset();
    da::update(snap);
    assert(displayed("PLAYING -1.50 s") && displayed("STOP") && displayed("the cursor follows the clip"));
    assert(da::handle_touch(50 + 20, 560 + 20, 8064).kind == da::ActionKind::stop_replay);
    assert(da::handle_touch(925, 250, 8065).kind == da::ActionKind::none);     // the clip owns the cursor while it plays
    snap.replay_state = 0; snap.scrub.cursor_valid = false;
    snap.clip_count = 0; snap.clips[0] = da::ClipInfo{}; snap.clips[1] = da::ClipInfo{};
    snap.revision = 308;
    da::update(snap);
    // Nothing under the overlay answers: MARK EVENT and the sensitivity buttons are covered.
    da::handle_touch(900, 470, 8066);
    da::handle_touch(1140, 400, 8067);
    assert(model.count() == marks_before && model.total() == total_before);
    assert(da::handle_touch(1070 + 20, 128 + 20, 8068).kind == da::ActionKind::scrub_close);
    assert(da::overlay() == da::Overlay::none && displayed("DETECTION"));
  }
  // Control run: the explanation, a disabled START with no receiver, the running table without a rate, then a result.
  {
    snap.receiver_ready = false; snap.revision = 309;
    da::update(snap);
    assert(da::handle_touch(818 + 20, 510 + 20, 8070).kind == da::ActionKind::none && da::overlay() == da::Overlay::control);
    assert(displayed("CONTROL RUN  /  FALSE-ALARM CHECK") && displayed("START 5 MIN") && displayed("Needs a running receiver."));
    assert(da::handle_touch(50 + 20, 560 + 20, 8071).kind == da::ActionKind::none);
    snap.receiver_ready = true; snap.revision = 310;
    da::update(snap);
    assert(da::handle_touch(50 + 20, 560 + 20, 8072).kind == da::ActionKind::control_start);
    snap.control.state = 1; snap.control.duration_s = 300; snap.control.elapsed_s = 150; snap.control.observed_s = 149;
    snap.control.count[0] = 0; snap.control.count[1] = 2; snap.control.count[2] = 9;
    snap.revision = 311;
    host_display::reset();
    da::update(snap);
    assert(displayed("RUNNING") && displayed("2m 30s of 5m 00s") && displayed("STOP RUN"));
    assert(displayed("A per-hour rate needs at least 1 minute measured.") && displayed("+12 dB") && displayed("+5 dB"));
    assert(da::handle_touch(50 + 20, 560 + 20, 8073).kind == da::ActionKind::control_stop);
    assert(da::handle_touch(900, 470, 8074).kind == da::ActionKind::none);   // MARK EVENT is covered
    snap.control.state = 2; snap.control.elapsed_s = 300; snap.control.observed_s = 298; snap.control.rate_ok = true;
    snap.control.per_hour[0] = 0.0f; snap.control.per_hour[1] = 24.0f; snap.control.per_hour[2] = 108.0f;
    snap.revision = 312;
    host_display::reset();
    da::update(snap);
    assert(displayed("COMPLETE") && displayed("RUN AGAIN") && displayed("108") && displayed("Measured 4m 58s"));
    assert(!displayed("A per-hour rate needs"));
    validate_bounds();
    assert(da::handle_touch(50 + 20, 560 + 20, 8075).kind == da::ActionKind::control_start);
    da::handle_touch(1070 + 20, 128 + 20, 8076);
    assert(da::overlay() == da::Overlay::none);
    snap.control = da::ControlView{}; snap.chime_on = false; snap.revision = 313;
    da::update(snap);
  }
  // Review tab: newest clip details, a VERIFY action, and the re-check result.
  da::handle_touch(20 + 5 * 210 + 40, 673, 8100);
  assert(da::page() == da::Page::review);
  snap.clip_count = 2;
  std::snprintf(snap.clips[0].name, sizeof(snap.clips[0].name), "s1792536903_e1.wav");
  std::snprintf(snap.clips[0].sha16, sizeof(snap.clips[0].sha16), "9f932fde12345678");
  std::snprintf(snap.clips[0].demod, sizeof(snap.clips[0].demod), "NFM");
  snap.clips[0].duration_ds = 100; snap.clips[0].frequency_hz = 97289000; snap.clips[0].speaker_on = false;
  std::snprintf(snap.clips[1].name, sizeof(snap.clips[1].name), "s1792536903_e0.wav");
  snap.clips[1].duration_ds = 87; snap.clips[1].truncated = true;
  snap.verify_state = 0;
  snap.revision = 31;
  host_display::reset();
  da::update(snap);
  assert(displayed("s1792536903_e1.wav") && displayed("NFM  97.289 MHz  speaker MUTED") && displayed("sha256 9f932fde12345678"));
  assert(displayed("VERIFY SELECTED") && displayed("PLAY SELECTED") && displayed("NOT RE-CHECKED YET") && displayed("8.7 s  TRUNCATED"));
  assert(displayed("1 RADIO s1792536903_e1.wav") && displayed("2 RADIO s1792536903_e0.wav"));
  validate_bounds();
  // Selecting the second row moves the details to that clip.
  {
    const da::Action pick = da::handle_touch(600, 172 + 44 + 20, 8150);
    assert(pick.kind == da::ActionKind::select_clip && pick.value == 1);
    snap.selected_clip = 1; snap.revision = 310;
    host_display::reset();
    da::update(snap);
    assert(displayed("sha256 ") && displayed("speaker MUTED  TRUNCATED"));
    snap.selected_clip = 0; snap.revision = 311;
    host_display::reset();
    da::update(snap);
  }
  assert(da::handle_touch(537, 490, 8200).kind == da::ActionKind::verify_clip);
  assert(da::handle_touch(817, 490, 8210).kind == da::ActionKind::play_clip);
  snap.replay_state = 1; snap.revision = 312;
  host_display::reset();
  da::update(snap);
  assert(displayed("LOADING...") && displayed("Reading the clip and checking its hash"));
  assert(da::handle_touch(817, 490, 8220).kind == da::ActionKind::none && da::handle_touch(537, 490, 8221).kind == da::ActionKind::none);
  snap.replay_state = 2; snap.revision = 313;
  host_display::reset();
  da::update(snap);
  assert(displayed("STOP REPLAY") && displayed("PLAYING through the speaker; mic paused"));
  assert(da::handle_touch(817, 490, 8230).kind == da::ActionKind::stop_replay && da::handle_touch(537, 490, 8231).kind == da::ActionKind::none);
  snap.replay_state = 3; snap.revision = 314;
  std::snprintf(snap.replay_note, sizeof(snap.replay_note), "Hash mismatch: not played");
  host_display::reset();
  da::update(snap);
  assert(displayed("Hash mismatch: not played"));
  snap.replay_state = 0; snap.revision = 315;
  host_display::reset();
  da::update(snap);
  snap.verify_state = 1; snap.revision = 32;
  host_display::reset();
  da::update(snap);
  assert(displayed("VERIFYING..."));
  assert(da::handle_touch(537, 490, 8300).kind == da::ActionKind::none);   // not while a check is running
  snap.verify_state = 2; snap.revision = 33;
  host_display::reset();
  da::update(snap);
  assert(displayed("VERIFIED: file on the card matches"));
  snap.verify_state = 3; snap.revision = 34;
  host_display::reset();
  da::update(snap);
  assert(displayed("MISMATCH"));
  validate_bounds();
  // Microphone: off by default, explicit start, always-visible banner, paused state is stated, mic clips are labelled.
  assert(displayed("MICROPHONE CLIPS (ROOM AUDIO)") && displayed("START MIC") && !displayed("MIC LISTENING"));
  assert(da::handle_touch(60, 570, 8450).kind == da::ActionKind::toggle_mic);
  assert(da::handle_touch(60, 390, 8460).kind == da::ActionKind::toggle_clips);
  snap.mic_state = 1; snap.revision = 35;
  host_display::reset();
  da::update(snap);
  assert(displayed("MIC LISTENING") && displayed("STOP MIC") && displayed("LISTENING"));
  validate_bounds();
  snap.mic_state = 2; snap.revision = 36;
  host_display::reset();
  da::update(snap);
  assert(displayed("MIC PAUSED: SPEAKER ON") && displayed("PAUSED: SPEAKER ON"));
  snap.mic_state = 3; snap.revision = 37;
  std::snprintf(snap.mic_error, sizeof(snap.mic_error), "mic_begin_failed");
  host_display::reset();
  da::update(snap);
  assert(displayed("MIC FAILED") && displayed("mic_begin_failed"));
  snap.mic_state = 1; snap.mic_error[0] = 0;
  snap.clips[0].mic = true; snap.clips[0].speaker_on = false;
  snap.revision = 38;
  host_display::reset();
  da::update(snap);
  assert(displayed("ROOM MIC  speaker MUTED") && displayed("1 MIC   s1792536903_e1.wav") && !displayed("97.289 MHz"));
  validate_bounds();
  snap.mic_state = 0; snap.clips[0].mic = false; snap.revision = 39;
  host_display::reset();
  da::update(snap);
  assert(!displayed("MIC LISTENING"));
  // (i) popup: opens on every tab with that tab's text, pages, hides the body, and closes cleanly.
  host_display::reset();
  assert(da::handle_touch(396, 39, 8470).kind == da::ActionKind::none);
  assert(displayed("ABOUT THIS PAGE  -  REVIEW") && displayed("SESSION ARCHIVE") && displayed("PAGE 1/2"));
  validate_bounds();
  host_display::reset();
  da::handle_touch(425, 586, 8480);  // NEXT is at x 350..500
  assert(displayed("PAGE 2/2") && displayed("Each replay is noted in the log"));
  host_display::reset();
  snap.revision = 400;
  da::update(snap);  // a new snapshot while the popup is open must not paint over it
  assert(!displayed("EVIDENCE REVIEW"));
  assert(da::handle_touch(1000, 586, 8490).kind == da::ActionKind::none);  // CLOSE
  assert(displayed("EVIDENCE REVIEW"));
  da::handle_touch(396, 39, 8500);
  da::handle_touch(50, 300, 8510);  // a tap outside the popup closes it
  assert(displayed("EVIDENCE REVIEW"));
  da::handle_touch(20 + 0 * 210 + 40, 673, 8520);  // Overview has its own text
  da::handle_touch(396, 39, 8530);
  assert(displayed("ABOUT THIS PAGE  -  OVERVIEW") && displayed("does not detect ghosts"));
  da::handle_touch(1000, 586, 8540);
  da::handle_touch(20 + 5 * 210 + 40, 673, 8550);
  da::handle_touch(20 + 1 * 210 + 40, 673, 8400);  // back to Spectrum
  // Once another screen takes the framebuffer, Dead Air must be silent.
  screen::begin_transition(screen::Id::home, 3000);
  screen::finish_transition();
  host_display::reset();
  snap.revision = 2;
  da::update(snap);
  assert(host_display::prims.empty());
  assert(da::handle_touch(900, 470, 3200).kind == da::ActionKind::none);
  da::leave();
  assert(!da::active());
  std::puts("Dead Air renderer host tests PASS");
}
