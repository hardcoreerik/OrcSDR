// ACARS dashboard (apps/orcsdr-tab5/ui/acars_*.cpp, unchanged) on the P4 console: VHF aircraft datalink, decoded on the
// receiver's 240 kS/s stream. Message saving to SD is not available yet (the archive then holds this session only).
#include "acars_app.hpp"
#include "acars_receiver.hpp"
#include "audio_service.hpp"
#include "home_app.hpp"
#include "radio_service.hpp"
#include "screens.hpp"
#include "ui_support.hpp"

namespace {
void iq_sink(const uint8_t* iq, size_t bytes) {
  orcsdr::acars_receiver::offer_iq(iq, bytes, orcsdr::radio::sample_rate());
}

orcsdr::acars_app::Hooks app_hooks() {
  orcsdr::acars_app::Hooks h;
  h.header_controls = ui_support::draw_header;
  h.sound_enabled = ui_support::sound_enabled;
  h.overlay_open = ui_support::header_overlay_open;
  h.archive_ops = nullptr;
  h.store.get_bool = ui_support::get_bool;
  h.store.put_bool = ui_support::put_bool;
  h.store.get_u8 = ui_support::get_u8;
  h.store.put_u8 = ui_support::put_u8;
  h.store.get_u32 = ui_support::get_u32;
  h.store.put_u32 = ui_support::put_u32;
  return h;
}

orcsdr::acars_receiver::Hooks receiver_hooks() {
  orcsdr::acars_receiver::Hooks h;
  h.device_ready = [] { return orcsdr::radio::snapshot().receiver_state == 2; };
  h.tune = [](uint32_t center_hz) {
    if (orcsdr::radio::snapshot().receiver_state != 2) return false;
    orcsdr::radio::set_sample_rate_override(orcsdr::acars_receiver::kStreamRate);
    home_app::request_frequency(center_hz);
    return true;
  };
  h.release = [] { orcsdr::radio::set_sample_rate_override(0); };
  h.sample_rate = [] { return orcsdr::radio::sample_rate(); };
  h.tuned_center = [] { return home_app::status().frequency_hz; };
  h.now_ms = ui_support::now_ms;
  h.utc_epoch = ui_support::utc_epoch;
  return h;
}

void enter() {
  audio_service::set_active(false);  // ACARS is text: no audio
  ui_support::header_reset();
  ui_support::claim_screen(orcsdr::screens::Id::acars);
  orcsdr::acars_receiver::install(receiver_hooks());
  orcsdr::acars_app::install(app_hooks());
  orcsdr::acars_app::open();
  orcsdr::radio::set_aux_iq_sink(iq_sink);
}
void leave() {
  orcsdr::radio::set_aux_iq_sink(nullptr);
  orcsdr::acars_app::close();
  orcsdr::radio::set_sample_rate_override(0);
}
void tick() {
  orcsdr::acars_app::tick();
  ui_support::header_service();
}
void tap(int x, int y) {
  if (ui_support::handle_header_touch(x, y)) return;
  orcsdr::acars_app::touch(x, y);
}
}  // namespace

namespace screens {
const Screen& acars_screen() {
  static const Screen s{orcsdr::dashboards::Id::acars, "ACARS", enter, leave, tick, tap, nullptr, nullptr};
  return s;
}
}  // namespace screens
