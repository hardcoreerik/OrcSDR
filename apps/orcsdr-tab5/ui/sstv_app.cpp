#include "sstv_app.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#include "screen_controller.hpp"
#include "sstv_archive.hpp"
#include "sstv_audio_scope.hpp"
#include "sstv_dashboard.hpp"
#include "sstv_diag.hpp"
#include "sstv_receiver.hpp"
#include "sstv_tune.hpp"

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

// ---------------------------------------------------------------- tuning state
namespace tn = orcsdr::sstv_tune;
struct Tune {
  uint8_t step_index = 3;     // 1 kHz
  uint8_t demod_choice = 0;   // 0 = follows the frequency, otherwise 1 + sstv_tune::Demod
  bool rails = true;
};
Tune g_tune;
uint32_t g_gain_reapply_ms = 0;
int g_manual_gain_tenth = 0;
bool g_manual_gain = false;
constexpr const char* kKeyHz = "sstv_hz";
constexpr const char* kKeyStep = "sstv_step";
constexpr const char* kKeyDemod = "sstv_demod";
constexpr const char* kKeyRails = "sstv_rails";

tn::Demod demod_now() {
  return g_tune.demod_choice ? static_cast<tn::Demod>(g_tune.demod_choice - 1) : tn::default_demod(g_frequency_hz);
}
void apply_demod() {
  if (g_hooks.set_demod) g_hooks.set_demod(static_cast<uint8_t>(demod_now()));
}
void load_tune() {
  g_tune = Tune{};
  g_frequency_hz = 145800000u;
  const auto& st = g_hooks.store;
  if (st.get_u32) g_frequency_hz = tn::clamp_hz(st.get_u32(kKeyHz, g_frequency_hz));
  if (st.get_u8) {
    const uint8_t step = st.get_u8(kKeyStep, 3);
    g_tune.step_index = step < tn::step_count() ? step : 3;
    const uint8_t demod = st.get_u8(kKeyDemod, 0);
    g_tune.demod_choice = demod <= 3 ? demod : 0;
  }
  if (st.get_bool) g_tune.rails = st.get_bool(kKeyRails, true);
}
void save_tune() {
  const auto& st = g_hooks.store;
  if (st.put_u32) st.put_u32(kKeyHz, g_frequency_hz);
  if (st.put_u8) {
    st.put_u8(kKeyStep, g_tune.step_index);
    st.put_u8(kKeyDemod, g_tune.demod_choice);
  }
  if (st.put_bool) st.put_bool(kKeyRails, g_tune.rails);
}
orcsdr::sstv::TuneView tune_view() {
  orcsdr::sstv::TuneView v;
  v.step_index = g_tune.step_index;
  v.demod = static_cast<uint8_t>(demod_now());
  v.demod_auto = g_tune.demod_choice == 0;
  v.rails = g_tune.rails;
  GainInfo gain;
  if (g_hooks.gain_info && g_hooks.gain_info(&gain)) {
    v.gain_available = gain.available;
    v.gain_auto = gain.is_auto;
    v.gain_tenth_db = static_cast<int16_t>(gain.tenth_db);
  }
  const uint32_t rate = g_hooks.sample_rate ? g_hooks.sample_rate() : 0u;
  v.rate_sps = rate ? rate : 240000u;
  return v;
}

// Point the radio at g_frequency_hz: the receiver forgets the picture in progress, the tuner retunes and the
// demodulator follows. A fixed manual gain is put back once the stream has settled after the retune.
void retune() {
  g_last_tune_ms = millis();
  orcsdr::sstv::set_frequency(g_frequency_hz);
  apply_demod();
  if (g_hooks.tune) g_hooks.tune(g_frequency_hz);
  if (g_manual_gain) g_gain_reapply_ms = millis() + 1500u;
  save_tune();
}
bool tune_hz(uint32_t hz) {
  if (hz < tn::kMinHz || hz > tn::kMaxHz) return false;
  if (hz == g_frequency_hz) return true;
  g_frequency_hz = hz;
  retune();
  return true;
}

void tune_profile(size_t index) {
  if (index >= std::size(orcsdr::sstv::kProfiles)) return;
  (void)tune_hz(orcsdr::sstv::kProfiles[index].hz);
}

void gain_step(int direction) {
  GainInfo gain;
  if (!g_hooks.gain_info || !g_hooks.gain_manual || !g_hooks.gain_info(&gain) || !gain.available || !gain.step_count) return;
  size_t nearest = 0;
  for (size_t i = 1; i < gain.step_count; ++i)
    if (std::abs(gain.steps[i] - gain.tenth_db) < std::abs(gain.steps[nearest] - gain.tenth_db)) nearest = i;
  const int next = std::clamp(static_cast<int>(nearest) + direction, 0, static_cast<int>(gain.step_count) - 1);
  g_manual_gain = true;
  g_manual_gain_tenth = gain.steps[next];
  g_hooks.gain_manual(g_manual_gain_tenth);
}
void gain_toggle() {
  GainInfo gain;
  if (!g_hooks.gain_info || !g_hooks.gain_info(&gain) || !gain.available) return;
  if (gain.is_auto && g_hooks.gain_manual) {
    g_manual_gain = true;
    g_manual_gain_tenth = gain.tenth_db;
    g_hooks.gain_manual(gain.tenth_db);
  } else if (g_hooks.gain_auto) {
    g_manual_gain = false;
    g_hooks.gain_auto();
  }
}
void audio_observer(const int16_t* samples, size_t count) { orcsdr::sstv_audio_scope::push(samples, count); }

orcsdr::sstv::UiSnapshot snapshot() {
  orcsdr::sstv::UiSnapshot s;
  s.receiver_ready = g_hooks.receiver_ready && g_hooks.receiver_ready();
  s.streaming = g_hooks.streaming && g_hooks.streaming();
  s.sd_ready = g_hooks.sd_ready && g_hooks.sd_ready(false);
  s.frequency_hz = g_frequency_hz;
  s.settings = g_settings;
  s.tune = tune_view();
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
    case Kind::tune_hz:
      if (!tune_hz(static_cast<uint32_t>(action.value))) notice("FREQUENCY OUT OF RANGE");
      break;
    case Kind::nudge:
      (void)nudge(action.value < 0 ? -1 : 1);
      break;
    case Kind::set_step:
      if (action.value >= 0 && static_cast<size_t>(action.value) < tn::step_count()) {
        g_tune.step_index = static_cast<uint8_t>(action.value);
        save_tune();
      }
      break;
    case Kind::cycle_demod:
      cycle_demod();
      break;
    case Kind::gain_toggle:
      gain_toggle();
      break;
    case Kind::gain_step:
      gain_step(action.value < 0 ? -1 : 1);
      break;
    case Kind::toggle_rails:
      g_tune.rails = !g_tune.rails;
      save_tune();
      break;
    case Kind::apply_correction:
      (void)tune_hz(tn::clamp_hz(static_cast<uint32_t>(static_cast<int64_t>(g_frequency_hz) + action.value)));
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
  load_tune();
  g_manual_gain = false;
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
  (void)orcsdr::sstv_audio_scope::start();
  orcsdr::sstv::set_audio_observer(audio_observer);
  orcsdr::screens::begin_transition(orcsdr::screens::Id::sstv, millis());
  draw(true);
  orcsdr::screens::finish_transition();
  g_last_tune_ms = millis();
  apply_demod();
  if (g_hooks.tune) g_hooks.tune(g_frequency_hz);
}

void close() {
  if (!orcsdr::sstv::ui_active()) return;
  orcsdr::sstv_diag::abort_selftest();               // a bench test must not outlive the screen
  orcsdr::sstv::set_audio_observer(nullptr);
  orcsdr::sstv_audio_scope::stop();
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
  if (g_gain_reapply_ms && static_cast<int32_t>(millis() - g_gain_reapply_ms) >= 0) {
    g_gain_reapply_ms = 0;
    if (g_manual_gain && g_hooks.gain_manual) g_hooks.gain_manual(g_manual_gain_tenth);
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

bool active() { return orcsdr::sstv::ui_active(); }
uint32_t frequency_hz() { return g_frequency_hz; }
bool tune_to(uint32_t hz) { return active() && tune_hz(hz); }
uint32_t step_hz() { return tn::step_hz(g_tune.step_index); }
bool nudge(int direction, bool fine) {
  if (!active()) return false;
  return tune_hz(tn::nudge(g_frequency_hz, fine ? tn::step_hz(0) : step_hz(), direction < 0 ? -1 : 1));
}
uint32_t next_step_hz(bool larger) {
  const size_t index = g_tune.step_index;
  const size_t next = larger ? std::min(index + 1, tn::step_count() - 1) : (index ? index - 1 : 0);
  return tn::step_hz(next);
}
bool set_step(uint32_t step) {
  if (!active()) return false;
  g_tune.step_index = static_cast<uint8_t>(tn::step_index(step));
  save_tune();
  return true;
}
void cycle_demod() {
  if (!active()) return;
  g_tune.demod_choice = static_cast<uint8_t>((g_tune.demod_choice + 1) % 4);
  apply_demod();
  save_tune();
}
const char* demod_label() {
  static char label[16];
  std::snprintf(label, sizeof(label), g_tune.demod_choice ? "%s" : "%s (AUTO)", tn::demod_name(demod_now()));
  return label;
}
bool wants_spectrum() { return orcsdr::sstv::ui_wants_spectrum(); }
void feed_spectrum(const float* levels_db, size_t bins, uint32_t rate_sps) {
  orcsdr::sstv::ui_feed_spectrum(levels_db, bins, rate_sps);
}

}  // namespace orcsdr::sstv_app
