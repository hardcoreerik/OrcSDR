// SSTV on the P4 console: the shared receive-only decoder and app (sstv_receiver, sstv_app, archive; apps/orcsdr-tab5/ui, unchanged) fed with
// the receiver's demodulated audio (NFM for VHF/UHF, USB or LSB below 30 MHz) at 48 kHz, shown on the compact dashboard
// (sstv_compact_dashboard.cpp): RECEIVE, IMAGES, TUNE, SETUP. Receive only. Not on this target yet: the ANALYZER page, the RF spectrum and
// tap-to-tune, keypad frequency entry, the full-size picture viewer, the IF filter choice (the demodulator has its own channel filter).
#include "audio_service.hpp"
#include "home_app.hpp"
#include "orcsdr_storage.hpp"
#include "present.hpp"
#include "radio_service.hpp"
#include "screens.hpp"
#include "sstv_app.hpp"
#include "sstv_compact_dashboard.hpp"
#include "sstv_receiver.hpp"
#include "sstv_tune.hpp"
#include "ui_support.hpp"

namespace {
namespace sv = orcsdr::sstv;

bool resize_viewport() {
  const auto* g = present_geometry();
  return sv::ui_set_viewport(g ? sv::compact::Rect{int(g->safe.x), int(g->safe.y), int(g->safe.width), int(g->safe.height)}
                               : sv::compact::Rect{0, 0, 1280, 720});
}

// Runs on the receiver task: hands the demodulated 48 kHz audio to the decoder's lock-free ring (it never blocks or draws).
void pcm_tap(const int16_t* pcm, size_t count) { sv::offer_audio_48k(pcm, count); }

orcsdr::sstv_app::Hooks app_hooks() {
  orcsdr::sstv_app::Hooks h;
  h.receiver_ready = [] { return orcsdr::radio::snapshot().receiver_state == 2; };
  h.streaming = [] { return orcsdr::radio::snapshot().receiver_state == 2; };
  h.sd_ready = [](bool) { return orcsdr::storage::mounted(); };   // the card mounts itself in the background after boot
  h.tune = [](uint32_t hz) {
    if (orcsdr::radio::snapshot().receiver_state == 2) orcsdr::radio::set_sample_rate_override(240000);
    home_app::request_frequency(hz);
  };
  h.set_demod = [](uint8_t demod) {
    using D = orcsdr::sstv_tune::Demod;
    const auto mode = static_cast<D>(demod) == D::usb ? orcsdr::dsp::Mode::usb : static_cast<D>(demod) == D::lsb ? orcsdr::dsp::Mode::lsb : orcsdr::dsp::Mode::nfm;
    audio_service::set_mode(mode, true);
  };
  h.release_tuning = [] { orcsdr::radio::set_sample_rate_override(0); };
  h.sample_rate = [] { return orcsdr::radio::sample_rate(); };
  h.gain_info = [](orcsdr::sstv_app::GainInfo* info) {
    const auto g = orcsdr::radio::gain();
    if (!g.available) return false;
    info->available = true;
    info->is_auto = g.auto_mode;
    info->tenth_db = g.tenth_db;
    info->step_count = g.step_count;
    for (size_t i = 0; i < info->step_count && i < 32; ++i) info->steps[i] = g.steps[i];
    return true;
  };
  h.gain_auto = [] { orcsdr::radio::set_gain_auto(); };
  h.gain_manual = [](int tenth_db) { orcsdr::radio::set_gain_manual(tenth_db); };
  h.header_controls = ui_support::draw_header;
  h.utc_epoch = ui_support::utc_epoch;
  h.store.get_bool = ui_support::get_bool;
  h.store.put_bool = ui_support::put_bool;
  h.store.get_u8 = ui_support::get_u8;
  h.store.put_u8 = ui_support::put_u8;
  h.store.get_u32 = ui_support::get_u32;
  h.store.put_u32 = ui_support::put_u32;
  return h;
}

void enter() {
  sv::ui_set_rf_level_source([] { return orcsdr::radio::level_dbfs(); });
  if (!resize_viewport()) {
    home_app::request_home();
    return;
  }
  ui_support::header_reset();
  ui_support::claim_screen(orcsdr::screens::Id::sstv);
  audio_service::set_active(true);                  // the demodulator runs; the speaker follows the user's sound setting
  audio_service::set_pcm_tap(pcm_tap);
  orcsdr::sstv_app::install(app_hooks());
  orcsdr::sstv_app::open();
}

void leave() {
  audio_service::set_pcm_tap(nullptr);
  orcsdr::sstv_app::close();
  orcsdr::radio::set_sample_rate_override(0);
}

void tick() {
  orcsdr::sstv_app::tick();   // decoder service, the self-test, the archive hand-off
  // The Tab5's main loop asks the app to repaint (draw(false)) about five times a second; the app's own tick does not.
  static uint32_t last_draw_ms = 0;
  const uint32_t now = ui_support::now_ms();
  if (now - last_draw_ms >= 200u && !ui_support::header_overlay_open()) {
    last_draw_ms = now;
    orcsdr::sstv_app::draw(false);
  }
  ui_support::header_service();
}

void tap(int x, int y) {
  if (ui_support::handle_header_touch(x, y, true)) return;
  orcsdr::sstv_app::touch(x, y);
}
}  // namespace

namespace screens {
const Screen& sstv_screen() {
  static const Screen s{orcsdr::dashboards::Id::sstv, "SSTV", enter, leave, tick, tap, nullptr, nullptr, nullptr, []() {
    if (!resize_viewport()) return false;
    orcsdr::sstv_app::draw(true);
    return true;
  }};
  return s;
}
}  // namespace screens
