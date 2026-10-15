#include "sstv_app.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "screen_controller.hpp"
#include "sstv_archive.hpp"
#include "memory_reclaim.hpp"
#include "sstv_audio_scope.hpp"
#include "sstv_capture.hpp"
#include "sstv_dashboard.hpp"
#include "sstv_diag.hpp"
#include "sstv_export.hpp"
#include "sstv_receiver.hpp"
#include "sstv_status.hpp"
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
  uint8_t if_index = 0;       // index into kIfWidths
};
// 0 = leave the radio's own width (25 kHz on the Browse path); the others override it for SSTV only.
constexpr uint32_t kIfWidths[] = {0, 100000, 50000, 25000, 16000, 12000, 8000};
Tune g_tune;
uint32_t g_gain_reapply_ms = 0;
int g_manual_gain_tenth = 0;
bool g_manual_gain = false;
constexpr const char* kKeyHz = "sstv_hz";
constexpr const char* kKeyStep = "sstv_step";
constexpr const char* kKeyDemod = "sstv_demod";
constexpr const char* kKeyRails = "sstv_rails";
constexpr const char* kKeyIf = "sstv_if";

tn::Demod demod_now() {
  return g_tune.demod_choice ? static_cast<tn::Demod>(g_tune.demod_choice - 1) : tn::default_demod(g_frequency_hz);
}
void apply_demod() {
  if (g_hooks.set_demod) g_hooks.set_demod(static_cast<uint8_t>(demod_now()));
  if (g_hooks.set_if_bandwidth) g_hooks.set_if_bandwidth(kIfWidths[g_tune.if_index]);
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
    const uint8_t width = st.get_u8(kKeyIf, 0);
    g_tune.if_index = width < std::size(kIfWidths) ? width : 0;
  }
  if (st.get_bool) g_tune.rails = st.get_bool(kKeyRails, true);
}
void save_tune() {
  const auto& st = g_hooks.store;
  if (st.put_u32) st.put_u32(kKeyHz, g_frequency_hz);
  if (st.put_u8) {
    st.put_u8(kKeyStep, g_tune.step_index);
    st.put_u8(kKeyDemod, g_tune.demod_choice);
    st.put_u8(kKeyIf, g_tune.if_index);
  }
  if (st.put_bool) st.put_bool(kKeyRails, g_tune.rails);
}
orcsdr::sstv::TuneView tune_view() {
  orcsdr::sstv::TuneView v;
  v.step_index = g_tune.step_index;
  v.demod = static_cast<uint8_t>(demod_now());
  v.demod_auto = g_tune.demod_choice == 0;
  v.rails = g_tune.rails;
  v.if_hz = g_hooks.if_bandwidth ? g_hooks.if_bandwidth() : kIfWidths[g_tune.if_index];
  v.if_auto = kIfWidths[g_tune.if_index] == 0;
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
// ---------------------------------------------------------------- read-only export (GET /api/sstv, /api/sstv/preview)
// The UI task publishes the newest finished picture as a small snapshot; the web server only reads that snapshot. It republishes
// when the newest gallery serial changes, when the archive filename for it appears or changes, or when the gallery is emptied.
uint32_t g_export_tried_serial = 0;
char g_export_tried_name[64]{};
bool g_export_tried = false;
void export_service() {
  namespace ex = orcsdr::sstv_export;
  if (!ex::ready()) return;
  uint32_t published_serial = 0;
  char published_name[64]{};
  const bool published = ex::published_capture(&published_serial, published_name, sizeof(published_name));
  if (orcsdr::sstv::archive_count() == 0) {
    if (published) ex::publish_empty();
    g_export_tried = false;
    return;
  }
  const orcsdr::sstv::Image newest = orcsdr::sstv::archived_image(0);   // metadata only: nothing is pinned yet
  char name[64]{};
  arc::Result saved;
  if (newest.session_id && arc::result_for(newest.session_id, &saved) && saved.state == arc::JobState::saved)
    std::snprintf(name, sizeof(name), "%s", saved.filename);
  if (published && published_serial == newest.serial && std::strcmp(published_name, name) == 0) return;
  if (g_export_tried && g_export_tried_serial == newest.serial && std::strcmp(g_export_tried_name, name) == 0) return;   // already tried
  g_export_tried = true;
  g_export_tried_serial = newest.serial;
  std::snprintf(g_export_tried_name, sizeof(g_export_tried_name), "%s", name);
  const orcsdr::sstv::Image pinned = orcsdr::sstv::pin_archived(0);
  if (!pinned.pixels) return;                       // nothing was pinned, so there is nothing to release
  uint8_t* back = ex::back_bmp();
  uint16_t preview_height = 0;
  const size_t bytes = back ? ex::encode_preview_bmp(pinned.pixels, pinned.width, pinned.height, pinned.decoded_rows, back,
                                                     ex::kMaxBmpBytes, &preview_height)
                            : 0;
  if (bytes) {
    orcsdr::sstv_status::ImageChips chips;
    orcsdr::sstv_status::image_chips(pinned, &chips);
    ex::Meta meta;
    meta.serial = pinned.serial;
    meta.session_id = pinned.session_id;
    meta.frequency_hz = pinned.frequency_hz;
    meta.utc_epoch = pinned.utc_epoch;
    meta.width = pinned.width;
    meta.height = pinned.height;
    meta.rows = pinned.decoded_rows;
    meta.preview_height = preview_height;
    meta.sync_locked = pinned.sync_locked;
    meta.sync_missed = pinned.sync_missed;
    meta.vis_code = pinned.vis_code;
    meta.partial = pinned.partial;
    meta.start_unknown = pinned.start_unknown;
    std::snprintf(meta.mode, sizeof(meta.mode), "%s", orcsdr::sstv::mode_name(pinned.mode));
    std::snprintf(meta.identification, sizeof(meta.identification), "%s", chips.identification);
    std::snprintf(meta.end, sizeof(meta.end), "%s", chips.ending);
    std::snprintf(meta.filename, sizeof(meta.filename), "%s", name);
    (void)ex::publish(meta, bytes);
  }
  orcsdr::sstv::release_image(pinned);              // exactly once per successful pin
}

void audio_observer(const int16_t* samples, size_t count) {
  orcsdr::sstv_audio_scope::push(samples, count);
  orcsdr::sstv_capture::push(samples, count);
}
// "SSTV REC SAVE" writes the file a few KB per main-loop tick: one blocking 1 MB SD write would stop decoder service for seconds.
struct CaptureSave {
  std::FILE* file = nullptr;
  size_t offset = 0;
  bool header_done = false;
  bool finished = false, failed = false;
  char path[96]{};
};
CaptureSave g_save;
void capture_save_step() {
  if (!g_save.file) return;
  namespace cap = orcsdr::sstv_capture;
  bool ok = true;
  if (!g_save.header_done) {
    uint8_t header[44];
    cap::header(header);
    ok = std::fwrite(header, 1, sizeof(header), g_save.file) == sizeof(header);
    g_save.header_done = true;
  } else if (g_save.offset < cap::pcm_bytes()) {
    const size_t chunk = std::min<size_t>(4096, cap::pcm_bytes() - g_save.offset);
    ok = std::fwrite(cap::pcm() + g_save.offset, 1, chunk, g_save.file) == chunk;
    g_save.offset += chunk;
  }
  if (!ok || (g_save.header_done && g_save.offset >= cap::pcm_bytes())) {
    std::fclose(g_save.file);
    g_save.file = nullptr;
    g_save.failed = !ok;
    g_save.finished = true;
  }
}

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
    case Kind::cycle_if:
      cycle_if_bandwidth();
      break;
    case Kind::toggle_rails:
      g_tune.rails = !g_tune.rails;
      save_tune();
      break;
    case Kind::apply_correction:
      (void)tune_hz(static_cast<uint32_t>(std::clamp<int64_t>(static_cast<int64_t>(g_frequency_hz) + action.value, tn::kMinHz, tn::kMaxHz)));
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

// What the SSTV receiver keeps in PSRAM after the user has left its screen: the picture gallery and a recording buffer. Both are
// conveniences; the registry may ask for them back when another feature (the FT8 decoder) cannot start for lack of memory.
// Nothing is released while the SSTV screen is open: the pictures on it are in use.
size_t memory_held() {
  // Only what memory_release() would give back: nothing while the SSTV screen is open, and not a recording in progress.
  if (orcsdr::sstv::ui_active()) return 0;
  return orcsdr::sstv::metrics().frame_bytes + (orcsdr::sstv_capture::recording() ? 0u : orcsdr::sstv_capture::held_bytes());
}
size_t memory_release() {
  if (orcsdr::sstv::ui_active()) return 0;
  const size_t before = memory_held();
  orcsdr::sstv::clear_gallery();
  if (!orcsdr::sstv_capture::recording()) orcsdr::sstv_capture::release();
  const size_t after = memory_held();
  return before > after ? before - after : 0;
}

}  // namespace

void install(const Hooks& hooks) {
  g_hooks = hooks;
  (void)orcsdr::memory_reclaim::register_holder({"SSTV pictures", memory_held, memory_release});
  orcsdr::sstv_export::init();
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
  if (g_save.file) {                                 // the save reads the capture buffer that is released below
    std::fclose(g_save.file);
    g_save.file = nullptr;
    g_save.failed = true;
    g_save.finished = true;
  }
  orcsdr::sstv::set_audio_observer(nullptr);
  orcsdr::sstv_audio_scope::stop();
  orcsdr::sstv_capture::release();
  orcsdr::sstv::stop();                              // an image in progress becomes a partial entry
  (void)arc::poll(utc());                            // queue it now; the worker keeps running
  export_service();                                  // include that last picture in the web export
  orcsdr::sstv::ui_leave();
  if (g_hooks.release_tuning) g_hooks.release_tuning();
}

void tick() {
  {   // runs with the dashboard closed too (the archive filename can arrive after it closes), four times a second
    static uint32_t last_ms = 0;
    if (millis() - last_ms >= 250u) {
      last_ms = millis();
      export_service();
    }
  }
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
  capture_save_step();                             // a recording being written out, a few KB per tick
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
uint32_t if_bandwidth_hz() { return kIfWidths[g_tune.if_index]; }
void cycle_if_bandwidth() {
  if (!active()) return;
  g_tune.if_index = static_cast<uint8_t>((g_tune.if_index + 1) % std::size(kIfWidths));
  apply_demod();
  save_tune();
}
bool tune_command(const char* args, char* reply, size_t size) {
  if (!active()) { std::snprintf(reply, size, "dashboard_not_open use=SSTV OPEN"); return false; }
  if (std::strncmp(args, "REC ", 4) == 0) {
    namespace cap = orcsdr::sstv_capture;
    const char* v = args + 4;
    if (std::strncmp(v, "START ", 6) == 0) {
      const unsigned long seconds = std::strtoul(v + 6, nullptr, 10);
      if (g_save.file) { std::snprintf(reply, size, "rec_start_refused save_in_progress"); return false; }   // the save reads the same buffer
      if (!cap::start(static_cast<uint32_t>(seconds))) { std::snprintf(reply, size, "rec_start_refused seconds=1..%u", static_cast<unsigned>(cap::kMaxSeconds)); return false; }
      std::snprintf(reply, size, "rec=started seconds=%lu rate=%u", seconds, static_cast<unsigned>(cap::kSampleRate));
      return true;
    }
    if (std::strcmp(v, "STATUS") == 0) {
      const char* state = cap::recording() ? "recording" : g_save.file ? "saving" : g_save.finished ? (g_save.failed ? "save_failed" : "saved") : "idle";
      std::snprintf(reply, size, "rec=%s samples=%lu full=%d file=%s", state, static_cast<unsigned long>(cap::samples()), cap::full() ? 1 : 0,
                    g_save.finished && !g_save.failed ? g_save.path + 3 : "-");
      return true;
    }
    if (std::strcmp(v, "SAVE") == 0) {
      cap::stop();
      if (!cap::samples()) { std::snprintf(reply, size, "rec_nothing_recorded"); return false; }
      if (!g_hooks.sd_ready || !g_hooks.sd_ready(true)) { std::snprintf(reply, size, "no_sd_card"); return false; }
      if (g_save.file) { std::snprintf(reply, size, "rec_save_already_running"); return false; }
      g_save = CaptureSave{};
      std::snprintf(g_save.path, sizeof(g_save.path), "/sd/orcsdr/sstv/cap_%lu.wav", static_cast<unsigned long>(millis() / 1000u));
      g_save.file = std::fopen(g_save.path, "wb");
      if (!g_save.file) { std::snprintf(reply, size, "cannot_create_file"); return false; }
      std::snprintf(reply, size, "rec=saving file=%s samples=%lu", g_save.path + 3, static_cast<unsigned long>(cap::samples()));
      return true;
    }
    std::snprintf(reply, size, "usage=SSTV REC START <seconds>|SAVE|STATUS");
    return false;
  }
  if (std::strncmp(args, "MODE ", 5) == 0) {
    const char* v = args + 5;
    int choice = -1;
    if (std::strcmp(v, "AUTO") == 0) choice = 0;
    else if (std::strcmp(v, "NFM") == 0) choice = 1;
    else if (std::strcmp(v, "USB") == 0) choice = 2;
    else if (std::strcmp(v, "LSB") == 0) choice = 3;
    if (choice < 0) { std::snprintf(reply, size, "usage=SSTV MODE AUTO|NFM|USB|LSB"); return false; }
    g_tune.demod_choice = static_cast<uint8_t>(choice);
    apply_demod();
    save_tune();
    std::snprintf(reply, size, "mode=%s", demod_label());
    return true;
  }
  if (std::strncmp(args, "IFBW ", 5) == 0) {
    char* end = nullptr;
    const unsigned long hz = std::strtoul(args + 5, &end, 10);
    if (std::strcmp(args + 5, "DEFAULT") == 0) { g_tune.if_index = 0; apply_demod(); save_tune(); std::snprintf(reply, size, "if_hz=%lu if_default=1", static_cast<unsigned long>(g_hooks.if_bandwidth ? g_hooks.if_bandwidth() : 0)); return true; }
    if (end == args + 5 || *end || hz == 0) { std::snprintf(reply, size, "usage=SSTV IFBW <hz 8000..100000>|DEFAULT"); return false; }
    size_t best = 1;
    for (size_t i = 2; i < std::size(kIfWidths); ++i)
      if (std::labs(static_cast<long>(kIfWidths[i]) - static_cast<long>(hz)) < std::labs(static_cast<long>(kIfWidths[best]) - static_cast<long>(hz))) best = i;
    g_tune.if_index = static_cast<uint8_t>(best);
    apply_demod();
    save_tune();
    std::snprintf(reply, size, "if_hz=%lu if_default=%d", static_cast<unsigned long>(g_hooks.if_bandwidth ? g_hooks.if_bandwidth() : kIfWidths[best]), kIfWidths[best] == 0 ? 1 : 0);
    return true;
  }
  if (std::strncmp(args, "STEP ", 5) == 0) {
    char* end = nullptr;
    const unsigned long hz = std::strtoul(args + 5, &end, 10);
    if (end == args + 5 || *end || !hz) { std::snprintf(reply, size, "usage=SSTV STEP <hz>"); return false; }
    g_tune.step_index = static_cast<uint8_t>(tn::step_index(static_cast<uint32_t>(hz)));
    save_tune();
    std::snprintf(reply, size, "step_hz=%lu", static_cast<unsigned long>(tn::step_hz(g_tune.step_index)));
    return true;
  }
  if (std::strncmp(args, "RAILS ", 6) == 0) {
    if (std::strcmp(args + 6, "ON") == 0) g_tune.rails = true;
    else if (std::strcmp(args + 6, "OFF") == 0) g_tune.rails = false;
    else { std::snprintf(reply, size, "usage=SSTV RAILS ON|OFF"); return false; }
    save_tune();
    std::snprintf(reply, size, "rails=%d", g_tune.rails ? 1 : 0);
    return true;
  }
  std::snprintf(reply, size, "unknown");
  return false;
}
void tune_summary(char* out, size_t size) {
  std::snprintf(out, size, "frequency_hz=%lu demod=\"%s\" step_hz=%lu if_hz=%lu if_default=%d rails=%d", static_cast<unsigned long>(g_frequency_hz),
                demod_label(), static_cast<unsigned long>(tn::step_hz(g_tune.step_index)),
                static_cast<unsigned long>(g_hooks.if_bandwidth ? g_hooks.if_bandwidth() : kIfWidths[g_tune.if_index]), kIfWidths[g_tune.if_index] == 0 ? 1 : 0, g_tune.rails ? 1 : 0);
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
