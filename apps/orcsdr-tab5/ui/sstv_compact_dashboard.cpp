// Compact SSTV dashboard: implements the orcsdr::sstv ui_* contract (sstv_dashboard.hpp) for views smaller than 1280 x 720, in place of
// sstv_dashboard.cpp (a build links one of the two). Pages: RECEIVE (live picture and status), IMAGES (gallery), TUNE (profiles, VFO, gain,
// demodulator, IF width), SETUP (detection, autosave). Not on this view yet: the ANALYZER page, the RF spectrum and tap-to-tune, the
// keypad entry of a frequency, the full-size viewer of a gallery picture. Everything shown comes from the receiver; no synthetic data.
#include "sstv_compact_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include "sstv_status.hpp"
#include "sstv_tune.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace orcsdr::sstv {
namespace cmp = compact;
namespace {

constexpr uint16_t kPanel = 0x0841, kGrid = 0x2945, kCyan = 0x2e7f, kGreen = 0x6fe8, kYellow = 0xff24, kAmber = 0xfd20, kMuted = 0x8c71,
                   kSelected = 0x1264, kImageBg = 0x0021;
constexpr size_t kLineCapacity = 1024;

struct State {
  cmp::Layout layout;
  UiSnapshot ui{};
  Page page = Page::receive;
  uint8_t profile = 0;
  size_t gallery_page = 0;
  size_t selected = 0;                 // gallery index (0 = newest)
  char notice[48]{};
  uint32_t notice_ms = 0;
  // Incremental picture painting (rows are pushed once, never a whole frame on a timer).
  const uint16_t* painted_source = nullptr;
  int painted_rows = 0;
  uint32_t last_revision = 0;
  char last_info[256]{};
  size_t last_archive_count = SIZE_MAX;
  uint16_t line[kLineCapacity];
  bool active = false;
};
State* g_state = nullptr;   // PSRAM, created by ui_set_viewport(): this screen costs no internal RAM when it is not open
void (*g_header_hook)() = nullptr;
float (*g_rf_level)() = nullptr;

// RF level text: the measured IQ level, or a dash when no source is installed.
void rf_text(char* out, size_t size) {
  if (g_rf_level) std::snprintf(out, size, "RF %.0f dBFS", static_cast<double>(g_rf_level()));
  else std::snprintf(out, size, "RF --");
}

bool hit(int32_t x, int32_t y, cmp::Rect r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

void text(const char* value, int x, int y, uint16_t color, int size, textdatum_t datum) {
  M5.Display.setTextDatum(datum);
  if (size <= 1) {
    M5.Display.setFont(size == 1 ? &fonts::DejaVu24 : &fonts::DejaVu18);
    M5.Display.setTextSize(1);
  } else {
    M5.Display.setFont(nullptr);
    M5.Display.setTextSize(size);
  }
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
  if (size <= 1) M5.Display.setFont(nullptr);
}

void fit_text(const char* value, cmp::Rect r, uint16_t color, textdatum_t datum = middle_center) {
  M5.Display.setFont(&fonts::DejaVu24);
  M5.Display.setTextSize(1);
  const bool fits = M5.Display.textWidth(value) <= r.w - 8;
  M5.Display.setFont(nullptr);
  const int x = datum == middle_left ? r.x + 6 : datum == middle_right ? r.x + r.w - 6 : r.x + r.w / 2;
  text(value, x, r.y + r.h / 2, color, fits ? 1 : 0, datum);
}

void frame(cmp::Rect r, uint16_t border = kGrid) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
}

void button(cmp::Rect r, const char* label, bool selected = false, bool enabled = true) {
  const uint16_t border = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  fit_text(label, r, enabled ? (selected ? kGreen : TFT_WHITE) : kMuted);
}

uint16_t tone_color(sstv_status::Tone tone) {
  switch (tone) {
    case sstv_status::Tone::good: return kGreen;
    case sstv_status::Tone::warn: return kAmber;
    case sstv_status::Tone::info: return kCyan;
    case sstv_status::Tone::idle: break;
  }
  return kMuted;
}

// Scales `image` (rows 0..rows-1) into `area` with the picture centred and aspect kept. Paints only rows not yet painted for this source.
void paint_image_rows(State& st, const Image& image, cmp::Rect area, bool force) {
  if (!image.pixels || !image.width || !image.height) return;
  const int view_w = area.w - 8, view_h = area.h - 8;
  const float fit = std::min(static_cast<float>(view_w) / image.width, static_cast<float>(view_h) / image.height);
  const int dw = std::max(1, std::min({view_w, static_cast<int>(fit * image.width), static_cast<int>(kLineCapacity)}));
  const int dh = std::max(1, std::min(view_h, static_cast<int>(fit * image.height)));
  const int left = area.x + (area.w - dw) / 2, top = area.y + (area.h - dh) / 2;
  const uint16_t rows = image.partial ? image.decoded_rows : image.height;
  if (force || st.painted_source != image.pixels || static_cast<int>(rows) < st.painted_rows) {
    M5.Display.fillRect(area.x + 2, area.y + 2, area.w - 4, area.h - 4, kImageBg);
    st.painted_source = image.pixels;
    st.painted_rows = 0;
  }
  for (int dy = st.painted_rows; dy < dh; ++dy) {
    const uint16_t sy = static_cast<uint16_t>(static_cast<uint32_t>(dy) * image.height / dh);
    if (sy >= rows) break;
    for (int dx = 0; dx < dw; ++dx) {
      const uint16_t sx = static_cast<uint16_t>(static_cast<uint32_t>(dx) * image.width / dw);
      st.line[dx] = image.pixels[static_cast<size_t>(sy) * image.width + sx];
    }
    // Native RGB565 row: push it as rgb565_t (a uint16_t* image is read as big-endian and shows wrong hues).
    M5.Display.pushImage(left, top + dy, dw, 1, reinterpret_cast<const lgfx::rgb565_t*>(st.line));
    st.painted_rows = dy + 1;
  }
}

// A static, complete scaled copy of a picture into `area` (gallery thumbnails).
void paint_thumbnail(State& st, const Image& image, cmp::Rect area) {
  M5.Display.fillRect(area.x + 2, area.y + 2, area.w - 4, area.h - 4, kImageBg);
  if (!image.pixels || !image.width || !image.height) return;
  const int view_w = area.w - 8, view_h = area.h - 28;
  const float fit = std::min(static_cast<float>(view_w) / image.width, static_cast<float>(view_h) / image.height);
  const int dw = std::max(1, std::min({view_w, static_cast<int>(fit * image.width), static_cast<int>(kLineCapacity)}));
  const int dh = std::max(1, std::min(view_h, static_cast<int>(fit * image.height)));
  const int left = area.x + (area.w - dw) / 2, top = area.y + 4;
  const uint16_t rows = image.partial ? image.decoded_rows : image.height;
  for (int dy = 0; dy < dh; ++dy) {
    const uint16_t sy = static_cast<uint16_t>(static_cast<uint32_t>(dy) * image.height / dh);
    if (sy >= rows) break;
    for (int dx = 0; dx < dw; ++dx) st.line[dx] = image.pixels[static_cast<size_t>(sy) * image.width + static_cast<uint16_t>(static_cast<uint32_t>(dx) * image.width / dw)];
    M5.Display.pushImage(left, top + dy, dw, 1, reinterpret_cast<const lgfx::rgb565_t*>(st.line));
  }
}

const Image* newest_archived(Image* storage) {
  *storage = archived_image(0);
  return storage->pixels ? storage : nullptr;
}

// ---------------------------------------------------------------- RECEIVE

void info_text(State& st, char* out, size_t size, uint16_t* color) {
  const auto m = metrics();
  Image last;
  const auto head = sstv_status::headline(m, newest_archived(&last));
  *color = tone_color(head.tone);
  const ModeSpec* spec = m.receiving ? mode_spec(m.received_mode) : nullptr;
  char mode[64];
  if (spec) std::snprintf(mode, sizeof(mode), "%s  %u x %u  row %u/%u", spec->name, spec->width, spec->height, static_cast<unsigned>(m.row), static_cast<unsigned>(m.rows));
  else std::snprintf(mode, sizeof(mode), "waiting for a transmission");
  char rf[24];
  rf_text(rf, sizeof(rf));
  std::snprintf(out, size, "%s\n%s\n%.6f MHz   %s\n%s   tone %.0f Hz\n%s", head.text, mode, static_cast<double>(st.ui.frequency_hz) / 1e6,
                rf, state_name(m.state), static_cast<double>(m.tone_hz), st.notice);
}

void draw_info(State& st, bool force) {
  char text_block[256];
  uint16_t color;
  info_text(st, text_block, sizeof(text_block), &color);
  if (!force && std::strncmp(text_block, st.last_info, sizeof(st.last_info) - 1) == 0) return;
  std::snprintf(st.last_info, sizeof(st.last_info), "%s", text_block);
  const cmp::Rect r = st.layout.info;
  frame(r);
  int y = r.y + 16;
  const int step = std::max(20, (r.h - 24) / 5);
  char* line = text_block;
  for (int i = 0; i < 5 && line; ++i) {
    char* next = std::strchr(line, '\n');
    if (next) *next = 0;
    if (line[0]) text(line, r.x + 10, y, i == 0 ? color : (i == 4 ? kAmber : TFT_WHITE), 0, middle_left);
    y += step;
    line = next ? next + 1 : nullptr;
  }
}

void draw_receive(State& st, bool force) {
  const cmp::Layout& l = st.layout;
  if (force) {
    frame(l.image);
    button(l.abort_button, "ABORT");
    button(l.save_button, "SAVE");
  }
  draw_info(st, force);
  const Image live = live_image();
  const Image shown = live.pixels ? live : archived_image(0);
  if (!shown.pixels) {
    if (force) text("WAITING FOR AN IMAGE", l.image.x + l.image.w / 2, l.image.y + l.image.h / 2, kMuted, 0, middle_center);
    return;
  }
  paint_image_rows(st, shown, l.image, force);
}

// ---------------------------------------------------------------- IMAGES

void draw_gallery(State& st) {
  const cmp::Layout& l = st.layout;
  const size_t count = archive_count();
  st.last_archive_count = count;
  const size_t pages = (count + cmp::kGalleryCells - 1) / cmp::kGalleryCells;
  if (st.gallery_page >= std::max<size_t>(pages, 1)) st.gallery_page = pages ? pages - 1 : 0;
  for (int i = 0; i < cmp::kGalleryCells; ++i) {
    const size_t index = st.gallery_page * cmp::kGalleryCells + static_cast<size_t>(i);
    frame(l.cell[i], index == st.selected && index < count ? kGreen : kGrid);
    if (index >= count) {
      if (i == 0 && count == 0) text("no images yet", l.cell[i].x + l.cell[i].w / 2, l.cell[i].y + l.cell[i].h / 2, kMuted, 0, middle_center);
      continue;
    }
    const Image im = archived_image(index);
    paint_thumbnail(st, im, l.cell[i]);
    const ModeSpec* spec = mode_spec(im.mode);
    char label[48];
    std::snprintf(label, sizeof(label), "%s%s", spec ? spec->name : "?", im.partial ? " partial" : "");
    text(label, l.cell[i].x + l.cell[i].w / 2, l.cell[i].y + l.cell[i].h - 12, index == st.selected ? kGreen : kMuted, 0, middle_center);
  }
  char label[24];
  std::snprintf(label, sizeof(label), "%u/%u", static_cast<unsigned>(st.gallery_page + 1), static_cast<unsigned>(std::max<size_t>(pages, 1)));
  button(l.gallery_prev, "PREV", false, st.gallery_page > 0);
  frame(l.gallery_label);
  fit_text(label, l.gallery_label, kMuted);
  button(l.gallery_next, "NEXT", false, st.gallery_page + 1 < pages);
  button(l.gallery_save, "SAVE", false, count > 0 && st.ui.sd_ready);
  button(l.gallery_clear, "CLEAR", false, count > 0);
}

// ---------------------------------------------------------------- TUNE

void draw_tune(State& st) {
  const cmp::Layout& l = st.layout;
  for (int i = 0; i < cmp::kProfileCount; ++i) button(l.profile[i], kProfiles[i].name, kProfiles[i].hz == st.ui.frequency_hz);   // lit when the dial is on that profile
  char value[64];
  frame(l.frequency);
  std::snprintf(value, sizeof(value), "%.6f MHz", static_cast<double>(st.ui.frequency_hz) / 1e6);
  fit_text(value, l.frequency, TFT_WHITE);
  const uint32_t step = sstv_tune::step_hz(st.ui.tune.step_index);
  button(l.nudge_down, "- STEP");
  button(l.nudge_up, "+ STEP");
  if (step >= 1000) std::snprintf(value, sizeof(value), "STEP %g kHz", step / 1000.0);
  else std::snprintf(value, sizeof(value), "STEP %u Hz", static_cast<unsigned>(step));
  button(l.step_button, value);
  std::snprintf(value, sizeof(value), "%s%s", sstv_tune::demod_name(static_cast<sstv_tune::Demod>(st.ui.tune.demod)), st.ui.tune.demod_auto ? " (AUTO)" : "");
  button(l.demod_button, value);
  if (st.ui.tune.gain_available) {
    button(l.gain_auto, st.ui.tune.gain_auto ? "GAIN AUTO" : "GAIN MAN", st.ui.tune.gain_auto);
    std::snprintf(value, sizeof(value), "- (%.1f)", st.ui.tune.gain_tenth_db / 10.0);
    button(l.gain_down, value, false, true);
    button(l.gain_up, "+", false, true);
  } else {
    button(l.gain_auto, "NO GAIN", false, false);
    button(l.gain_down, "-", false, false);
    button(l.gain_up, "+", false, false);
  }
  button(l.if_button, "IF: SET BY THE DEMODULATOR", false, false);   // the IF filter choice is not available on this device
  frame(l.level);
  char rf[24];
  rf_text(rf, sizeof(rf));
  std::snprintf(value, sizeof(value), "%s   %s   %u S/s", rf, st.ui.streaming ? "STREAMING" : "NOT STREAMING", static_cast<unsigned>(st.ui.tune.rate_sps));
  text(value, l.level.x + 10, l.level.y + l.level.h / 2, st.ui.streaming ? kGreen : kAmber, 0, middle_left);
}

// ---------------------------------------------------------------- SETUP

const char* detection_label(sstv_settings::Detection d) {
  switch (d) {
    case sstv_settings::Detection::automatic: return "DETECTION: AUTOMATIC";
    case sstv_settings::Detection::vis_only: return "DETECTION: VIS ONLY";
    case sstv_settings::Detection::manual: return "DETECTION: MANUAL";
  }
  return "DETECTION";
}

void draw_setup(State& st) {
  const cmp::Layout& l = st.layout;
  button(l.detection_button, detection_label(st.ui.settings.detection));
  char value[64];
  const ModeSpec* spec = mode_spec(st.ui.settings.manual_mode);
  std::snprintf(value, sizeof(value), "MODE: %s", spec ? spec->name : "?");
  button(l.mode_button, value, st.ui.settings.detection == sstv_settings::Detection::manual);
  button(l.autosave_button, st.ui.settings.autosave ? "AUTOSAVE: ON" : "AUTOSAVE: OFF", st.ui.settings.autosave);
  button(l.clear_button, "CLEAR GALLERY", false, archive_count() > 0);
  frame(l.setup_note);
  text(st.ui.sd_ready ? "SD card ready: images save to the card" : "No SD card: images are kept in memory only", l.setup_note.x + 10, l.setup_note.y + l.setup_note.h / 2,
       st.ui.sd_ready ? kGreen : kAmber, 0, middle_left);
}

// ---------------------------------------------------------------- frame

void draw_tabs(State& st) {
  static constexpr const char* labels[cmp::kTabCount] = {"RECEIVE", "IMAGES", "TUNE", "SETUP"};
  static constexpr Page pages[cmp::kTabCount] = {Page::receive, Page::gallery, Page::tune, Page::setup};
  for (int i = 0; i < cmp::kTabCount; ++i) button(st.layout.tab[i], labels[i], st.page == pages[i]);
}

void draw_header(State& st) {
  const auto& header = audio_header::layout();
  M5.Display.fillRect(st.layout.viewport.x, st.layout.viewport.y, st.layout.viewport.w, header.content_top - st.layout.viewport.y, TFT_BLACK);
  audio_header::draw_brand("SSTV");
  if (header.status.w >= 100) text("SSTV RECEIVE", header.status.x + 8, header.status.y + header.status.h / 2, TFT_WHITE, header.status.w >= 200 ? 1 : 0, middle_left);
  M5.Display.drawFastHLine(st.layout.viewport.x + 8, header.content_top - 4, st.layout.viewport.w - 16, kGreen);
  if (g_header_hook != nullptr) g_header_hook();
}

void draw_body(State& st, bool force) {
  if (force) M5.Display.fillRect(st.layout.content.x, st.layout.content.y, st.layout.content.w, st.layout.content.h, TFT_BLACK);
  switch (st.page) {
    case Page::receive: draw_receive(st, force); break;
    case Page::gallery: draw_gallery(st); break;
    case Page::tune: draw_tune(st); break;
    case Page::setup: draw_setup(st); break;
    case Page::analyzer:
    case Page::count: break;
  }
}

void show_page(State& st, Page page) {
  st.page = page;
  st.painted_source = nullptr;
  st.painted_rows = 0;
  st.last_info[0] = 0;
  draw_tabs(st);
  draw_body(st, true);
}

}  // namespace

bool ui_set_viewport(cmp::Rect viewport) {
  const cmp::Layout next = cmp::make_layout(viewport);
  if (!next.valid || !audio_header::set_viewport(viewport)) return false;
  if (g_state == nullptr) {
    void* memory = heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == nullptr) return false;
    g_state = new (memory) State();
  }
  g_state->layout = next;
  return true;
}

void set_header_hook(void (*draw_controls)()) { g_header_hook = draw_controls; }
void ui_set_rf_level_source(float (*level_dbfs)()) { g_rf_level = level_dbfs; }

void ui_enter(const UiSnapshot& snapshot) {
  if (g_state == nullptr || !g_state->layout.valid) return;
  State& st = *g_state;
  st.ui = snapshot;
  st.active = true;
  st.page = Page::receive;
  st.painted_source = nullptr;
  st.painted_rows = 0;
  st.last_info[0] = 0;
  st.selected = 0;
  M5.Display.fillRect(st.layout.viewport.x, st.layout.viewport.y, st.layout.viewport.w, st.layout.viewport.h, TFT_BLACK);
  draw_header(st);
  draw_tabs(st);
  draw_body(st, true);
}

void ui_leave() {
  if (g_state == nullptr) return;
  g_state->active = false;
  g_state->~State();
  heap_caps_free(g_state);
  g_state = nullptr;
}

bool ui_active() { return g_state != nullptr && g_state->active; }

void ui_update(const UiSnapshot& snapshot) {
  if (!ui_active()) return;
  State& st = *g_state;
  const bool tune_changed = st.ui.tune != snapshot.tune || st.ui.frequency_hz != snapshot.frequency_hz || st.ui.streaming != snapshot.streaming;
  const bool settings_changed = std::memcmp(&st.ui.settings, &snapshot.settings, sizeof(snapshot.settings)) != 0 || st.ui.sd_ready != snapshot.sd_ready;
  st.ui = snapshot;
  if (st.notice[0] && static_cast<uint32_t>(millis() - st.notice_ms) > 6000u) {
    st.notice[0] = 0;
    st.last_info[0] = 0;
  }
  switch (st.page) {
    case Page::receive: draw_receive(st, false); break;
    case Page::gallery:
      if (archive_count() != st.last_archive_count) draw_gallery(st);
      break;
    case Page::tune:
      if (tune_changed) draw_tune(st);
      break;
    case Page::setup:
      if (settings_changed) draw_setup(st);
      break;
    case Page::analyzer:
    case Page::count: break;
  }
}

Action ui_touch(int32_t x, int32_t y) {
  if (!ui_active()) return {};
  State& st = *g_state;
  const cmp::Layout& l = st.layout;
  static constexpr Page pages[cmp::kTabCount] = {Page::receive, Page::gallery, Page::tune, Page::setup};
  for (int i = 0; i < cmp::kTabCount; ++i)
    if (hit(x, y, l.tab[i])) {
      show_page(st, pages[i]);
      return {};
    }
  switch (st.page) {
    case Page::receive:
      if (hit(x, y, l.abort_button)) return {ActionKind::abort_rx, 0};
      if (hit(x, y, l.save_button)) return {ActionKind::save_image, -1};
      break;
    case Page::gallery: {
      const size_t count = archive_count();
      const size_t pages_total = (count + cmp::kGalleryCells - 1) / cmp::kGalleryCells;
      if (hit(x, y, l.gallery_prev) && st.gallery_page > 0) {
        --st.gallery_page;
        draw_gallery(st);
      } else if (hit(x, y, l.gallery_next) && st.gallery_page + 1 < pages_total) {
        ++st.gallery_page;
        draw_gallery(st);
      } else if (hit(x, y, l.gallery_save) && count > 0) {
        return {ActionKind::save_image, static_cast<int32_t>(st.selected)};
      } else if (hit(x, y, l.gallery_clear) && count > 0) {
        st.selected = 0;
        return {ActionKind::clear_gallery, 0};
      } else {
        for (int i = 0; i < cmp::kGalleryCells; ++i) {
          const size_t index = st.gallery_page * cmp::kGalleryCells + static_cast<size_t>(i);
          if (hit(x, y, l.cell[i]) && index < count) {
            st.selected = index;
            draw_gallery(st);
            break;
          }
        }
      }
      break;
    }
    case Page::tune:
      for (int i = 0; i < cmp::kProfileCount; ++i)
        if (hit(x, y, l.profile[i])) {
          st.profile = static_cast<uint8_t>(i);
          return {ActionKind::tune_profile, i};
        }
      if (hit(x, y, l.nudge_down)) return {ActionKind::nudge, -1};
      if (hit(x, y, l.nudge_up)) return {ActionKind::nudge, 1};
      if (hit(x, y, l.step_button)) return {ActionKind::set_step, static_cast<int32_t>((st.ui.tune.step_index + 1) % sstv_tune::step_count())};
      if (hit(x, y, l.demod_button)) return {ActionKind::cycle_demod, 0};
      if (st.ui.tune.gain_available) {
        if (hit(x, y, l.gain_auto)) return {ActionKind::gain_toggle, 0};
        if (hit(x, y, l.gain_down)) return {ActionKind::gain_step, -1};
        if (hit(x, y, l.gain_up)) return {ActionKind::gain_step, 1};
      }
      break;
    case Page::setup: {
      using D = sstv_settings::Detection;
      if (hit(x, y, l.detection_button)) {
        const D next = st.ui.settings.detection == D::automatic ? D::vis_only : D::automatic;   // manual is entered by choosing a mode
        return {ActionKind::set_detection, static_cast<int32_t>(next)};
      }
      if (hit(x, y, l.mode_button)) {
        uint8_t ordinal = static_cast<uint8_t>(st.ui.settings.manual_mode);
        ordinal = ordinal >= 14 || ordinal < 1 ? 1 : static_cast<uint8_t>(ordinal + 1);
        return {ActionKind::select_mode, ordinal};
      }
      if (hit(x, y, l.autosave_button)) return {ActionKind::toggle_autosave, 0};
      if (hit(x, y, l.clear_button) && archive_count() > 0) return {ActionKind::clear_gallery, 0};
      break;
    }
    case Page::analyzer:
    case Page::count: break;
  }
  return {};
}

Page ui_page() { return g_state != nullptr ? g_state->page : Page::receive; }
uint8_t ui_profile() { return g_state != nullptr ? g_state->profile : 0; }
void ui_set_profile(uint8_t index) {
  if (g_state == nullptr) return;
  g_state->profile = index < cmp::kProfileCount ? index : 0;
  if (g_state->active && g_state->page == Page::tune) draw_tune(*g_state);
}
void ui_set_settings(const sstv_settings::Settings& settings) {
  if (!ui_active()) return;
  g_state->ui.settings = settings;
  if (g_state->page == Page::setup) draw_setup(*g_state);
}
void ui_set_tune(const TuneView& tune) {
  if (!ui_active()) return;
  g_state->ui.tune = tune;
  if (g_state->page == Page::tune) draw_tune(*g_state);
}
void ui_notice(const char* message) {
  if (!ui_active() || message == nullptr) return;
  std::snprintf(g_state->notice, sizeof(g_state->notice), "%.47s", message);
  g_state->notice_ms = millis();
  g_state->last_info[0] = 0;
  if (g_state->page == Page::receive) draw_info(*g_state, true);
}
bool ui_wants_spectrum() { return false; }   // no RF spectrum on this view yet
void ui_feed_spectrum(const float*, size_t, uint32_t) {}
UiStats ui_stats() { return {}; }

}  // namespace orcsdr::sstv
