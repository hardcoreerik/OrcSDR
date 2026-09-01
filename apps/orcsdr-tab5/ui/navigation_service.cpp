#include "navigation_service.hpp"

#include <M5Unified.h>

#include <cmath>
#include <iterator>

#include "adsb_dashboard.hpp"
#include "fm_dashboard.hpp"
#include "lora_dashboard.hpp"
#include "p25_dashboard.hpp"
#include "rf24_dashboard.hpp"

namespace orcsdr::navigation {
namespace {
Hooks g_hooks{};
bool g_restore_graphics = true;

bool ready() {
  return g_hooks.home_snapshot && g_hooks.sync_audio && g_hooks.audio_enabled &&
         g_hooks.volume && g_hooks.ensure_speaker_running && g_hooks.close_overlays &&
         g_hooks.settings_state && g_hooks.disable_graphics && g_hooks.restore_graphics &&
         g_hooks.restore_screen;
}
}  // namespace

void configure(const Hooks& hooks) { g_hooks = hooks; }

void show_home(bool demo) {
  if (!ready()) return;
  screens::begin_transition(screens::Id::home, millis());
  fm::leave();
  p25::leave();
  adsb::leave();
  lora::leave();
  rf24::leave();
  settings::leave();
  g_hooks.close_overlays();
  g_hooks.sync_audio();
  if (g_hooks.audio_enabled()) (void)g_hooks.ensure_speaker_running(g_hooks.volume());
  M5.Display.fillScreen(TFT_BLACK);
  home::enter(g_hooks.home_snapshot(demo));
  if (demo) {
    float levels[256];
    for (size_t i = 0; i < std::size(levels); ++i) {
      const float center = static_cast<float>(static_cast<int>(i) - 128);
      const float carrier = 72.0f * expf(-(center * center) / 70.0f);
      const float side = 20.0f * expf(-((center - 52.0f) * (center - 52.0f)) / 18.0f);
      levels[i] = -105.0f + carrier + side + 4.0f * sinf(i * 0.39f);
    }
    home::draw_spectrum(levels, 0, std::size(levels), -105.0f);
  }
  screens::finish_transition();
}

void draw_home() {
  if (!ready() || !screens::may_draw(screens::Id::home) || !home::active()) return;
  screens::note_visible_update(screens::Id::home);
  home::update(g_hooks.home_snapshot(false));
}

void open_settings(settings::Section section) {
  if (!ready()) return;
  if (g_hooks.persist_settings_open) g_hooks.persist_settings_open();
  screens::begin_transition(screens::Id::settings, millis(), true);
  g_restore_graphics = g_hooks.disable_graphics();
  g_hooks.close_overlays();
  M5.Display.fillScreen(TFT_BLACK);
  settings::enter(g_hooks.settings_state(), section);
  screens::finish_transition();
}

void close_settings() {
  if (!ready()) return;
  g_hooks.restore_graphics(g_restore_graphics);
  const auto restore = screens::close_settings(millis());
  settings::leave();
  M5.Display.fillScreen(TFT_BLACK);
  screens::finish_transition();
  g_hooks.restore_screen(restore);
}

}  // namespace orcsdr::navigation
