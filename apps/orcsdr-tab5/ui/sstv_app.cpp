#include "sstv_app.hpp"

#include <M5Unified.h>

#include <iterator>

#include "screen_controller.hpp"
#include "sstv_archive.hpp"
#include "sstv_dashboard.hpp"
#include "sstv_diag.hpp"
#include "sstv_receiver.hpp"

namespace orcsdr::sstv_app {
namespace {

namespace arc = orcsdr::sstv_archive;
using orcsdr::sstv_settings::Detection;
using orcsdr::sstv_settings::Settings;

Hooks g_hooks;
Settings g_settings;
uint32_t g_frequency_hz = 145800000u;   // ISS SSTV, the default profile

uint32_t utc() { return g_hooks.utc_epoch ? g_hooks.utc_epoch() : 0u; }
uint32_t utc_source() { return utc(); }

void notice(const char* text) { orcsdr::sstv::ui_notice(text); }

uint32_t g_last_tune_ms = 0;

void tune_profile(size_t index) {
  if (index >= std::size(orcsdr::sstv::kProfiles)) return;
  g_last_tune_ms = millis();
  g_frequency_hz = orcsdr::sstv::kProfiles[index].hz;
  orcsdr::sstv::set_frequency(g_frequency_hz);
  if (g_hooks.tune) g_hooks.tune(g_frequency_hz);
}

orcsdr::sstv::UiSnapshot snapshot() {
  orcsdr::sstv::UiSnapshot s;
  s.receiver_ready = g_hooks.receiver_ready && g_hooks.receiver_ready();
  s.streaming = g_hooks.streaming && g_hooks.streaming();
  s.sd_ready = g_hooks.sd_ready && g_hooks.sd_ready(false);
  s.frequency_hz = g_frequency_hz;
  s.settings = g_settings;
  return s;
}

// Detection or storage setting changed: apply, remember, redraw. Nothing is restarted and an
// image in progress keeps decoding.
void commit_settings() {
  orcsdr::sstv_settings::apply(g_settings);
  orcsdr::sstv_settings::save(g_hooks.store, g_settings);
  arc::set_enabled(g_settings.autosave);
  orcsdr::sstv::ui_set_settings(g_settings);
}

bool queue_for_sd(int32_t which) {
  if (!g_hooks.sd_ready || !g_hooks.sd_ready(true)) {
    notice("NO SD CARD - INSERT ONE");
    return false;
  }
  const orcsdr::sstv::Image image = which < 0 ? orcsdr::sstv::snapshot_live()
                                              : orcsdr::sstv::pin_archived(static_cast<size_t>(which));
  if (!image.pixels) {
    notice("NOTHING TO SAVE YET");
    return false;
  }
  if (!arc::enqueue(image, utc())) {
    orcsdr::sstv::release_image(image);
    notice("SD QUEUE BUSY - TRY AGAIN");
    return false;
  }
  notice("QUEUED FOR SD - SEE IMAGES TAB");
  return true;
}

void handle(const orcsdr::sstv::Action& action) {
  using Kind = orcsdr::sstv::ActionKind;
  switch (action.kind) {
    case Kind::tune_profile:
      tune_profile(static_cast<size_t>(action.value));
      break;
    case Kind::set_detection:
      g_settings = orcsdr::sstv_settings::with_detection(g_settings, static_cast<Detection>(action.value));
      commit_settings();
      break;
    case Kind::select_mode:
      g_settings = orcsdr::sstv_settings::with_manual_mode(
          g_settings, static_cast<orcsdr::sstv::Mode>(action.value));
      commit_settings();
      break;
    case Kind::toggle_autosave:
      g_settings.autosave = !g_settings.autosave;
      commit_settings();
      break;
    case Kind::save_image:
      (void)queue_for_sd(action.value);
      break;
    case Kind::clear_gallery:
      orcsdr::sstv::clear_gallery();
      notice("RAM GALLERY CLEARED");
      break;
    case Kind::abort_rx:
      orcsdr::sstv::abort_image();
      notice("IMAGE ABORTED / SEARCHING");
      break;
    default:
      break;
  }
}

}  // namespace

void install(const Hooks& hooks) {
  g_hooks = hooks;
  orcsdr::sstv::set_utc_source(utc_source);
  orcsdr::sstv::set_header_hook(hooks.header_controls);
}

void open() {
  g_settings = orcsdr::sstv_settings::load(g_hooks.store);
  orcsdr::sstv_settings::apply(g_settings);          // before start(): the first header is already filtered
  if (!arc::initialised()) {
    arc::Config config;
    config.enabled = g_settings.autosave;
    arc::init(nullptr, config);
    (void)arc::start_worker();
  } else {
    arc::set_enabled(g_settings.autosave);
  }
  arc::request_recover();                            // runs on the worker when it is idle
  orcsdr::sstv::start(g_frequency_hz);
  orcsdr::screens::begin_transition(orcsdr::screens::Id::sstv, millis());
  draw(true);
  orcsdr::screens::finish_transition();
  tune_profile(orcsdr::sstv::ui_profile());
}

void close() {
  if (!orcsdr::sstv::ui_active()) return;
  orcsdr::sstv_diag::abort_selftest();               // a bench test must not outlive the screen
  orcsdr::sstv::stop();                              // an image in progress becomes a partial entry
  (void)arc::poll(utc());                            // queue it now; the worker keeps running
  orcsdr::sstv::ui_leave();
  if (g_hooks.release_tuning) g_hooks.release_tuning();
}

void tick() {
  if (!orcsdr::sstv::ui_active()) return;
  // The dongle may not have been ready when the dashboard opened (shortly after a boot or a flash), and the
  // single tune from open() is then lost: the screen is up but nothing streams. Retune the radio, and only the
  // radio (never set_frequency, which would end a picture), whenever a usable receiver exists, no IQ flows and
  // nothing is being received.
  if (g_hooks.tune && g_hooks.receiver_ready && g_hooks.streaming && millis() - g_last_tune_ms > 4000u &&
      g_hooks.receiver_ready() && !g_hooks.streaming() && !orcsdr::sstv::metrics().receiving) {
    g_last_tune_ms = millis();
    g_hooks.tune(g_frequency_hz);
  }
  orcsdr::sstv_diag::tick(millis());               // bench self-test audio, when one was requested
  orcsdr::sstv::service();
  (void)arc::poll(utc());
}

void draw(bool static_panel) {
  if (!static_panel && !orcsdr::screens::may_draw(orcsdr::screens::Id::sstv)) return;
  if (!static_panel) orcsdr::screens::note_visible_update(orcsdr::screens::Id::sstv);
  const auto snap = snapshot();
  if (static_panel || !orcsdr::sstv::ui_active()) orcsdr::sstv::ui_enter(snap);
  else orcsdr::sstv::ui_update(snap);
}

void touch(int32_t x, int32_t y) { handle(orcsdr::sstv::ui_touch(x, y)); }
bool save_image(int32_t which) { return queue_for_sd(which); }

}  // namespace orcsdr::sstv_app
