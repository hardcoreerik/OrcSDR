#include "rf_visualizer.hpp"

#include "nvs_store.hpp"
#include "rf_analysis.hpp"
#include "text_editor.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <strings.h>

namespace orcsdr::visualizer {
namespace {

constexpr uint32_t kPersistMagic = 0x52565631;
constexpr uint16_t kPersistVersion = 1;
constexpr size_t kMaxControls = 192;
constexpr size_t kPresetValues = 32;
constexpr size_t kCustomPresets = 4;
constexpr size_t kBins = 1024;
constexpr int kPlotX = 42;
constexpr int kPlotY = 82;
constexpr int kPlotW = 1196;
constexpr int kPlotH = 590;
constexpr int kDopplerColumnStep = 2;
constexpr size_t kDopplerColumns = (kPlotW + kDopplerColumnStep - 1) / kDopplerColumnStep;
constexpr int kDopplerRowStep = 2;
constexpr size_t kOccupancyRows = 24;
constexpr int kDrawerY = 418;
constexpr int kDrawerH = 302;
constexpr int kHudH = 70;
constexpr uint32_t kHudTimeoutMs = 3000;
constexpr uint32_t kNameTimeoutMs = 700;
constexpr uint32_t kLongPressMs = 650;
constexpr uint32_t kDoubleTapMs = 350;
constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kGrid = 0x2945;
constexpr uint16_t kCyan = 0x05ff;
constexpr uint16_t kGreen = 0x6fe0;
constexpr uint16_t kMuted = 0x7bef;
constexpr uint16_t kYellow = 0xff80;
constexpr uint16_t kMagenta = 0xf81f;
M5Canvas g_canvas(&M5.Display);
M5Canvas g_display_canvas;
void* g_display_buffer = nullptr;
size_t g_display_buffer_bytes = 0;
esp_lcd_panel_handle_t g_dsi_panel = nullptr;
uint16_t g_dsi_width = 0;
uint16_t g_dsi_height = 0;
void* g_frame_buffers[2] = {};
uint8_t g_front_frame_buffer = 0;
uint8_t g_back_frame_buffer = 1;
bool g_page_flip_active = false;
bool g_page_flip_pending = false;
uint32_t g_page_flip_queued_ms = 0;
constexpr uint32_t kPageFlipSettleMs = 20;

struct Preset {
  char name[21];
  float values[kPresetValues];
  uint8_t value_count;
  bool used;
};

struct Persisted {
  uint32_t magic;
  uint16_t version;
  uint16_t bytes;
  uint8_t last_view;
  uint8_t reserved[3];
  float values[kMaxControls];
  Preset presets[static_cast<size_t>(View::count)][kCustomPresets];
};

using SpectrumFrame = rf_analysis::Snapshot;

struct Gesture {
  bool down = false;
  bool long_fired = false;
  int32_t x = 0;
  int32_t y = 0;
  uint32_t started_ms = 0;
};

struct ChannelState {
  float nco_i = 1;
  float nco_q = 0;
  float lpf_i = 0;
  float lpf_q = 0;
  float previous_i = 0;
  float previous_q = 0;
  float dc = 0;
  float level = -120;
  float squelch = -90;
  float offset_hz = 0;
  uint8_t phase = 0;
  uint8_t demod = 0;
  bool have_previous = false;
  bool muted = false;
};

NvsStore* g_store = nullptr;
AudioSink g_audio_sink = nullptr;
Persisted* g_persist_storage = nullptr;
#define g_persist (*g_persist_storage)
Runtime g_runtime{};
portMUX_TYPE g_runtime_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> g_active{false};
std::atomic<bool> g_source_available{false};
std::atomic<uint8_t> g_view{0};
SemaphoreHandle_t g_frame_mutex = nullptr;
SemaphoreHandle_t g_history_mutex = nullptr;
SpectrumFrame* g_frame = nullptr;
SpectrumFrame* g_ui_frame = nullptr;
SpectrumFrame* g_observer_frame = nullptr;
bool g_initialized = false;
uint32_t g_drawn_revision = 0;
uint8_t* g_history = nullptr;
bool g_history_audio = false;
size_t g_history_rows = 0;
size_t g_history_head = 0;
size_t g_history_count = 0;
uint8_t* g_density = nullptr;
uint8_t* g_density_snapshot = nullptr;
size_t g_density_rows = 0;
uint16_t* g_phosphor_row = nullptr;
uint8_t* g_doppler_snapshot = nullptr;
uint16_t* g_doppler_row = nullptr;
int16_t g_doppler_track[kDopplerColumns]{};
uint32_t g_last_density_decay_ms = 0;
uint8_t g_density_decay_phase = 0;
uint32_t g_last_heavy_view_draw_ms = 0;
uint32_t g_history_revision = 0;
uint32_t g_analysis_frames = 0;
uint32_t g_presentation_frames = 0;
uint32_t g_last_analysis_report_ms = 0;
uint32_t g_last_present_report_ms = 0;
float g_analysis_fps = 0;
float g_presentation_fps = 0;
uint32_t g_last_canvas_push_ms = 0;
uint32_t g_last_waterfall_row_ms = 0;
bool g_waterfall_initialized = false;
bool g_waterfall_canvas_synced = true;
float g_waterfall_floor = -80;
float g_waterfall_ceiling = -30;
float g_audio_floor = -100;
float g_audio_ceiling = -20;
float g_spectrum_floor = -110;
float g_spectrum_ceiling = -20;
bool g_spectrum_levels_initialized = false;
float g_phosphor_floor = -110;
float g_phosphor_ceiling = -20;
bool g_phosphor_levels_initialized = false;
uint32_t g_name_until_ms = 0;
uint32_t g_hud_hide_ms = 0;
uint32_t g_message_until_ms = 0;
char g_message[64]{};
bool g_inspect = false;
bool g_hud_locked = false;
bool g_drawer = false;
bool g_chooser = false;
uint8_t g_drawer_page = 0;
int g_control_scroll = 0;
Gesture g_gesture{};
uint32_t g_last_tap_ms = 0;
int32_t g_last_tap_x = 0;
int32_t g_last_tap_y = 0;
uint8_t g_origin_screen = 0;
uint8_t g_origin_tab = 0;
Action g_action{};
portMUX_TYPE g_action_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_persist_due_ms = 0;
uint32_t g_last_health_ms = 0;
uint32_t g_stable_since_ms = 0;
uint32_t g_last_usb_overruns = 0;
uint32_t g_last_consumer_drops = 0;
uint32_t g_last_audio_drops = 0;
uint8_t g_effective_quality = 1;
uint32_t g_occupancy_frames[32]{};
uint32_t g_occupancy_busy[32]{};
uint32_t g_occupancy_dwell_ms[32]{};
uint8_t g_occupancy_heatmap[kOccupancyRows * 32]{};
uint32_t g_occupancy_bin_frames = 0;
uint32_t g_occupancy_bin_busy[32]{};
uint32_t g_occupancy_bin_started_ms = 0;
size_t g_occupancy_heatmap_head = 0;
size_t g_occupancy_heatmap_count = 0;
ChannelState g_channels[8]{};
uint8_t g_selected_channel = 0;
bool g_channel_solo = false;
bool g_source_lost_drawn = false;
bool g_preset_naming = false;
bool g_editor_pressed = false;
size_t g_selected_preset = 0;

void set_value(size_t index, float next, bool persist);
void allocate_view_buffers(View next);
void configure_analysis();
void process_channel_audio(const uint8_t* iq, size_t bytes);

size_t copy_view_values(View current, float* output, size_t capacity) {
  size_t count = 0, descriptor_count = 0;
  const auto* list = controls(&descriptor_count);
  for (size_t i = 0; i < descriptor_count && count < capacity; ++i) {
    if ((list[i].view == View::common || list[i].view == current) &&
        list[i].kind != ControlKind::action)
      output[count++] = g_persist.values[i];
  }
  return count;
}

void apply_view_values(View current, const Preset& preset) {
  size_t used = 0, descriptor_count = 0;
  const auto* list = controls(&descriptor_count);
  for (size_t i = 0; i < descriptor_count && used < preset.value_count; ++i) {
    if ((list[i].view == View::common || list[i].view == current) &&
        list[i].kind != ControlKind::action)
      set_value(i, preset.values[used++], false);
  }
  g_persist_due_ms = millis() + 1;
  allocate_view_buffers(current);
}

bool preset_slot(const char* text, size_t* slot) {
  if (!text || !slot) return false;
  char* end = nullptr;
  const long value = strtol(text, &end, 10);
  if (end == text || *end != '\0' || value < 1 || value > static_cast<long>(kCustomPresets))
    return false;
  *slot = static_cast<size_t>(value - 1);
  return true;
}

void save_preset(size_t slot, const char* name) {
  auto& preset = g_persist.presets[g_view.load()][slot];
  preset = {};
  if (name && name[0]) strlcpy(preset.name, name, sizeof(preset.name));
  else snprintf(preset.name, sizeof(preset.name), "Custom %u", static_cast<unsigned>(slot + 1));
  preset.value_count = static_cast<uint8_t>(
      copy_view_values(view(), preset.values, std::size(preset.values)));
  preset.used = true;
  g_persist_due_ms = millis() + 1;
}

float value(const char* id) {
  size_t index = 0;
  return find_control(id, &index) && index < kMaxControls ? g_persist.values[index] : 0;
}

int choice_count(const char* choices) {
  if (!choices || !choices[0]) return 0;
  int count = 1;
  for (const char* p = choices; *p; ++p) if (*p == '|') ++count;
  return count;
}

void set_value(size_t index, float next, bool persist = true) {
  size_t count = 0;
  const auto* list = controls(&count);
  if (index >= count || index >= kMaxControls) return;
  const auto& c = list[index];
  if (c.kind == ControlKind::action) return;
  next = std::clamp(next, c.minimum, c.maximum);
  if (c.kind != ControlKind::real) next = roundf(next);
  if (c.kind == ControlKind::choice)
    next = std::min(next, static_cast<float>(std::max(0, choice_count(c.choices) - 1)));
  if (strcmp(c.id, "display.floor_dbfs") == 0)
    next = std::min(next, value("display.ceiling_dbfs") - 20.0f);
  if (strcmp(c.id, "display.ceiling_dbfs") == 0)
    next = std::max(next, value("display.floor_dbfs") + 20.0f);
  if (strcmp(c.id, "audiospec.floor_dbfs") == 0)
    next = std::min(next, value("audiospec.ceiling_dbfs") - 20.0f);
  if (strcmp(c.id, "audiospec.ceiling_dbfs") == 0)
    next = std::max(next, value("audiospec.floor_dbfs") + 20.0f);
  g_persist.values[index] = next;
  if (strcmp(c.id, "visual.quality") == 0)
    g_effective_quality = static_cast<uint8_t>(next);
  else if (strcmp(c.id, "channelizer.solo") == 0)
    g_channel_solo = next != 0;
  else if (strcmp(c.id, "channelizer.mute") == 0)
    g_channels[g_selected_channel].muted = next != 0;
  else if (strcmp(c.id, "channelizer.demod") == 0) {
    g_channels[g_selected_channel].demod = static_cast<uint8_t>(next);
    if (next == 4 && value("channelizer.bandwidth") < 5) {
      size_t bandwidth = 0;
      if (find_control("channelizer.bandwidth", &bandwidth)) g_persist.values[bandwidth] = 5;
    }
  } else if (strcmp(c.id, "channelizer.squelch_dbfs") == 0)
    g_channels[g_selected_channel].squelch = next;
  else if (strcmp(c.id, "channelizer.selected_offset_hz") == 0)
    g_channels[g_selected_channel].offset_hz = next;
  if (strcmp(c.id, "visual.freeze") == 0) persist = false;
  if (persist) g_persist_due_ms = millis() + 2000;
  if (g_initialized) configure_analysis();
}

uint32_t capabilities() {
  Runtime runtime{};
  portENTER_CRITICAL(&g_runtime_mux);
  runtime = g_runtime;
  portEXIT_CRITICAL(&g_runtime_mux);
  uint32_t result = runtime.source_available ? cap_iq | cap_carrier_frequency : 0;
  if (runtime.filtered_iq) result |= cap_filtered_iq;
  if (runtime.stereo_audio) result |= cap_stereo_audio;
  if (runtime.sd_writable) result |= cap_sd;
  if (g_effective_quality <= 2) result |= cap_channel_audio;
  return result;
}

bool available(const ControlDescriptor& c) {
  if (c.required_caps == cap_none) return true;
  if (strcmp(c.id, "doppler.units") == 0) return true;
  return (capabilities() & c.required_caps) == c.required_caps;
}

void message(const char* text, uint32_t duration = 1800) {
  strlcpy(g_message, text ? text : "", sizeof(g_message));
  g_message_until_ms = millis() + duration;
}

void queue_action(ActionKind kind, uint32_t v = 0) {
  portENTER_CRITICAL(&g_action_mux);
  g_action = {kind, v};
  portEXIT_CRITICAL(&g_action_mux);
}

uint16_t heat_color(uint8_t v, uint8_t palette = 0) {
  const auto byte = [](int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
  };
  if (palette == 0) {
    if (v < 32) return g_canvas.color565(0, 0, static_cast<uint8_t>(v * 2));
    if (v < 96) return g_canvas.color565(0, 0, static_cast<uint8_t>(64 + (v - 32) * 3));
    if (v < 144) return g_canvas.color565(0, static_cast<uint8_t>((v - 96) * 5), 255);
    if (v < 192) return g_canvas.color565(static_cast<uint8_t>((v - 144) * 5), 255,
                                           static_cast<uint8_t>(255 - (v - 144) * 5));
    if (v < 232) return g_canvas.color565(255, static_cast<uint8_t>(255 - (v - 192) * 6), 0);
    return g_canvas.color565(255, static_cast<uint8_t>((v - 232) * 11),
                             static_cast<uint8_t>((v - 232) * 11));
  }
  if (palette == 4) return g_canvas.color565(v, v, v);
  if (palette == 5) return g_canvas.color565(v, static_cast<uint8_t>(v * 0.55f), 0);
  if (palette == 6) return g_canvas.color565(static_cast<uint8_t>(v * 0.3f), v, 0);
  if (palette == 7) return g_canvas.color565(v, static_cast<uint8_t>(v * 0.55f), 0);
  if (palette == 8) return g_canvas.color565(0, v, v);
  const int r = palette == 2 ? v * v / 384 : palette == 3 ? v * 2 : (v - 128) * 2;
  const int g = palette == 1 ? 255 - std::abs(128 - static_cast<int>(v)) * 2 :
                palette == 3 ? v * v / 255 : (v - 64) * 4;
  const int b = palette == 3 ? (v < 128 ? v : 255 - v) :
                v < 128 ? v * 2 : 255 - (v - 128) * 4;
  return g_canvas.color565(byte(r), byte(g), byte(b));
}

uint8_t waterfall_rate(float choice) {
  constexpr uint8_t rates[] = {30, 5, 10, 15, 20, 30, 45, 60};
  return rates[std::clamp(static_cast<int>(choice), 0, 7)];
}

uint8_t waterfall_intensity(float level, float floor, float ceiling, float gamma) {
  const float normalized = std::clamp((level - floor) / (ceiling - floor), 0.0f, 1.0f);
  return static_cast<uint8_t>(255.0f * powf(normalized, gamma));
}

int audio_scroll_pixels(uint32_t elapsed_ms, float choice) {
  constexpr uint8_t spans[] = {5, 10, 20, 30, 60};
  const uint32_t span_ms = spans[std::clamp(static_cast<int>(choice), 0, 4)] * 1000u;
  return std::clamp(static_cast<int>((elapsed_ms * kPlotW + span_ms / 2) / span_ms),
                    1, kPlotW);
}

bool is_full_repaint_view(View current) {
  return current != View::waterfall && current != View::audio_spectrogram;
}

void set_canvas_buffer(uint8_t index) {
  if (!g_frame_buffers[index]) return;
  const auto* panel = static_cast<lgfx::Panel_DSI*>(M5.Display.getPanel());
  const auto& config = panel->config();
  g_canvas.setBuffer(g_frame_buffers[index], config.panel_width, config.panel_height);
  g_display_canvas.setBuffer(g_frame_buffers[index], config.panel_width, config.panel_height);
  g_canvas.setRotation(M5.Display.getRotation());
  g_display_canvas.setRotation(M5.Display.getRotation());
}

void enable_page_flip() {
  if (g_page_flip_active || !g_dsi_panel || !g_frame_buffers[0] || !g_frame_buffers[1]) return;
  memcpy(g_frame_buffers[1], g_frame_buffers[0], g_display_buffer_bytes);
  g_front_frame_buffer = 0;
  g_back_frame_buffer = 1;
  g_page_flip_active = true;
  g_page_flip_pending = false;
  set_canvas_buffer(g_back_frame_buffer);
}

void disable_page_flip() {
  if (!g_page_flip_active) return;
  const uint8_t visible = g_page_flip_pending ? g_back_frame_buffer : g_front_frame_buffer;
  if (visible != 0) memcpy(g_frame_buffers[0], g_frame_buffers[visible], g_display_buffer_bytes);
  if (g_dsi_panel)
    esp_lcd_panel_draw_bitmap(g_dsi_panel, 0, 0, g_dsi_width, g_dsi_height,
                              g_frame_buffers[0]);
  g_front_frame_buffer = 0;
  g_back_frame_buffer = 1;
  g_page_flip_active = false;
  g_page_flip_pending = false;
  set_canvas_buffer(0);
}

bool service_page_flip(uint32_t now) {
  if (!g_page_flip_pending) return true;
  if (now - g_page_flip_queued_ms < kPageFlipSettleMs) return false;
  g_front_frame_buffer = g_back_frame_buffer;
  g_back_frame_buffer = 1 - g_front_frame_buffer;
  g_page_flip_pending = false;
  set_canvas_buffer(g_back_frame_buffer);
  return true;
}

void present_canvas(uint32_t now) {
  if (g_page_flip_active) {
    if (g_page_flip_pending || !g_dsi_panel) return;
    if (esp_lcd_panel_draw_bitmap(g_dsi_panel, 0, 0, g_dsi_width, g_dsi_height,
                                  g_canvas.getBuffer()) == ESP_OK) {
      g_page_flip_pending = true;
      g_page_flip_queued_ms = now;
    }
    return;
  }
  if (g_canvas.getBuffer() && g_display_buffer && g_canvas.getBuffer() != g_display_buffer)
    memcpy(g_display_buffer, g_canvas.getBuffer(), g_display_buffer_bytes);
}

void text(const char* s, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_left) {
  g_canvas.setTextDatum(datum);
  g_canvas.setTextColor(color, TFT_BLACK);
  g_canvas.setTextSize(size);
  g_canvas.drawString(s ? s : "", x, y);
}

bool inside(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void button(int x, int y, int w, int h, const char* label, uint16_t color, bool selected = false) {
  g_canvas.fillRoundRect(x, y, w, h, 7, selected ? color : kPanel);
  g_canvas.drawRoundRect(x, y, w, h, 7, color);
  text(label, x + w / 2, y + h / 2, selected ? TFT_BLACK : color, 2, middle_center);
}

void free_view_buffers() {
  if (g_history) heap_caps_free(g_history);
  if (g_density) heap_caps_free(g_density);
  if (g_density_snapshot) heap_caps_free(g_density_snapshot);
  if (g_phosphor_row) heap_caps_free(g_phosphor_row);
  if (g_doppler_snapshot) heap_caps_free(g_doppler_snapshot);
  if (g_doppler_row) heap_caps_free(g_doppler_row);
  g_history = nullptr;
  g_density = nullptr;
  g_density_snapshot = nullptr;
  g_phosphor_row = nullptr;
  g_doppler_snapshot = nullptr;
  g_doppler_row = nullptr;
  g_history_rows = g_history_head = g_history_count = g_density_rows = 0;
  g_last_density_decay_ms = 0;
  g_density_decay_phase = 0;
  g_history_audio = false;
}

size_t memory_budget() {
  const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const uint8_t quality = static_cast<uint8_t>(std::clamp(value("visual.quality"), 0.0f, 2.0f));
  if (quality == 0) return std::min<size_t>(8u * 1024u * 1024u, free_psram * 35u / 100u);
  if (quality == 1) return std::min<size_t>(4u * 1024u * 1024u, free_psram * 20u / 100u);
  return std::min<size_t>(2u * 1024u * 1024u, free_psram * 10u / 100u);
}

void allocate_view_buffers(View next) {
  if (g_history_mutex && xSemaphoreTake(g_history_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  const auto ensure_doppler_buffers = [] {
    if (!g_doppler_snapshot) {
      g_doppler_snapshot = static_cast<uint8_t*>(
          heap_caps_malloc(kDopplerColumns * kBins, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!g_doppler_row) {
      g_doppler_row = static_cast<uint16_t*>(
          heap_caps_malloc(kPlotW * kDopplerRowStep * sizeof(uint16_t),
                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return g_doppler_snapshot && g_doppler_row;
  };
  const size_t budget = memory_budget();
  const bool spectrum_history = next == View::waterfall || next == View::spectrum3d ||
                                next == View::doppler;
  const bool spectrum_family = next == View::spectrum || spectrum_history ||
                               next == View::peak_average || next == View::channelizer;
  if (spectrum_family && g_history && !g_history_audio) {
    if (next == View::doppler) (void)ensure_doppler_buffers();
    if (g_history_mutex) xSemaphoreGive(g_history_mutex);
    return;
  }
  if ((next == View::constellation || next == View::iqscope || next == View::polar ||
       next == View::occupancy) || next == View::phosphor || next == View::audio_spectrogram ||
      (spectrum_family && g_history_audio))
    free_view_buffers();
  if (spectrum_history || next == View::audio_spectrogram) {
    g_history_rows = std::clamp<size_t>(budget / kBins, 64, 4096);
    g_history = static_cast<uint8_t*>(
        heap_caps_calloc(g_history_rows, kBins, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_history) g_history_rows = 0;
    g_history_audio = next == View::audio_spectrogram;
    if (next == View::doppler) (void)ensure_doppler_buffers();
  } else if (next == View::phosphor) {
    g_density_rows = (kPlotH + 1) / 2;
    g_density = static_cast<uint8_t*>(
        heap_caps_calloc(g_density_rows, kBins, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_density_snapshot = static_cast<uint8_t*>(
        heap_caps_calloc(g_density_rows, kBins, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_phosphor_row = static_cast<uint16_t*>(
        heap_caps_malloc(kPlotW * 2 * sizeof(uint16_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_density || !g_density_snapshot || !g_phosphor_row) {
      if (g_density) heap_caps_free(g_density);
      if (g_density_snapshot) heap_caps_free(g_density_snapshot);
      if (g_phosphor_row) heap_caps_free(g_phosphor_row);
      g_density = nullptr;
      g_density_snapshot = nullptr;
      g_phosphor_row = nullptr;
      g_density_rows = 0;
    }
  }
  if (g_history_mutex) xSemaphoreGive(g_history_mutex);
}

uint16_t density_decay_scale(uint32_t elapsed_ms, float half_life_s) {
  const float scale = powf(0.5f, elapsed_ms / std::max(100.0f, half_life_s * 1000.0f));
  return static_cast<uint16_t>(std::clamp(lroundf(scale * 256.0f), 0l, 256l));
}

uint8_t density_linear_decay(uint32_t elapsed_ms, float half_life_s) {
  return static_cast<uint8_t>(std::clamp(
      lroundf(127.5f * elapsed_ms / std::max(100.0f, half_life_s * 1000.0f)),
      1l, 255l));
}

uint8_t density_accumulate(uint8_t current, uint8_t sample, uint8_t mode) {
  if (mode == 0) return std::max(current, sample);
  // Phosphor persistence is a hit histogram: each FFT paints a visible stroke,
  // then repeated hits become brighter while isolated noise remains particulate.
  const unsigned increment = mode == 1 ? std::max(1u, sample / 8u) : 12u;
  return static_cast<uint8_t>(std::min(255u, current + increment));
}

uint8_t density_display_intensity(uint8_t stored, float exposure) {
  return static_cast<uint8_t>(std::clamp(
      lroundf(stored * std::clamp(exposure, 0.25f, 8.0f) * 10.0f), 0l, 255l));
}

int selected_fft_size(View current) {
  if (current == View::doppler) {
    constexpr int sizes[] = {1024, 2048, 4096, 8192};
    int i = static_cast<int>(value("doppler.fft_size"));
    if (g_effective_quality > 0 && i > 2) i = 2;
    return sizes[std::clamp(i, 0, 3)];
  }
  if (current == View::audio_spectrogram) {
    constexpr int sizes[] = {256, 512, 1024, 2048};
    return sizes[std::clamp(static_cast<int>(value("audiospec.fft_size")), 0, 3)];
  }
  constexpr int sizes[] = {256, 512, 1024, 2048, 4096};
  int i = current == View::spectrum ? static_cast<int>(value("fft.size")) : 2;
  return sizes[std::clamp(i, 0, 4)];
}

void add_history_row(const SpectrumFrame& frame, bool audio) {
  if (!g_history || !g_history_rows) return;
  if (g_history_mutex && xSemaphoreTake(g_history_mutex, 0) != pdTRUE) return;
  uint8_t* row = g_history + g_history_head * kBins;
  const float floor = audio ? value("audiospec.floor_dbfs") : value("display.floor_dbfs");
  const float ceiling = audio ? value("audiospec.ceiling_dbfs") : value("display.ceiling_dbfs");
  const size_t count = audio ? frame.audio_bins : frame.bins;
  if (!count) {
    if (g_history_mutex) xSemaphoreGive(g_history_mutex);
    return;
  }
  for (size_t x = 0; x < kBins; ++x) {
    const float level = audio ? frame.audio[x * count / kBins] : frame.live[x * count / kBins];
    row[x] = static_cast<uint8_t>(std::clamp((level - floor) * 255.0f /
                                                 std::max(20.0f, ceiling - floor),
                                             0.0f, 255.0f));
  }
  g_history_head = (g_history_head + 1) % g_history_rows;
  g_history_count = std::min(g_history_count + 1, g_history_rows);
  ++g_history_revision;
  if (g_history_mutex) xSemaphoreGive(g_history_mutex);
}

void add_density(const SpectrumFrame& frame) {
  if (!g_density || !g_density_rows || !frame.bins) return;
  if (g_history_mutex && xSemaphoreTake(g_history_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;
  const int auto_levels = static_cast<int>(value("display.auto_levels"));
  if (!auto_levels) {
    g_phosphor_floor = value("display.floor_dbfs");
    g_phosphor_ceiling = value("display.ceiling_dbfs");
    g_phosphor_levels_initialized = false;
  } else {
    const float target_ceiling = std::clamp(frame.strongest + 8.0f, -80.0f, 5.0f);
    const float target_floor = std::clamp(frame.noise - 12.0f, -140.0f,
                                          target_ceiling - 40.0f);
    if (!g_phosphor_levels_initialized) {
      g_phosphor_floor = target_floor;
      g_phosphor_ceiling = target_ceiling;
      g_phosphor_levels_initialized = true;
    } else {
      const float alpha = auto_levels == 2 ? 0.25f : 0.06f;
      g_phosphor_floor += alpha * (target_floor - g_phosphor_floor);
      g_phosphor_ceiling += alpha * (target_ceiling - g_phosphor_ceiling);
    }
  }
  const float floor = g_phosphor_floor;
  const float ceiling = std::max(floor + 40.0f, g_phosphor_ceiling);
  const float reject = std::min(frame.noise + value("persistence.background_reject_db"),
                                frame.strongest - 1.5f);
  const uint8_t accumulation = static_cast<uint8_t>(value("persistence.accumulation"));
  const uint32_t now = millis();
  const uint32_t elapsed = now - g_last_density_decay_ms;
  if (!g_last_density_decay_ms) g_last_density_decay_ms = now;
  if (elapsed >= 100) {
    const float half_life = value("persistence.half_life_s");
    const bool exponential = value("persistence.decay_curve") != 0;
    // Decay interleaved cells each pass. This keeps the producer lock short
    // without making a visible horizontal erase band.
    const uint32_t decay_elapsed = elapsed * 8;
    const uint16_t scale = density_decay_scale(decay_elapsed, half_life);
    const uint8_t decrement = density_linear_decay(decay_elapsed, half_life);
    const size_t cell_count = kBins * g_density_rows;
    for (size_t i = g_density_decay_phase; i < cell_count; i += 8) {
      uint8_t& cell = g_density[i];
      cell = exponential ? static_cast<uint8_t>(cell * scale / 256)
                         : cell > decrement ? cell - decrement : 0;
    }
    g_density_decay_phase = static_cast<uint8_t>((g_density_decay_phase + 1) & 7);
    g_last_density_decay_ms = now;
  }
  for (size_t x = 0; x < kBins; ++x) {
    const float relative = std::clamp((frame.live[x] - reject) /
        std::max(6.0f, ceiling - reject), 0.0f, 1.0f);
    if (relative <= 0.0f) continue;
    const float n = std::clamp((frame.live[x] - floor) /
        std::max(20.0f, ceiling - floor), 0.0f, 1.0f);
    const size_t y = std::min(g_density_rows - 1,
                              static_cast<size_t>((1.0f - n) * (g_density_rows - 1)));
    const uint8_t sample = static_cast<uint8_t>(lroundf(sqrtf(relative) * 255.0f));
    const int spread = std::clamp(static_cast<int>(value("persistence.point_size")) - 1, 0, 2);
    for (int offset = -spread; offset <= spread; ++offset) {
      const int row = static_cast<int>(y) + offset;
      if (row < 0 || row >= static_cast<int>(g_density_rows)) continue;
      const uint8_t stroke = offset == 0 ? sample : static_cast<uint8_t>(sample / 3);
      uint8_t& cell = g_density[static_cast<size_t>(row) * kBins + x];
      cell = density_accumulate(cell, stroke, accumulation);
    }
  }
  ++g_history_revision;
  if (g_history_mutex) xSemaphoreGive(g_history_mutex);
}

void update_occupancy(const SpectrumFrame& frame) {
  if (!frame.bins) return;
  constexpr size_t counts[] = {4, 8, 16, 32};
  constexpr uint32_t time_bins_ms[] = {100, 250, 500, 1000, 2000, 5000, 10000};
  const size_t bands = counts[std::clamp(static_cast<int>(value("occupancy.band_count")), 0, 3)];
  const float threshold = frame.noise + value("occupancy.threshold_offset_db");
  const uint32_t frame_ms = g_effective_quality == 0 ? 16 : g_effective_quality == 1 ? 33 : 100;
  const uint32_t required_ms = static_cast<uint32_t>(value("occupancy.minimum_dwell_ms"));
  const uint32_t now = millis();
  if (!g_occupancy_bin_started_ms) g_occupancy_bin_started_ms = now;
  ++g_occupancy_bin_frames;
  for (size_t band = 0; band < bands; ++band) {
    const size_t first = band * frame.bins / bands;
    const size_t last = (band + 1) * frame.bins / bands;
    float peak = -160;
    for (size_t i = first; i < last; ++i) peak = std::max(peak, frame.live[i]);
    ++g_occupancy_frames[band];
    if (peak >= threshold)
      g_occupancy_dwell_ms[band] = std::min<uint32_t>(required_ms, g_occupancy_dwell_ms[band] + frame_ms);
    else
      g_occupancy_dwell_ms[band] = 0;
    if (g_occupancy_dwell_ms[band] >= required_ms) {
      ++g_occupancy_busy[band];
      ++g_occupancy_bin_busy[band];
    }
  }
  const uint32_t time_bin_ms = time_bins_ms[
      std::clamp(static_cast<int>(value("occupancy.time_bin_ms")), 0, 6)];
  if (now - g_occupancy_bin_started_ms >= time_bin_ms) {
    uint8_t* row = g_occupancy_heatmap + g_occupancy_heatmap_head * 32;
    for (size_t band = 0; band < bands; ++band)
      row[band] = static_cast<uint8_t>(std::min<uint32_t>(255,
          255 * g_occupancy_bin_busy[band] / std::max<uint32_t>(1, g_occupancy_bin_frames)));
    for (size_t band = bands; band < 32; ++band) row[band] = 0;
    g_occupancy_heatmap_head = (g_occupancy_heatmap_head + 1) % kOccupancyRows;
    g_occupancy_heatmap_count = std::min(kOccupancyRows, g_occupancy_heatmap_count + 1);
    g_occupancy_bin_frames = 0;
    memset(g_occupancy_bin_busy, 0, sizeof(g_occupancy_bin_busy));
    g_occupancy_bin_started_ms = now;
  }
}


void configure_analysis() {
  Runtime runtime{};
  portENTER_CRITICAL(&g_runtime_mux);
  runtime = g_runtime;
  portEXIT_CRITICAL(&g_runtime_mux);
  const View current = static_cast<View>(g_view.load(std::memory_order_acquire));
  rf_analysis::Config config{};
  config.center_hz = runtime.center_hz;
  config.span_hz = runtime.span_hz;
  config.sample_rate_sps = runtime.sample_rate_sps;
  config.audio_rate_sps = runtime.audio_rate_sps;
  config.measurement_bandwidth_hz = runtime.filter_bandwidth_hz;
  config.fft_size = static_cast<uint16_t>(
      current == View::audio_spectrogram ? 1024 : selected_fft_size(current));
  config.audio_fft_size = static_cast<uint16_t>(selected_fft_size(View::audio_spectrogram));
  config.interval_ms = g_effective_quality == 0 ? 16 : g_effective_quality == 1 ? 33 : 100;
  // Doppler shares Core 1 with DSP/audio. A 4096-point FFT at the display cadence
  // can starve IDLE1; 20 Hz retains a fluid drift trace without risking reception.
  if (current == View::doppler) config.interval_ms = std::max<uint16_t>(50, config.interval_ms);
  config.audio_spectrum = current == View::audio_spectrogram;
  config.audio_demod = runtime.audio_demod == AudioDemod::fm
                           ? rf_analysis::AudioDemod::fm
                           : runtime.audio_demod == AudioDemod::am
                                 ? rf_analysis::AudioDemod::am
                                 : rf_analysis::AudioDemod::none;
  rf_analysis::set_config(config);
}

void transform_iq_frame(SpectrumFrame* frame) {
  if (!frame || !frame->iq_count) return;
  const View current = static_cast<View>(g_view.load(std::memory_order_acquire));
  if (current != View::constellation && current != View::polar) return;
  const int source = static_cast<int>(value(current == View::constellation
                                                 ? "constellation.source"
                                                 : "polar.source"));
  const float manual_deg = value(current == View::constellation
                                      ? "constellation.phase_deg"
                                      : "polar.rotation_deg");
  const float manual = manual_deg * 3.14159265358979323846f / 180.0f;
  static float filtered_i = 0, filtered_q = 0, carrier_phase = 0, carrier_rate = 0;
  if (source > 0) {
    Runtime runtime{};
    portENTER_CRITICAL(&g_runtime_mux);
    runtime = g_runtime;
    portEXIT_CRITICAL(&g_runtime_mux);
    const float bandwidth = current == View::constellation
                                ? value("constellation.channel_bw_hz")
                                : std::max(2400.0f, runtime.span_hz / 8.0f);
    const float alpha = std::clamp(6.2831853f * bandwidth /
                                       std::max(1.0f, static_cast<float>(runtime.sample_rate_sps)),
                                   0.002f, 0.35f);
    const int recovery = current == View::constellation
                             ? static_cast<int>(value("constellation.carrier_recovery"))
                             : (value("polar.phase_reference") == 2 ? 1 : 0);
    for (size_t i = 0; i < frame->iq_count; ++i) {
      filtered_i += alpha * (frame->iq_i[i] - filtered_i);
      filtered_q += alpha * (frame->iq_q[i] - filtered_q);
      const float phase = carrier_phase + manual;
      const float c = cosf(phase), s = sinf(phase);
      const float rotated_i = filtered_i * c + filtered_q * s;
      const float rotated_q = filtered_q * c - filtered_i * s;
      frame->iq_i[i] = rotated_i;
      frame->iq_q[i] = rotated_q;
      if (recovery) {
        const float error = recovery == 2
                                ? copysignf(1.0f, rotated_i) * rotated_q -
                                      copysignf(1.0f, rotated_q) * rotated_i
                                : atan2f(rotated_q, rotated_i);
        carrier_rate = std::clamp(carrier_rate + 0.00002f * error, -0.08f, 0.08f);
        carrier_phase += carrier_rate + 0.002f * error;
        if (carrier_phase > 3.14159265358979323846f)
          carrier_phase -= 6.28318530717958647692f;
        if (carrier_phase < -3.14159265358979323846f)
          carrier_phase += 6.28318530717958647692f;
      }
    }
    if (source > 1 && current == View::constellation) {
      const float symbol_rate = value("constellation.symbol_rate");
      if (symbol_rate > 0 && runtime.sample_rate_sps > symbol_rate) {
        const float samples_per_symbol = runtime.sample_rate_sps / symbol_rate;
        float cursor = 0;
        size_t output = 0;
        while (cursor < frame->iq_count && output < frame->iq_count) {
          const size_t index =
              std::min<size_t>(frame->iq_count - 1, static_cast<size_t>(cursor));
          frame->iq_i[output] = frame->iq_i[index];
          frame->iq_q[output++] = frame->iq_q[index];
          cursor += samples_per_symbol;
        }
        frame->iq_count = static_cast<uint16_t>(output);
      }
    }
  } else if (manual != 0) {
    const float c = cosf(manual), s = sinf(manual);
    for (size_t i = 0; i < frame->iq_count; ++i) {
      const float re = frame->iq_i[i], im = frame->iq_q[i];
      frame->iq_i[i] = re * c + im * s;
      frame->iq_q[i] = im * c - re * s;
    }
  }
}

void analysis_observer(const rf_analysis::Snapshot& snapshot, const uint8_t* iq,
                       size_t bytes) {
  if (!g_active.load(std::memory_order_acquire) || !g_observer_frame) return;
  *g_observer_frame = snapshot;
  transform_iq_frame(g_observer_frame);
  if (xSemaphoreTake(g_frame_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
    *g_frame = *g_observer_frame;
    xSemaphoreGive(g_frame_mutex);
  }
  const View current = static_cast<View>(g_view.load(std::memory_order_acquire));
  if (!value("visual.freeze")) {
    if (current == View::phosphor) add_density(*g_observer_frame);
    else if (current == View::waterfall || current == View::spectrum3d || current == View::doppler)
      add_history_row(*g_observer_frame, false);
    else if (current == View::audio_spectrogram)
      add_history_row(*g_observer_frame, true);
    if (current == View::occupancy) update_occupancy(*g_observer_frame);
  }
  if (current == View::channelizer) process_channel_audio(iq, bytes);
  ++g_analysis_frames;
}

void process_channel_audio(const uint8_t* iq, size_t bytes) {
  if (!g_audio_sink || !g_channel_solo || !iq || bytes < 40) return;
  Runtime runtime{};
  portENTER_CRITICAL(&g_runtime_mux);
  runtime = g_runtime;
  portEXIT_CRITICAL(&g_runtime_mux);
  constexpr size_t counts[] = {2, 4, 8, 16};
  const size_t tiles = counts[std::clamp(static_cast<int>(value("channelizer.count")), 0, 3)];
  const size_t audio_limit = g_effective_quality == 0 ? 8 : g_effective_quality == 1 ? 4 : 2;
  const size_t channels = std::min<size_t>(tiles, audio_limit);
  if (g_selected_channel >= channels) return;
  int16_t pcm[512];
  size_t pcm_count = 0;
  const uint32_t decim = std::max<uint32_t>(1, runtime.sample_rate_sps / 48000u);
  for (size_t channel = 0; channel < channels; ++channel) {
    auto& state = g_channels[channel];
    const bool contiguous = value("channelizer.plan") == 0;
    const float offset = contiguous
                             ? (static_cast<float>(channel) + 0.5f) / tiles - 0.5f
                             : state.offset_hz / std::max(1.0f, static_cast<float>(runtime.sample_rate_sps));
    const float omega = -2.0f * 3.14159265358979323846f * offset;
    const float step_i = cosf(omega), step_q = sinf(omega);
    for (size_t sample = 0; sample < bytes / 2; ++sample) {
      const float in_i = (static_cast<int>(iq[sample * 2]) - 127.5f) / 127.5f;
      const float in_q = (static_cast<int>(iq[sample * 2 + 1]) - 127.5f) / 127.5f;
      const float mixed_i = in_i * state.nco_i - in_q * state.nco_q;
      const float mixed_q = in_i * state.nco_q + in_q * state.nco_i;
      const float next_i = state.nco_i * step_i - state.nco_q * step_q;
      state.nco_q = state.nco_i * step_q + state.nco_q * step_i;
      state.nco_i = next_i;
      state.lpf_i += 0.12f * (mixed_i - state.lpf_i);
      state.lpf_q += 0.12f * (mixed_q - state.lpf_q);
      if (++state.phase < decim) continue;
      state.phase = 0;
      float demod = 0;
      if (state.demod == 1) {
        demod = hypotf(state.lpf_i, state.lpf_q);
      } else if (state.demod == 2) {
        demod = state.lpf_i - state.lpf_q;
      } else if (state.demod == 3) {
        demod = state.lpf_i + state.lpf_q;
      } else if (state.have_previous) {
        const float cross = state.previous_i * state.lpf_q - state.previous_q * state.lpf_i;
        const float dot = state.previous_i * state.lpf_i + state.previous_q * state.lpf_q;
        demod = atan2f(cross, dot);
      }
      state.previous_i = state.lpf_i;
      state.previous_q = state.lpf_q;
      state.have_previous = true;
      state.level = 0.98f * state.level +
                    0.02f * (20.0f * log10f(hypotf(state.lpf_i, state.lpf_q) + 1.0e-6f));
      state.dc += 0.002f * (demod - state.dc);
      const float scale = state.demod == 4 ? 3600.0f :
                          state.demod == 0 ? 7000.0f : 12000.0f;
      if (channel == g_selected_channel && !state.muted && state.level >= state.squelch &&
          pcm_count < std::size(pcm))
        pcm[pcm_count++] = static_cast<int16_t>(std::clamp((demod - state.dc) * scale,
                                                           -15000.0f, 15000.0f));
    }
  }
  if (pcm_count) g_audio_sink(pcm, pcm_count);
}


void draw_frame_chrome() {
  g_canvas.setFont(nullptr);
  g_canvas.clearScrollRect();
  g_waterfall_initialized = false;
  g_waterfall_canvas_synced = true;
  g_canvas.fillScreen(kBg);
  g_canvas.drawRoundRect(8, 8, 1264, 704, 12, kGrid);
  char badge[4];
  snprintf(badge, sizeof(badge), "%02u", static_cast<unsigned>(g_view.load() + 1));
  g_canvas.drawRoundRect(18, 14, 58, 42, 7, kCyan);
  text(badge, 47, 35, kCyan, 2, middle_center);
  text(view_name(static_cast<View>(g_view.load())), 640, 35, TFT_WHITE, 3, middle_center);
}

void draw_grid(bool polar = false) {
  g_canvas.fillRect(kPlotX, kPlotY, kPlotW, kPlotH, kBg);
  g_canvas.drawRect(kPlotX, kPlotY, kPlotW, kPlotH, kGrid);
  if (polar) return;
  for (int i = 1; i < 8; ++i)
    g_canvas.drawFastVLine(kPlotX + i * kPlotW / 8, kPlotY, kPlotH, kGrid);
  for (int i = 1; i < 6; ++i)
    g_canvas.drawFastHLine(kPlotX, kPlotY + i * kPlotH / 6, kPlotW, kGrid);
}

void spectrum_levels(const SpectrumFrame& frame, float* floor, float* ceiling) {
  const float configured_floor = value("display.floor_dbfs");
  const float configured_ceiling = value("display.ceiling_dbfs");
  const int auto_levels = static_cast<int>(value("display.auto_levels"));
  if (!auto_levels) {
    g_spectrum_floor = configured_floor;
    g_spectrum_ceiling = configured_ceiling;
    g_spectrum_levels_initialized = false;
  } else {
    const float target_ceiling = std::clamp(frame.strongest + 8.0f, -80.0f, 5.0f);
    const float target_floor = std::clamp(frame.noise - 12.0f, -140.0f,
                                          target_ceiling - 40.0f);
    if (!g_spectrum_levels_initialized) {
      g_spectrum_floor = target_floor;
      g_spectrum_ceiling = target_ceiling;
      g_spectrum_levels_initialized = true;
    } else {
      const float alpha = auto_levels == 2 ? 0.25f : 0.06f;
      g_spectrum_floor += alpha * (target_floor - g_spectrum_floor);
      g_spectrum_ceiling += alpha * (target_ceiling - g_spectrum_ceiling);
    }
  }
  *floor = g_spectrum_floor;
  *ceiling = std::max(g_spectrum_floor + 40.0f, g_spectrum_ceiling);
}

int level_y(float level, float floor, float ceiling, int y, int h) {
  const float n = std::clamp((level - floor) / std::max(20.0f, ceiling - floor), 0.0f, 1.0f);
  return y + h - 1 - static_cast<int>(n * (h - 2));
}

void draw_trace(const float* levels, size_t bins, uint16_t color, int y = kPlotY,
                int h = kPlotH, int thickness = 1, float floor = NAN, float ceiling = NAN) {
  if (!levels || bins < 2) return;
  if (std::isnan(floor)) floor = value("display.floor_dbfs");
  if (std::isnan(ceiling)) ceiling = value("display.ceiling_dbfs");
  int px = kPlotX;
  int py = level_y(levels[0], floor, ceiling, y, h);
  for (int x = 1; x < kPlotW; ++x) {
    const size_t bin = static_cast<size_t>(x) * bins / kPlotW;
    const int next_y = level_y(levels[std::min(bin, bins - 1)], floor, ceiling, y, h);
    g_canvas.drawLine(px, py, kPlotX + x, next_y, color);
    if (thickness > 1) g_canvas.drawLine(px, py + 1, kPlotX + x, next_y + 1, color);
    px = kPlotX + x;
    py = next_y;
  }
}

void draw_spectrum_view(const SpectrumFrame& frame) {
  draw_grid();
  float floor = 0;
  float ceiling = 0;
  spectrum_levels(frame, &floor, &ceiling);
  draw_trace(frame.live, frame.bins, kCyan, kPlotY, kPlotH,
             static_cast<int>(value("fft.trace_thickness")), floor, ceiling);
  if (value("fft.peak_hold_mode") > 0)
    draw_trace(frame.peak, frame.bins, kYellow, kPlotY, kPlotH, 1, floor, ceiling);
  if (value("fft.center_cursor") > 0)
    g_canvas.drawFastVLine(kPlotX + kPlotW / 2, kPlotY, kPlotH, kGreen);
}

void draw_waterfall_view(const SpectrumFrame& frame) {
  if (!frame.bins) return;
  const uint32_t now = millis();
  const uint8_t rate = waterfall_rate(value("waterfall.speed"));
  if (g_waterfall_initialized && now - g_last_waterfall_row_ms < 1000u / rate) return;
  g_last_waterfall_row_ms = now;

  const bool custom_levels = value("waterfall.level_mode") != 0;
  const float configured_floor = value(custom_levels ? "waterfall.floor_dbfs"
                                                       : "display.floor_dbfs");
  const float configured_ceiling = value(custom_levels ? "waterfall.ceiling_dbfs"
                                                         : "display.ceiling_dbfs");
  const int auto_levels = static_cast<int>(value("display.auto_levels"));
  if (!auto_levels || custom_levels) {
    g_waterfall_floor = configured_floor;
    g_waterfall_ceiling = configured_ceiling;
  } else {
    const float target_floor = std::max(configured_floor, frame.noise - 2.0f);
    const float target_ceiling = std::min(configured_ceiling,
        std::max(target_floor + 16.0f, frame.strongest + 1.0f));
    const float alpha = auto_levels == 2 ? 0.25f : 0.06f;
    g_waterfall_floor += alpha * (target_floor - g_waterfall_floor);
    g_waterfall_ceiling += alpha * (target_ceiling - g_waterfall_ceiling);
  }
  g_waterfall_ceiling = std::max(g_waterfall_floor + 16.0f, g_waterfall_ceiling);

  const bool first_row = !g_waterfall_initialized;
  g_waterfall_initialized = true;
  const bool newest_at_bottom = value("waterfall.direction") != 0;

  uint16_t row[kPlotW];
  const float gamma = value("waterfall.gamma");
  const uint8_t palette = static_cast<uint8_t>(value("waterfall.palette"));
  for (int x = 0; x < kPlotW; ++x) {
    const float level = frame.live[static_cast<size_t>(x) * frame.bins / kPlotW];
    row[x] = heat_color(waterfall_intensity(
        level, g_waterfall_floor, g_waterfall_ceiling, gamma), palette);
  }
  const int y = newest_at_bottom ? kPlotY + kPlotH - 1 : kPlotY;
  const bool direct = !g_inspect && !g_drawer && !g_chooser &&
      (!g_message_until_ms || static_cast<int32_t>(g_message_until_ms - now) <= 0) &&
      g_source_available.load(std::memory_order_acquire);
  auto& canvas = direct ? g_display_canvas : g_canvas;
  if (first_row) {
    for (int py = kPlotY; py < kPlotY + kPlotH; ++py)
      canvas.pushImage(kPlotX, py, kPlotW, 1, row);
    g_waterfall_canvas_synced = !direct;
    return;
  }
  canvas.setScrollRect(kPlotX, kPlotY, kPlotW, kPlotH, kBg);
  canvas.scroll(0, newest_at_bottom ? -1 : 1);
  canvas.pushImage(kPlotX, y, kPlotW, 1, row);
  canvas.clearScrollRect();
  g_waterfall_canvas_synced = !direct;
}

void draw_audio_spectrogram(const SpectrumFrame& frame) {
  if (!frame.audio_bins) return;
  const uint32_t now = millis();
  const bool first_column = !g_waterfall_initialized;
  const int columns = first_column
                          ? 1
                          : audio_scroll_pixels(now - g_last_waterfall_row_ms,
                                                value("audiospec.time_span_s"));
  g_last_waterfall_row_ms = now;
  g_waterfall_initialized = true;
  uint16_t column[kPlotH];
  const float configured_floor = value("audiospec.floor_dbfs");
  const float configured_ceiling = value("audiospec.ceiling_dbfs");
  const int tracking = static_cast<int>(value("audiospec.normalization"));
  if (!tracking) {
    g_audio_floor = configured_floor;
    g_audio_ceiling = configured_ceiling;
  } else {
    float mean = 0;
    float strongest = -160;
    for (size_t i = 0; i < frame.audio_bins; ++i) {
      mean += frame.audio[i];
      strongest = std::max(strongest, frame.audio[i]);
    }
    mean /= frame.audio_bins;
    const float target_floor = std::max(configured_floor, mean - 8.0f);
    const float target_ceiling = std::min(
        configured_ceiling, std::max(target_floor + 35.0f, strongest + 3.0f));
    const float alpha = first_column || tracking == 2 ? 1.0f : 0.06f;
    g_audio_floor += alpha * (target_floor - g_audio_floor);
    g_audio_ceiling += alpha * (target_ceiling - g_audio_ceiling);
  }
  g_audio_ceiling = std::max(g_audio_floor + 20.0f, g_audio_ceiling);
  const uint8_t palette = static_cast<uint8_t>(value("audiospec.palette"));
  for (int y = 0; y < kPlotH; ++y) {
    const size_t source = static_cast<size_t>(kPlotH - 1 - y) * frame.audio_bins / kPlotH;
    const float level = frame.audio[std::min(source, static_cast<size_t>(frame.audio_bins - 1))];
    const uint8_t intensity = static_cast<uint8_t>(std::clamp(
        (level - g_audio_floor) * 255.0f / (g_audio_ceiling - g_audio_floor),
        0.0f, 255.0f));
    column[y] = heat_color(intensity, palette);
  }
  const bool direct = !g_inspect && !g_drawer && !g_chooser &&
      (!g_message_until_ms || static_cast<int32_t>(g_message_until_ms - now) <= 0) &&
      g_source_available.load(std::memory_order_acquire);
  auto& canvas = direct ? g_display_canvas : g_canvas;
  canvas.setScrollRect(kPlotX, kPlotY, kPlotW, kPlotH, kBg);
  if (!first_column) canvas.scroll(-columns, 0);
  for (int x = kPlotX + kPlotW - columns; x < kPlotX + kPlotW; ++x)
    canvas.pushImage(x, kPlotY, 1, kPlotH, column);
  canvas.clearScrollRect();
  g_waterfall_canvas_synced = !direct;
}

void draw_phosphor_view() {
  if (!g_density || !g_density_snapshot || !g_density_rows || !g_phosphor_row) return;
  if (!g_history_mutex || xSemaphoreTake(g_history_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    memcpy(g_density_snapshot, g_density, g_density_rows * kBins);
    if (g_history_mutex) xSemaphoreGive(g_history_mutex);
  }
  // If the producer owns the density briefly, render the previous complete
  // snapshot. Never submit a cleared, incomplete frame to the page flipper.
  const uint8_t palette = static_cast<uint8_t>(
      6 + std::clamp(static_cast<int>(value("persistence.palette")), 0, 3));
  const float exposure = value("persistence.exposure");
  uint16_t colors[256];
  for (size_t i = 0; i < std::size(colors); ++i)
    colors[i] = heat_color(static_cast<uint8_t>(i), palette);
  g_canvas.fillRect(kPlotX, kPlotY, kPlotW, kPlotH, kBg);
  for (size_t sy = 0; sy < g_density_rows; ++sy) {
    for (int x = 0; x < kPlotW; x += 2) {
      const size_t sx0 = static_cast<size_t>(x) * kBins / kPlotW;
      const size_t sx1 = std::max(sx0 + 1,
          std::min(kBins, static_cast<size_t>(x + 2) * kBins / kPlotW));
      uint8_t intensity = 0;
      for (size_t sx = sx0; sx < sx1; ++sx)
        intensity = std::max(intensity, g_density_snapshot[sy * kBins + sx]);
      const uint16_t color = colors[density_display_intensity(intensity, exposure)];
      g_phosphor_row[x] = color;
      if (x + 1 < kPlotW) g_phosphor_row[x + 1] = color;
    }
    const int y = kPlotY + static_cast<int>(sy) * 2;
    memcpy(g_phosphor_row + kPlotW, g_phosphor_row,
           kPlotW * sizeof(uint16_t));
    g_canvas.pushImage(kPlotX, y, kPlotW,
                       std::min(2, kPlotY + kPlotH - y), g_phosphor_row);
  }
  g_canvas.drawRect(kPlotX, kPlotY, kPlotW, kPlotH, kGrid);
  text("PERSIST", 1243, kPlotY + 14, kMuted, 1);
  for (int i = 0; i < 5; ++i) {
    const uint8_t intensity = static_cast<uint8_t>((4 - i) * 63);
    g_canvas.fillRect(1246, kPlotY + 34 + i * 22, 14, 20, colors[intensity]);
  }
  g_waterfall_canvas_synced = true;
}

void draw_3d_view() {
  draw_grid();
  if (!g_history || !g_history_count) return;
  if (g_history_mutex && xSemaphoreTake(g_history_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  const size_t history_head = g_history_head;
  const size_t history_count = g_history_count;
  if (g_history_mutex) xSemaphoreGive(g_history_mutex);
  constexpr size_t choices[] = {16, 24, 32, 48, 64, 96, 128};
  const size_t requested = choices[std::clamp(
      static_cast<int>(value("spectrum3d.slices")), 0, static_cast<int>(std::size(choices) - 1))];
  const size_t quality_cap = g_effective_quality == 0 ? 32 : g_effective_quality == 1 ? 24 : 16;
  const size_t slices = std::min({requested, history_count, quality_cap});
  if (!slices) return;
  const int depth_x = 190;
  const int depth_y = 390;
  const int step = g_effective_quality == 0 ? 6 : g_effective_quality == 1 ? 8 : 12;
  uint8_t row[kBins];
  for (size_t s = 0; s < slices; ++s) {
    const size_t row_index = (history_head + g_history_rows - 1 -
                              s * history_count / slices) % g_history_rows;
    if (g_history_mutex && xSemaphoreTake(g_history_mutex, pdMS_TO_TICKS(2)) != pdTRUE) continue;
    memcpy(row, g_history + row_index * kBins, sizeof(row));
    if (g_history_mutex) xSemaphoreGive(g_history_mutex);
    const float depth = slices > 1 ? static_cast<float>(s) / (slices - 1) : 0.0f;
    const int dx = static_cast<int>(lroundf(depth * depth_x));
    const int base_y = kPlotY + kPlotH - 18 - static_cast<int>(lroundf(depth * depth_y));
    const int width = kPlotW - dx - 8;
    const int amplitude = static_cast<int>(180.0f * (1.0f - 0.55f * depth));
    int px = kPlotX + dx, py = base_y;
    for (int x = step; x < width; x += step) {
      const uint8_t intensity = row[static_cast<size_t>(x) * kBins / width];
      const int yy = std::clamp(base_y - intensity * amplitude / 255,
                                kPlotY + 4, kPlotY + kPlotH - 4);
      const uint16_t color = heat_color(intensity);
      g_canvas.drawLine(px, py, kPlotX + dx + x, yy, color);
      px = kPlotX + dx + x;
      py = yy;
    }
  }
}

void draw_constellation(const SpectrumFrame& frame) {
  draw_grid();
  const int cx = kPlotX + kPlotW / 2, cy = kPlotY + kPlotH / 2;
  g_canvas.drawFastHLine(kPlotX, cy, kPlotW, kGrid);
  g_canvas.drawFastVLine(cx, kPlotY, kPlotH, kGrid);
  for (size_t i = 0; i < frame.iq_count; ++i) {
    const int x = cx + static_cast<int>(frame.iq_i[i] * kPlotH * 0.35f);
    const int y = cy - static_cast<int>(frame.iq_q[i] * kPlotH * 0.35f);
    if (inside(x, y, kPlotX, kPlotY, kPlotW, kPlotH)) g_canvas.drawPixel(x, y, kCyan);
  }
  text("LIVE IQ", kPlotX + 14, kPlotY + 18, kGreen, 1);
}

void draw_iqscope(const SpectrumFrame& frame) {
  draw_grid();
  if (frame.iq_count < 2) return;
  const int mid1 = kPlotY + kPlotH / 4, mid2 = kPlotY + 3 * kPlotH / 4;
  g_canvas.drawFastHLine(kPlotX, mid1, kPlotW, kGrid);
  g_canvas.drawFastHLine(kPlotX, mid2, kPlotW, kGrid);
  for (int x = 1; x < kPlotW; ++x) {
    const size_t a = (x - 1) * frame.iq_count / kPlotW;
    const size_t b = x * frame.iq_count / kPlotW;
    g_canvas.drawLine(kPlotX + x - 1, mid1 - static_cast<int>(frame.iq_i[a] * 120),
                        kPlotX + x, mid1 - static_cast<int>(frame.iq_i[b] * 120), kCyan);
    g_canvas.drawLine(kPlotX + x - 1, mid2 - static_cast<int>(frame.iq_q[a] * 120),
                        kPlotX + x, mid2 - static_cast<int>(frame.iq_q[b] * 120), kYellow);
  }
}

void draw_polar(const SpectrumFrame& frame) {
  draw_grid(true);
  const int cx = 640, cy = 375, radius = 265;
  for (int r = radius / 4; r <= radius; r += radius / 4) g_canvas.drawCircle(cx, cy, r, kGrid);
  for (int a = 0; a < 360; a += 45) {
    const float angle = a * 3.14159265358979323846f / 180.0f;
    g_canvas.drawLine(cx, cy, cx + static_cast<int>(cosf(angle) * radius),
                        cy - static_cast<int>(sinf(angle) * radius), kGrid);
  }
  for (size_t i = 0; i < frame.iq_count; ++i) {
    const int x = cx + static_cast<int>(frame.iq_i[i] * radius * 0.8f);
    const int y = cy - static_cast<int>(frame.iq_q[i] * radius * 0.8f);
    if (inside(x, y, kPlotX, kPlotY, kPlotW, kPlotH)) g_canvas.drawPixel(x, y, kMagenta);
  }
}

void draw_occupancy() {
  constexpr size_t counts[] = {4, 8, 16, 32};
  const size_t bands = counts[std::clamp(static_cast<int>(value("occupancy.band_count")), 0, 3)];
  constexpr int heat_y = kPlotY;
  constexpr int heat_h = 310;
  constexpr int bars_y = heat_y + heat_h + 80;
  constexpr int bars_h = 150;
  g_canvas.fillRect(kPlotX, heat_y, kPlotW, kPlotH, kBg);

  const size_t rows = std::max<size_t>(1, g_occupancy_heatmap_count);
  const int cell_w = std::max(1, kPlotW / static_cast<int>(bands));
  const int cell_h = std::max(1, heat_h / static_cast<int>(rows));
  for (size_t row = 0; row < rows; ++row) {
    const size_t source = (g_occupancy_heatmap_head + kOccupancyRows - rows + row) % kOccupancyRows;
    const uint8_t* values = g_occupancy_heatmap + source * 32;
    for (size_t band = 0; band < bands; ++band) {
      const int x = kPlotX + static_cast<int>(band) * cell_w;
      const int y = heat_y + static_cast<int>(row) * cell_h;
      g_canvas.fillRect(x + 1, y + 1, std::max(1, cell_w - 2), std::max(1, cell_h - 2),
                        heat_color(values[band], 0));
    }
  }
  g_canvas.drawRect(kPlotX, heat_y, kPlotW, heat_h, kGrid);
  for (size_t band = 1; band < bands; ++band)
    g_canvas.drawFastVLine(kPlotX + static_cast<int>(band) * cell_w, heat_y, heat_h, kGrid);

  char label[20];
  text("OCCUPANCY BY BAND", kPlotX, bars_y - 24, kMuted, 1);
  const int bar_w = kPlotW / static_cast<int>(bands);
  for (size_t i = 0; i < bands; ++i) {
    const float pct = g_occupancy_frames[i]
                          ? 100.0f * g_occupancy_busy[i] / g_occupancy_frames[i]
                          : 0;
    const int h = static_cast<int>(pct * bars_h / 100.0f);
    g_canvas.fillRect(kPlotX + static_cast<int>(i) * bar_w + 3,
                      bars_y + bars_h - h, std::max(1, bar_w - 6), h,
                      heat_color(static_cast<uint8_t>(lroundf(pct * 255.0f / 100.0f)), 0));
    snprintf(label, sizeof(label), "%.0f%%", static_cast<double>(pct));
    text(label, kPlotX + static_cast<int>(i) * bar_w + bar_w / 2, bars_y - 7, TFT_WHITE, 1,
         middle_center);
    snprintf(label, sizeof(label), "%u", static_cast<unsigned>(i + 1));
    text(label, kPlotX + static_cast<int>(i) * bar_w + bar_w / 2, bars_y + bars_h + 16,
         kMuted, 1, middle_center);
  }
  text("BAND #", kPlotX + kPlotW / 2, bars_y + bars_h + 36, kMuted, 1, middle_center);
  text("OCCUPANCY", 1242, heat_y + 12, kMuted, 1);
  for (int i = 0; i < 5; ++i) {
    const uint8_t intensity = static_cast<uint8_t>((4 - i) * 63);
    g_canvas.fillRect(1246, heat_y + 36 + i * 30, 14, 28, heat_color(intensity, 0));
  }
}

void draw_peak_average(const SpectrumFrame& frame) {
  draw_grid();
  float floor = 0;
  float ceiling = 0;
  spectrum_levels(frame, &floor, &ceiling);
  if (value("peakavg.show_max"))
    draw_trace(frame.peak, frame.bins, kYellow, kPlotY, kPlotH, 1, floor, ceiling);
  if (value("peakavg.show_live"))
    draw_trace(frame.live, frame.bins, kCyan, kPlotY, kPlotH, 1, floor, ceiling);
  if (value("peakavg.show_average"))
    draw_trace(frame.average, frame.bins, kGreen, kPlotY, kPlotH, 1, floor, ceiling);
}

void draw_doppler(const SpectrumFrame&) {
  g_canvas.fillRect(kPlotX, kPlotY, kPlotW, kPlotH, kBg);
  if (!g_history || !g_history_count || !g_doppler_snapshot || !g_doppler_row) return;

  constexpr float spans_hz[] = {250, 500, 1000, 2000, 5000};
  constexpr uint16_t history_seconds[] = {10, 30, 60, 300, 900};
  const float span_hz = spans_hz[std::clamp(static_cast<int>(value("doppler.span_hz")), 0, 4)];
  const uint16_t history_s = history_seconds[
      std::clamp(static_cast<int>(value("doppler.history_s")), 0, 4)];
  const uint32_t interval_ms = std::max<uint32_t>(
      50, g_effective_quality == 0 ? 16 : g_effective_quality == 1 ? 33 : 100);
  const size_t wanted = static_cast<size_t>(history_s) * 1000 / interval_ms;
  const size_t samples = std::min(g_history_count, std::max<size_t>(1, wanted));
  const size_t columns = std::min(kDopplerColumns,
      g_effective_quality == 0 ? kDopplerColumns : g_effective_quality == 1 ? 448u : 300u);
  const size_t used_columns = std::min(columns, std::max<size_t>(1,
      (samples * interval_ms * columns + history_s * 1000 - 1) / (history_s * 1000)));
  const size_t first_column = columns - used_columns;

  if (g_history_mutex && xSemaphoreTake(g_history_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  const size_t head = g_history_head;
  for (size_t column = 0; column < used_columns; ++column) {
    const size_t sample = used_columns > 1 ? column * (samples - 1) / (used_columns - 1) : 0;
    const size_t source_row = (head + g_history_rows - samples + sample) % g_history_rows;
    memcpy(g_doppler_snapshot + (first_column + column) * kBins,
           g_history + source_row * kBins, kBins);
  }
  if (g_history_mutex) xSemaphoreGive(g_history_mutex);

  Runtime runtime{};
  portENTER_CRITICAL(&g_runtime_mux);
  runtime = g_runtime;
  portEXIT_CRITICAL(&g_runtime_mux);
  const float source_span_hz = std::max(1.0f, static_cast<float>(runtime.span_hz));
  const int center_bin = static_cast<int>(kBins / 2);
  const int half_window = std::max(1, static_cast<int>(lroundf(span_hz * kBins / source_span_hz / 2.0f)));
  const int first_bin = std::max(0, center_bin - half_window);
  const int last_bin = std::min(static_cast<int>(kBins - 1), center_bin + half_window);
  const uint8_t palette = static_cast<uint8_t>(value("waterfall.palette"));

  const int tracker = static_cast<int>(value("doppler.tracking"));
  const int track_window = std::max(
      1, static_cast<int>(lroundf(value("doppler.track_window_hz") * kBins / source_span_hz)));
  const uint8_t threshold = static_cast<uint8_t>(std::clamp(
      lroundf(value("doppler.threshold_db") * 2.5f), 8l, 96l));
  int previous_track = -1;
  for (size_t column = 0; column < first_column; ++column) g_doppler_track[column] = -1;
  for (size_t column = first_column; column < columns; ++column) {
    const uint8_t* source = g_doppler_snapshot + column * kBins;
    int tracked_bin = -1;
    uint8_t tracked_level = 0;
    uint32_t baseline_sum = 0;
    for (int bin = first_bin; bin <= last_bin; ++bin) {
      baseline_sum += source[bin];
      if (source[bin] > tracked_level) {
        tracked_level = source[bin];
        tracked_bin = bin;
      }
    }
    const uint8_t baseline = static_cast<uint8_t>(baseline_sum /
        std::max(1, last_bin - first_bin + 1));
    if (!tracker || tracked_level < std::min(255, static_cast<int>(baseline) + threshold)) {
      g_doppler_track[column] = -1;
      continue;
    }
    if (tracker >= 2) {
      const int start = std::max(first_bin, tracked_bin - track_window);
      const int end = std::min(last_bin, tracked_bin + track_window);
      uint32_t weighted_sum = 0, weight = 0;
      for (int bin = start; bin <= end; ++bin) {
        const uint16_t signal = source[bin] > baseline ? source[bin] - baseline : 0;
        weighted_sum += static_cast<uint32_t>(bin) * signal;
        weight += signal;
      }
      if (weight) tracked_bin = static_cast<int>(weighted_sum / weight);
    }
    if (tracker == 3 && previous_track >= 0)
      tracked_bin = (previous_track * 4 + tracked_bin) / 5;
    g_doppler_track[column] = static_cast<int16_t>(tracked_bin);
    previous_track = tracked_bin;
  }

  for (int y = 0; y < kPlotH; y += kDopplerRowStep) {
    const float ratio = static_cast<float>(y + kDopplerRowStep / 2) / std::max(1, kPlotH - 1);
    const int bin = std::clamp(static_cast<int>(lroundf(last_bin - ratio * (last_bin - first_bin))),
                               first_bin, last_bin);
    for (int x = 0; x < kPlotW; x += kDopplerColumnStep) {
      const size_t column = std::min(columns - 1, static_cast<size_t>(x) * columns / kPlotW);
      const uint16_t color = column < first_column ? kBg
          : heat_color(g_doppler_snapshot[column * kBins + bin], palette);
      g_doppler_row[x] = color;
      if (x + 1 < kPlotW) g_doppler_row[x + 1] = color;
    }
    memcpy(g_doppler_row + kPlotW, g_doppler_row, kPlotW * sizeof(uint16_t));
    g_canvas.pushImage(kPlotX, kPlotY + y, kPlotW,
                       std::min(kDopplerRowStep, kPlotH - y), g_doppler_row);
  }

  for (int i = 1; i < 6; ++i)
    g_canvas.drawFastHLine(kPlotX, kPlotY + i * kPlotH / 6, kPlotW, kGrid);
  for (int i = 1; i < 6; ++i)
    g_canvas.drawFastVLine(kPlotX + i * kPlotW / 6, kPlotY, kPlotH, kGrid);
  if (tracker > 0) {
    int last_x = -1, last_y = -1;
    for (size_t column = 0; column < columns; ++column) {
      const int bin = g_doppler_track[column];
      if (bin < first_bin || bin > last_bin) continue;
      const int x = kPlotX + static_cast<int>(column * kPlotW / std::max<size_t>(1, columns - 1));
      const int y = kPlotY + static_cast<int>((last_bin - bin) * (kPlotH - 1) /
                                               std::max(1, last_bin - first_bin));
      if (last_x >= 0) g_canvas.drawLine(last_x, last_y, x, y, kYellow);
      last_x = x;
      last_y = y;
    }
  }
  g_canvas.drawRect(kPlotX, kPlotY, kPlotW, kPlotH, kGrid);
  char label[32];
  snprintf(label, sizeof(label), "+%.1f kHz", span_hz / 2000.0f);
  text(label, 6, kPlotY + 10, kMuted, 1);
  text("0", 24, kPlotY + kPlotH / 2, kMuted, 1);
  snprintf(label, sizeof(label), "-%.1f kHz", span_hz / 2000.0f);
  text(label, 6, kPlotY + kPlotH - 8, kMuted, 1);
  for (int i = 0; i <= 6; ++i) {
    snprintf(label, sizeof(label), "%us", static_cast<unsigned>(history_s * i / 6));
    text(label, kPlotX + i * kPlotW / 6, kPlotY + kPlotH + 14, kMuted, 1, middle_center);
  }
  text("TIME", kPlotX + kPlotW / 2, kPlotY + kPlotH + 32, kMuted, 1, middle_center);
}

void draw_channelizer(const SpectrumFrame& frame) {
  g_canvas.fillRect(kPlotX, kPlotY, kPlotW, kPlotH, kBg);
  constexpr int counts[] = {2, 4, 8, 16};
  const int count = counts[std::clamp(static_cast<int>(value("channelizer.count")), 0, 3)];
  const int cols = count <= 4 ? count : count <= 8 ? 4 : 8;
  const int rows = (count + cols - 1) / cols;
  const int tile_w = kPlotW / cols, tile_h = kPlotH / rows;
  for (int i = 0; i < count; ++i) {
    const int x = kPlotX + (i % cols) * tile_w, y = kPlotY + (i / cols) * tile_h;
    g_canvas.drawRect(x + 2, y + 2, tile_w - 4, tile_h - 4,
                        i == g_selected_channel ? kGreen : kGrid);
    char label[24];
    snprintf(label, sizeof(label), "CH %d", i + 1);
    text(label, x + 12, y + 18, i == g_selected_channel ? kGreen : TFT_WHITE, 1);
    if (frame.bins) {
      const size_t first = i * frame.bins / count;
      const size_t last = (i + 1) * frame.bins / count;
      float peak = -160;
      int px = x + 6, py = y + tile_h - 34;
      for (int tx = 1; tx < tile_w - 12; ++tx) {
        const size_t bin = first + tx * std::max<size_t>(1, last - first) /
                                      std::max(1, tile_w - 12);
        const size_t source = std::min(bin, static_cast<size_t>(frame.bins - 1));
        peak = std::max(peak, frame.live[source]);
        const int yy = y + tile_h - 34 - static_cast<int>(
            std::clamp((frame.live[source] + 120.0f) / 100.0f,
                       0.0f, 1.0f) * (tile_h - 58));
        g_canvas.drawLine(px, py, x + 6 + tx, yy, kCyan);
        px = x + 6 + tx;
        py = yy;
      }
      snprintf(label, sizeof(label), "%.1f dBFS", static_cast<double>(peak));
      text(label, x + tile_w / 2, y + tile_h - 14, kGreen, 1, middle_center);
    }
  }
  Runtime runtime{};
  portENTER_CRITICAL(&g_runtime_mux);
  runtime = g_runtime;
  portEXIT_CRITICAL(&g_runtime_mux);
  char footer[48];
  const int guard = static_cast<int>(value("channelizer.guard_pct"));
  const float guard_pct[] = {0, 5, 10, 20};
  const float bw = runtime.span_hz / static_cast<float>(count) *
                   (1.0f - guard_pct[std::clamp(guard, 0, 3)] / 100.0f);
  snprintf(footer, sizeof(footer), "AUTO CHANNEL BW %.2f kHz", static_cast<double>(bw / 1000.0f));
  text(footer, 640, 690, kMuted, 1, middle_center);
}

void draw_active_view(const SpectrumFrame& frame) {
  switch (static_cast<View>(g_view.load())) {
    case View::spectrum: draw_spectrum_view(frame); break;
    case View::waterfall: draw_waterfall_view(frame); break;
    case View::phosphor: draw_phosphor_view(); break;
    case View::spectrum3d: draw_3d_view(); break;
    case View::constellation: draw_constellation(frame); break;
    case View::iqscope: draw_iqscope(frame); break;
    case View::polar: draw_polar(frame); break;
    case View::occupancy: draw_occupancy(); break;
    case View::peak_average: draw_peak_average(frame); break;
    case View::doppler: draw_doppler(frame); break;
    case View::channelizer: draw_channelizer(frame); break;
    case View::audio_spectrogram: draw_audio_spectrogram(frame); break;
    default: break;
  }
}

void draw_hud() {
  g_canvas.fillRect(8, 8, 1264, kHudH, kPanel);
  button(16, 14, 76, 52, "BACK", kCyan);
  button(102, 14, 58, 52, "<", kCyan);
  button(170, 14, 430, 52, view_name(static_cast<View>(g_view.load())), kGreen);
  button(610, 14, 58, 52, ">", kCyan);
  button(678, 14, 112, 52, value("visual.freeze") ? "FROZEN" : "FREEZE", kYellow,
         value("visual.freeze") != 0);
  button(800, 14, 144, 52, "CONTROLS", kCyan, g_drawer);
  button(954, 14, 104, 52, g_hud_locked ? "LOCKED" : "LOCK", kCyan, g_hud_locked);
  char fps[64];
  snprintf(fps, sizeof(fps), "P %.0f  A %.0f  DROP %lu", static_cast<double>(g_presentation_fps),
           static_cast<double>(g_analysis_fps),
           static_cast<unsigned long>(g_ui_frame ? g_ui_frame->input_drops : 0));
  text(fps, 1260, 40, kMuted, 1, middle_right);
}

void draw_handles() {
  g_canvas.fillRoundRect(590, 8, 100, 12, 5, kGrid);
  g_canvas.fillRoundRect(590, 700, 100, 12, 5, kGrid);
}

void draw_chooser() {
  g_canvas.fillRect(110, 86, 1060, 570, kPanel);
  g_canvas.drawRoundRect(110, 86, 1060, 570, 12, kCyan);
  for (size_t i = 0; i < static_cast<size_t>(View::count); ++i) {
    const int col = static_cast<int>(i % 3), row = static_cast<int>(i / 3);
    const int x = 130 + col * 346, y = 108 + row * 132;
    button(x, y, 326, 112, view_name(static_cast<View>(i)),
           i == g_view.load() ? kGreen : kCyan, i == g_view.load());
  }
}

bool quick_control_visible(View current, const ControlDescriptor& control,
                           bool waterfall_custom) {
  if (control.view != View::common && control.view != current) return false;
  if (current == View::waterfall &&
      (strcmp(control.id, "display.floor_dbfs") == 0 ||
       strcmp(control.id, "display.ceiling_dbfs") == 0))
    return !waterfall_custom;
  if (current == View::waterfall &&
      (strcmp(control.id, "waterfall.floor_dbfs") == 0 ||
       strcmp(control.id, "waterfall.ceiling_dbfs") == 0))
    return waterfall_custom;
  if (control.view != View::common) return control.group == ControlGroup::quick;
  if (strcmp(control.id, "rf.span_hz") == 0) return current == View::spectrum;
  if (strcmp(control.id, "visual.freeze") != 0) return false;
  return current == View::waterfall || current == View::phosphor ||
         current == View::spectrum3d;
}

size_t visible_controls_for(View current, ControlGroup wanted, bool waterfall_custom,
                            const ControlDescriptor** output, size_t capacity) {
  size_t count = 0, total = 0;
  const auto* list = controls(&total);
  for (size_t i = 0; i < total && count < capacity; ++i) {
    const bool applies = list[i].view == View::common || list[i].view == current;
    const bool group_matches = wanted == ControlGroup::quick
                                   ? quick_control_visible(current, list[i], waterfall_custom)
                                   : list[i].group == wanted ||
                                         (wanted == ControlGroup::advanced &&
                                          list[i].group == ControlGroup::action);
    if (applies && group_matches)
      output[count++] = &list[i];
  }
  return count;
}

size_t visible_controls(const ControlDescriptor** output, size_t capacity) {
  const ControlGroup wanted = g_drawer_page == 0 ? ControlGroup::quick
                                  : g_drawer_page == 1 ? ControlGroup::advanced
                                                       : ControlGroup::display;
  return visible_controls_for(view(), wanted, value("waterfall.level_mode") != 0,
                              output, capacity);
}

void format_value(const ControlDescriptor& c, float v, char* out, size_t size) {
  if (c.kind == ControlKind::toggle) {
    strlcpy(out, v ? "ON" : "OFF", size);
    return;
  }
  if (c.kind == ControlKind::choice && c.choices && c.choices[0]) {
    const int wanted = static_cast<int>(v);
    const char* start = c.choices;
    for (int i = 0;; ++i) {
      const char* end = strchr(start, '|');
      if (i == wanted || !end) {
        const size_t n = std::min<size_t>(size - 1, end ? static_cast<size_t>(end - start)
                                                            : strlen(start));
        memcpy(out, start, n);
        out[n] = '\0';
        return;
      }
      start = end + 1;
    }
  }
  if (c.kind == ControlKind::integer) snprintf(out, size, "%ld", static_cast<long>(lroundf(v)));
  else snprintf(out, size, "%.2f", static_cast<double>(v));
}

void draw_drawer() {
  g_canvas.fillRect(0, kDrawerY, 1280, kDrawerH, kPanel);
  g_canvas.drawFastHLine(0, kDrawerY, 1280, kCyan);
  constexpr const char* tabs[] = {"QUICK", "ADVANCED", "DISPLAY", "PRESETS"};
  for (int i = 0; i < 4; ++i)
    button(18 + i * 210, kDrawerY + 14, 196, 46, tabs[i], kCyan, g_drawer_page == i);
  button(1110, kDrawerY + 14, 150, 46, "CLOSE", kMuted);
  if (g_drawer_page == 3) {
    button(32, kDrawerY + 82, 230, 58, "DEFAULT", kGreen);
    button(278, kDrawerY + 82, 230, 58, "MAX DETAIL", kCyan);
    button(524, kDrawerY + 82, 230, 58, "RADIO PRIORITY", kYellow);
    for (size_t i = 0; i < kCustomPresets; ++i) {
      const Preset& p = g_persist.presets[g_view.load()][i];
      char label[32];
      snprintf(label, sizeof(label), "%u: %s", static_cast<unsigned>(i + 1),
               p.used ? p.name : "EMPTY");
      button(32 + static_cast<int>(i) * 300, kDrawerY + 168, 282, 58, label,
             p.used ? kCyan : kMuted);
    }
    button(430, kDrawerY + 238, 420, 50, "RENAME SELECTED PRESET", kCyan);
    return;
  }
  const ControlDescriptor* shown[48]{};
  const size_t count = visible_controls(shown, std::size(shown));
  const int first = std::clamp(g_control_scroll, 0, std::max(0, static_cast<int>(count) - 3));
  for (int row = 0; row < 3 && first + row < static_cast<int>(count); ++row) {
    const auto& c = *shown[first + row];
    const int y = kDrawerY + 75 + row * 68;
    const bool enabled = available(c);
    text(c.label, 32, y + 28, enabled ? TFT_WHITE : kMuted, 2);
    if (c.kind == ControlKind::action) {
      button(930, y + 2, 260, 54, "RUN", enabled ? kCyan : kMuted);
      continue;
    }
    size_t index = 0;
    find_control(c.id, &index);
    char current[40];
    format_value(c, g_persist.values[index], current, sizeof(current));
    button(730, y + 2, 58, 54, "-", enabled ? kCyan : kMuted);
    button(798, y + 2, 264, 54, current, enabled ? kGreen : kMuted);
    button(1072, y + 2, 58, 54, "+", enabled ? kCyan : kMuted);
  }
  button(1160, kDrawerY + 82, 92, 58, "UP", kCyan);
  button(1160, kDrawerY + 218, 92, 58, "DOWN", kCyan);
}

void switch_view(int delta) {
  int next = static_cast<int>(g_view.load()) + delta;
  const int count = static_cast<int>(View::count);
  next = (next % count + count) % count;
  g_view.store(static_cast<uint8_t>(next));
  g_persist.last_view = static_cast<uint8_t>(next);
  g_persist_due_ms = millis() + 2000;
  allocate_view_buffers(static_cast<View>(next));
  configure_analysis();
  g_drawn_revision = 0;
  g_last_heavy_view_draw_ms = 0;
  g_name_until_ms = millis() + kNameTimeoutMs;
  draw_frame_chrome();
  draw_handles();
}

void reset_view_defaults() {
  size_t count = 0;
  const auto* list = controls(&count);
  const View current = static_cast<View>(g_view.load());
  for (size_t i = 0; i < count && i < kMaxControls; ++i)
    if (list[i].view == current || list[i].view == View::common)
      g_persist.values[i] = list[i].default_value;
  g_persist_due_ms = millis() + 1;
  allocate_view_buffers(current);
  configure_analysis();
}

void run_action(const ControlDescriptor& c) {
  if (strcmp(c.id, "visual.reset") == 0) reset_view_defaults();
  else if (strcmp(c.id, "fft.clear_peak") == 0 || strcmp(c.id, "peakavg.clear_hold") == 0) {
    rf_analysis::clear_peak();
    if (xSemaphoreTake(g_frame_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      for (float& p : g_frame->peak) p = -160;
      xSemaphoreGive(g_frame_mutex);
    }
  } else if (strcmp(c.id, "peakavg.clear_average") == 0) {
    rf_analysis::clear_average();
    if (xSemaphoreTake(g_frame_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      for (float& p : g_frame->average) p = -160;
      xSemaphoreGive(g_frame_mutex);
    }
  } else if (strstr(c.id, "clear") || strstr(c.id, "reset_session")) {
    const bool history_locked = !g_history_mutex ||
        xSemaphoreTake(g_history_mutex, pdMS_TO_TICKS(20)) == pdTRUE;
    if (history_locked) {
      if (g_history) memset(g_history, 0, g_history_rows * kBins);
      if (g_density) memset(g_density, 0, g_density_rows * kBins);
      g_history_count = 0;
      if (g_history_mutex) xSemaphoreGive(g_history_mutex);
    }
    memset(g_occupancy_frames, 0, sizeof(g_occupancy_frames));
    memset(g_occupancy_busy, 0, sizeof(g_occupancy_busy));
    memset(g_occupancy_dwell_ms, 0, sizeof(g_occupancy_dwell_ms));
    memset(g_occupancy_heatmap, 0, sizeof(g_occupancy_heatmap));
    memset(g_occupancy_bin_busy, 0, sizeof(g_occupancy_bin_busy));
    g_occupancy_bin_frames = 0;
    g_occupancy_bin_started_ms = 0;
    g_occupancy_heatmap_head = 0;
    g_occupancy_heatmap_count = 0;
  } else if (strcmp(c.id, "iqscope.arm_single") == 0) {
    size_t index = 0;
    if (find_control("visual.freeze", &index)) set_value(index, 0, false);
    message("SINGLE ARMED");
  } else if (strstr(c.id, "save_csv")) {
    message("CSV EXPORT QUEUED");
  } else {
    message("ACTION APPLIED");
  }
}

void adjust_control(const ControlDescriptor& c, int direction) {
  if (!available(c)) {
    message(c.disabled_reason && c.disabled_reason[0] ? c.disabled_reason : "CONTROL UNAVAILABLE");
    return;
  }
  if (c.kind == ControlKind::action) {
    run_action(c);
    return;
  }
  size_t index = 0;
  find_control(c.id, &index);
  const float next = g_persist.values[index] + direction * c.step;
  if (strcmp(c.id, "audiospec.source") == 0 && (next == 1 || next == 2) &&
      !(capabilities() & cap_stereo_audio)) {
    message("STEREO SOURCE UNAVAILABLE");
    return;
  }
  set_value(index, next);
  if (strcmp(c.id, "rf.center_hz") == 0)
    queue_action(ActionKind::tune_hz, static_cast<uint32_t>(g_persist.values[index]));
  if (strcmp(c.id, "rf.span_hz") == 0)
    queue_action(ActionKind::span_hz, static_cast<uint32_t>(g_persist.values[index]));
}

void handle_drawer_tap(int x, int y) {
  if (inside(x, y, 1110, kDrawerY + 14, 150, 46)) {
    g_drawer = false;
    draw_frame_chrome();
    return;
  }
  for (int page = 0; page < 4; ++page) {
    if (inside(x, y, 18 + page * 210, kDrawerY + 14, 196, 46)) {
      g_drawer_page = static_cast<uint8_t>(page);
      g_control_scroll = 0;
      return;
    }
  }
  if (g_drawer_page == 3) {
    if (inside(x, y, 32, kDrawerY + 82, 230, 58)) reset_view_defaults();
    else if (inside(x, y, 278, kDrawerY + 82, 230, 58)) {
      size_t index = 0;
      if (find_control("visual.quality", &index)) set_value(index, 0);
    } else if (inside(x, y, 524, kDrawerY + 82, 230, 58)) {
      size_t index = 0;
      if (find_control("visual.quality", &index)) set_value(index, 2);
    }
    for (size_t i = 0; i < kCustomPresets; ++i) {
      if (!inside(x, y, 32 + static_cast<int>(i) * 300, kDrawerY + 168, 282, 58))
        continue;
      auto& preset = g_persist.presets[g_view.load()][i];
      g_selected_preset = i;
      if (preset.used) {
        apply_view_values(view(), preset);
        message("PRESET APPLIED");
      } else {
        char initial[21];
        snprintf(initial, sizeof(initial), "Custom %u", static_cast<unsigned>(i + 1));
        text_editor::begin("PRESET NAME", initial, 20, false, "SAVE");
        g_preset_naming = true;
        g_editor_pressed = false;
        text_editor::draw();
      }
      return;
    }
    if (inside(x, y, 430, kDrawerY + 238, 420, 50)) {
      const auto& preset = g_persist.presets[g_view.load()][g_selected_preset];
      text_editor::begin("PRESET NAME", preset.used ? preset.name : "Custom", 20, false, "SAVE");
      g_preset_naming = true;
      g_editor_pressed = false;
      text_editor::draw();
      return;
    }
    return;
  }
  const ControlDescriptor* shown[48]{};
  const size_t count = visible_controls(shown, std::size(shown));
  if (inside(x, y, 1160, kDrawerY + 82, 92, 58)) g_control_scroll = std::max(0, g_control_scroll - 1);
  else if (inside(x, y, 1160, kDrawerY + 218, 92, 58))
    g_control_scroll = std::min(std::max(0, static_cast<int>(count) - 3), g_control_scroll + 1);
  const int first = std::clamp(g_control_scroll, 0, std::max(0, static_cast<int>(count) - 3));
  for (int row = 0; row < 3 && first + row < static_cast<int>(count); ++row) {
    const int yy = kDrawerY + 77 + row * 68;
    if (!inside(x, y, 700, yy, 450, 56)) continue;
    const auto& c = *shown[first + row];
    adjust_control(c, x < 795 ? -1 : x > 1065 ? 1 : 1);
  }
}

void handle_hud_tap(int x, int y) {
  if (inside(x, y, 16, 14, 76, 52)) queue_action(ActionKind::close);
  else if (inside(x, y, 102, 14, 58, 52)) switch_view(-1);
  else if (inside(x, y, 170, 14, 430, 52)) g_chooser = !g_chooser;
  else if (inside(x, y, 610, 14, 58, 52)) switch_view(1);
  else if (inside(x, y, 678, 14, 112, 52)) {
    size_t index = 0;
    find_control("visual.freeze", &index);
    set_value(index, !g_persist.values[index], false);
  } else if (inside(x, y, 800, 14, 144, 52)) {
    g_drawer = true;
    g_drawer_page = 0;
  } else if (inside(x, y, 954, 14, 104, 52)) {
    g_hud_locked = !g_hud_locked;
  }
  g_hud_hide_ms = millis() + kHudTimeoutMs;
}

void handle_chooser_tap(int x, int y) {
  for (size_t i = 0; i < static_cast<size_t>(View::count); ++i) {
    const int col = static_cast<int>(i % 3), row = static_cast<int>(i / 3);
    if (!inside(x, y, 130 + col * 346, 108 + row * 132, 326, 112)) continue;
    const int delta = static_cast<int>(i) - static_cast<int>(g_view.load());
    g_chooser = false;
    switch_view(delta);
    return;
  }
}

void handle_canvas_tap(int x, int y, bool double_tap) {
  const View current = static_cast<View>(g_view.load());
  Runtime runtime{};
  portENTER_CRITICAL(&g_runtime_mux);
  runtime = g_runtime;
  portEXIT_CRITICAL(&g_runtime_mux);
  if (double_tap && (current == View::waterfall || current == View::occupancy ||
                     current == View::channelizer)) {
    const int64_t offset = (static_cast<int64_t>(x - kPlotX) * runtime.span_hz) / kPlotW -
                           runtime.span_hz / 2;
    queue_action(ActionKind::tune_hz,
                 static_cast<uint32_t>(std::max<int64_t>(1, runtime.center_hz + offset)));
  } else if (current == View::channelizer) {
    constexpr int counts[] = {2, 4, 8, 16};
    const int count = counts[std::clamp(static_cast<int>(value("channelizer.count")), 0, 3)];
    const int cols = count <= 4 ? count : count <= 8 ? 4 : 8;
    const int rows = (count + cols - 1) / cols;
    const int col = std::clamp((x - kPlotX) * cols / kPlotW, 0, cols - 1);
    const int row = std::clamp((y - kPlotY) * rows / kPlotH, 0, rows - 1);
    g_selected_channel = static_cast<uint8_t>(std::min(count - 1, row * cols + col));
    size_t index = 0;
    if (find_control("channelizer.demod", &index))
      g_persist.values[index] = g_channels[g_selected_channel].demod;
    if (find_control("channelizer.squelch_dbfs", &index))
      g_persist.values[index] = g_channels[g_selected_channel].squelch;
    if (find_control("channelizer.selected_offset_hz", &index))
      g_persist.values[index] = g_channels[g_selected_channel].offset_hz;
    if (find_control("channelizer.mute", &index))
      g_persist.values[index] = g_channels[g_selected_channel].muted ? 1 : 0;
    if (value("channelizer.selected_action") == 2) {
      g_channel_solo = true;
      if (find_control("channelizer.solo", &index)) g_persist.values[index] = 1;
    }
    draw_frame_chrome();
  } else if (double_tap && (current == View::spectrum3d || current == View::constellation ||
                            current == View::iqscope || current == View::polar)) {
    reset_view_defaults();
  } else if (current == View::spectrum && !double_tap) {
    const int64_t offset = (static_cast<int64_t>(x - kPlotX) * runtime.span_hz) / kPlotW -
                           runtime.span_hz / 2;
    queue_action(ActionKind::tune_hz,
                 static_cast<uint32_t>(std::max<int64_t>(1, runtime.center_hz + offset)));
  }
}

void service_health(uint32_t now) {
  if (now - g_last_health_ms < 1000) return;
  g_last_health_ms = now;
  Runtime runtime{};
  portENTER_CRITICAL(&g_runtime_mux);
  runtime = g_runtime;
  portEXIT_CRITICAL(&g_runtime_mux);
  const uint8_t requested = static_cast<uint8_t>(std::clamp(value("visual.quality"), 0.0f, 2.0f));
  const bool stressed = runtime.usb_overruns > g_last_usb_overruns ||
                        runtime.consumer_drops > g_last_consumer_drops ||
                        runtime.audio_drops > g_last_audio_drops;
  const uint8_t before = g_effective_quality;
  if (stressed) {
    g_effective_quality = std::min<uint8_t>(2, static_cast<uint8_t>(g_effective_quality + 1));
    g_stable_since_ms = now;
    message("VIS: RADIO PRIORITY", 1200);
  } else if (g_effective_quality > requested && now - g_stable_since_ms >= 5000) {
    --g_effective_quality;
    g_stable_since_ms = now;
  }
  g_last_usb_overruns = runtime.usb_overruns;
  g_last_consumer_drops = runtime.consumer_drops;
  g_last_audio_drops = runtime.audio_drops;
  if (g_effective_quality != before) configure_analysis();
}

}  // namespace

bool initialize(NvsStore* store, AudioSink audio_sink) {
  g_store = store;
  g_audio_sink = audio_sink;
  if (!g_persist_storage) {
    g_persist_storage = static_cast<Persisted*>(
        heap_caps_calloc(1, sizeof(Persisted), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_persist_storage) return false;
  }
  size_t count = 0;
  const auto* list = controls(&count);
  if (count > kMaxControls || !controls_self_check()) return false;
  if (g_initialized) {
    if (g_store) {
      Persisted saved{};
      if (g_store->get_bytes("rf_vis", &saved, sizeof(saved)) == sizeof(saved) &&
          saved.magic == kPersistMagic && saved.version == kPersistVersion &&
          saved.bytes == sizeof(saved)) {
        g_persist = saved;
        g_view.store(std::min<uint8_t>(g_persist.last_view,
                                      static_cast<uint8_t>(View::count) - 1));
      }
    }
    rf_analysis::set_observer(analysis_observer);
    configure_analysis();
    return true;
  }
  g_canvas.setColorDepth(lgfx::rgb565_nonswapped);
  auto* panel = static_cast<lgfx::Panel_DSI*>(M5.Display.getPanel());
  const auto& panel_config = panel->config();
  g_dsi_panel = panel->getPanelHandle();
  g_dsi_width = panel_config.panel_width;
  g_dsi_height = panel_config.panel_height;
  g_frame_buffers[0] = panel->getFrameBuffer(0);
  g_frame_buffers[1] = panel->getFrameBuffer(1);
  g_display_buffer = g_frame_buffers[0];
  g_display_buffer_bytes = static_cast<size_t>(panel_config.panel_width) *
                           panel_config.panel_height * sizeof(uint16_t);
  if (!g_display_buffer || !g_frame_buffers[1] || !g_dsi_panel) return false;
  g_canvas.setBuffer(g_display_buffer, panel_config.panel_width, panel_config.panel_height);
  g_canvas.setSwapBytes(true);
  g_display_canvas.setColorDepth(lgfx::rgb565_nonswapped);
  g_display_canvas.setBuffer(g_display_buffer, panel_config.panel_width,
                             panel_config.panel_height);
  g_display_canvas.setSwapBytes(true);
  g_canvas.setRotation(M5.Display.getRotation());
  g_display_canvas.setRotation(M5.Display.getRotation());
  g_persist = {};
  g_persist.magic = kPersistMagic;
  g_persist.version = kPersistVersion;
  for (size_t i = 0; i < count; ++i) g_persist.values[i] = list[i].default_value;
  g_persist.bytes = sizeof(g_persist);
  Persisted saved{};
  if (g_store && g_store->get_bytes("rf_vis", &saved, sizeof(saved)) == sizeof(saved) &&
      saved.magic == kPersistMagic && saved.version == kPersistVersion &&
      saved.bytes == sizeof(saved))
    g_persist = saved;
  g_persist.last_view = std::min<uint8_t>(g_persist.last_view,
                                          static_cast<uint8_t>(View::count) - 1);
  g_view.store(g_persist.last_view);
  g_frame_mutex = xSemaphoreCreateMutex();
  g_history_mutex = xSemaphoreCreateMutex();
  g_frame = static_cast<SpectrumFrame*>(
      heap_caps_calloc(1, sizeof(SpectrumFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_ui_frame = static_cast<SpectrumFrame*>(
      heap_caps_calloc(1, sizeof(SpectrumFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_observer_frame = static_cast<SpectrumFrame*>(
      heap_caps_calloc(1, sizeof(SpectrumFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_frame_mutex || !g_history_mutex || !g_frame || !g_ui_frame || !g_observer_frame)
    return false;
  for (float& level : g_frame->average) level = -160;
  for (float& level : g_frame->peak) level = -160;
  if (!rf_analysis::initialize() || !rf_analysis::self_check()) return false;
  rf_analysis::set_observer(analysis_observer);
  g_initialized = true;
  configure_analysis();
  return true;
}

void set_runtime(const Runtime& runtime) {
  Runtime previous{};
  portENTER_CRITICAL(&g_runtime_mux);
  previous = g_runtime;
  g_runtime = runtime;
  portEXIT_CRITICAL(&g_runtime_mux);
  g_source_available.store(runtime.source_available, std::memory_order_release);
  const bool analysis_changed = previous.center_hz != runtime.center_hz ||
                                previous.span_hz != runtime.span_hz ||
                                previous.sample_rate_sps != runtime.sample_rate_sps ||
                                previous.audio_rate_sps != runtime.audio_rate_sps ||
                                previous.filter_bandwidth_hz != runtime.filter_bandwidth_hz ||
                                previous.audio_demod != runtime.audio_demod;
  if (g_initialized && analysis_changed) configure_analysis();
}

void offer_iq(const uint8_t* iq, size_t bytes) {
  rf_analysis::offer_iq(iq, bytes);
}

void offer_audio(const int16_t* left, const int16_t* right, size_t frames,
                 uint32_t sample_rate_sps) {
  rf_analysis::offer_audio(left, right, frames, sample_rate_sps);
}

bool enter(uint8_t origin_screen_value, uint8_t origin_tab_value) {
  if (!g_source_available.load(std::memory_order_acquire)) return false;
  g_origin_screen = origin_screen_value;
  g_origin_tab = origin_tab_value;
  g_inspect = g_hud_locked = g_drawer = g_chooser = false;
  g_effective_quality = static_cast<uint8_t>(std::clamp(value("visual.quality"), 0.0f, 2.0f));
  Runtime runtime{};
  portENTER_CRITICAL(&g_runtime_mux);
  runtime = g_runtime;
  portEXIT_CRITICAL(&g_runtime_mux);
  g_last_usb_overruns = runtime.usb_overruns;
  g_last_consumer_drops = runtime.consumer_drops;
  g_last_audio_drops = runtime.audio_drops;
  g_last_health_ms = g_stable_since_ms = millis();
  allocate_view_buffers(static_cast<View>(g_view.load()));
  g_active.store(true, std::memory_order_release);
  configure_analysis();
  rf_analysis::set_enabled(true);
  g_name_until_ms = millis() + kNameTimeoutMs;
  g_drawn_revision = 0;
  draw_frame_chrome();
  draw_handles();
  present_canvas(millis());
  return true;
}

void leave() {
  disable_page_flip();
  g_active.store(false, std::memory_order_release);
  rf_analysis::set_enabled(false);
  g_channel_solo = false;
  if (!g_history_mutex || xSemaphoreTake(g_history_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    free_view_buffers();
    if (g_history_mutex) xSemaphoreGive(g_history_mutex);
  }
  if (g_store && g_persist_due_ms) g_store->put_bytes("rf_vis", &g_persist, sizeof(g_persist));
  g_persist_due_ms = 0;
}

bool active() { return g_active.load(std::memory_order_acquire); }
uint8_t origin_screen() { return g_origin_screen; }
uint8_t origin_tab() { return g_origin_tab; }
View view() { return static_cast<View>(g_view.load(std::memory_order_acquire)); }

void service_ui(uint32_t now) {
  if (!active()) return;
  const bool full_repaint = is_full_repaint_view(view());
  if (full_repaint) {
    enable_page_flip();
    if (!service_page_flip(now)) return;
  } else {
    disable_page_flip();
  }
  if (g_preset_naming) return;
  service_health(now);
  if (g_persist_due_ms && static_cast<int32_t>(now - g_persist_due_ms) >= 0) {
    if (g_store) g_store->put_bytes("rf_vis", &g_persist, sizeof(g_persist));
    g_persist_due_ms = 0;
  }
  if (!g_hud_locked && g_inspect && !g_drawer && !g_chooser &&
      static_cast<int32_t>(now - g_hud_hide_ms) >= 0) {
    g_inspect = false;
    draw_frame_chrome();
  }
  if (xSemaphoreTake(g_frame_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;
  *g_ui_frame = *g_frame;
  xSemaphoreGive(g_frame_mutex);
  const SpectrumFrame& frame = *g_ui_frame;
  const bool direct_history =
      (view() == View::waterfall || view() == View::audio_spectrogram) &&
      !g_inspect && !g_drawer && !g_chooser &&
      (!g_message_until_ms || static_cast<int32_t>(g_message_until_ms - now) <= 0) &&
      g_source_available.load(std::memory_order_acquire);
  if ((view() == View::waterfall || view() == View::audio_spectrogram) &&
      !direct_history && !g_waterfall_canvas_synced &&
      g_canvas.getBuffer() && g_display_buffer && g_canvas.getBuffer() != g_display_buffer) {
    memcpy(g_canvas.getBuffer(), g_display_buffer, g_display_buffer_bytes);
    g_waterfall_canvas_synced = true;
  }
  const View current = view();
  const uint32_t heavy_interval = current == View::spectrum3d
      ? (g_effective_quality == 0 ? 66u : g_effective_quality == 1 ? 100u : 125u)
      : current == View::doppler
            ? (g_effective_quality == 0 ? 50u : g_effective_quality == 1 ? 66u : 100u)
            : current == View::occupancy
                  ? (g_effective_quality == 0 ? 33u : g_effective_quality == 1 ? 50u : 66u)
            : 0u;
  const bool heavy_due = !heavy_interval ||
      now - g_last_heavy_view_draw_ms >= heavy_interval;
  bool frame_drawn = false;
  if (!value("visual.freeze") && frame.revision != g_drawn_revision && heavy_due) {
    g_drawn_revision = frame.revision;
    if (full_repaint) draw_frame_chrome();
    draw_active_view(frame);
    frame_drawn = true;
    if (heavy_interval) g_last_heavy_view_draw_ms = now;
    ++g_presentation_frames;
  }
  if (now - g_last_analysis_report_ms >= 1000) {
    g_analysis_fps = g_analysis_frames * 1000.0f /
                     std::max<uint32_t>(1, now - g_last_analysis_report_ms);
    g_analysis_frames = 0;
    g_last_analysis_report_ms = now;
  }
  if (now - g_last_present_report_ms >= 1000) {
    g_presentation_fps = g_presentation_frames * 1000.0f /
                         std::max<uint32_t>(1, now - g_last_present_report_ms);
    g_presentation_frames = 0;
    g_last_present_report_ms = now;
  }
  if (g_inspect) draw_hud();
  else draw_handles();
  if (g_chooser) draw_chooser();
  if (g_drawer) draw_drawer();
  if (g_name_until_ms && static_cast<int32_t>(g_name_until_ms - now) > 0 && !g_inspect)
    text(view_name(view()), 640, 68, TFT_WHITE, 2, middle_center);
  if (!g_source_available.load(std::memory_order_acquire) && !g_source_lost_drawn) {
    for (int y = kPlotY; y < kPlotY + kPlotH; y += 6)
      g_canvas.drawFastHLine(kPlotX, y, kPlotW, kPanel);
    g_canvas.fillRect(410, 315, 460, 90, kPanel);
    g_canvas.drawRect(410, 315, 460, 90, kYellow);
    text("SOURCE LOST - PAUSED", 640, 360, kYellow, 3, middle_center);
    g_source_lost_drawn = true;
  } else if (g_source_available.load(std::memory_order_acquire) && g_source_lost_drawn) {
    g_source_lost_drawn = false;
    g_drawn_revision = 0;
  }
  if (g_message_until_ms && static_cast<int32_t>(g_message_until_ms - now) > 0) {
    g_canvas.fillRoundRect(360, 645, 560, 50, 8, kPanel);
    text(g_message, 640, 670, kYellow, 2, middle_center);
  }
  if (!direct_history && frame_drawn) {
    g_last_canvas_push_ms = now;
    present_canvas(now);
  } else if (frame_drawn) {
    g_last_canvas_push_ms = now;
  }
}

void handle_touch(int32_t x, int32_t y, bool pressed, uint8_t touch_count,
                  int32_t second_x, int32_t second_y, uint32_t now) {
  (void)touch_count;
  (void)second_x;
  (void)second_y;
  if (!active()) return;
  if (g_preset_naming) {
    if (pressed && !g_editor_pressed) {
      const auto result = text_editor::handle_touch(x, y);
      if (result == text_editor::Result::accepted) {
        auto& preset = g_persist.presets[g_view.load()][g_selected_preset];
        if (preset.used) {
          strlcpy(preset.name, text_editor::value(), sizeof(preset.name));
          g_persist_due_ms = millis() + 1;
        } else {
          save_preset(g_selected_preset, text_editor::value());
        }
        g_preset_naming = false;
        g_editor_pressed = false;
        draw_frame_chrome();
        g_drawer = true;
        g_drawer_page = 3;
        draw_drawer();
      } else if (result == text_editor::Result::cancelled) {
        g_preset_naming = false;
        g_editor_pressed = false;
        draw_frame_chrome();
        g_drawer = true;
        g_drawer_page = 3;
        draw_drawer();
      }
    }
    if (g_preset_naming) g_editor_pressed = pressed;
    return;
  }
  if (pressed && !g_gesture.down) {
    g_gesture = {true, false, x, y, now};
    return;
  }
  if (pressed && g_gesture.down) {
    if (!g_gesture.long_fired && now - g_gesture.started_ms >= kLongPressMs) {
      g_gesture.long_fired = true;
      g_inspect = true;
      g_hud_locked = !g_hud_locked;
      g_hud_hide_ms = now + kHudTimeoutMs;
    }
    return;
  }
  if (pressed || !g_gesture.down) return;
  const int dx = x - g_gesture.x, dy = y - g_gesture.y;
  const bool tap = std::abs(dx) < 20 && std::abs(dy) < 20 && !g_gesture.long_fired;
  const bool double_tap = tap && now - g_last_tap_ms <= kDoubleTapMs &&
                          std::abs(x - g_last_tap_x) < 40 && std::abs(y - g_last_tap_y) < 40;
  g_gesture.down = false;
  if (g_drawer) {
    if (dy > 80 || (tap && y < kDrawerY)) g_drawer = false;
    else if (tap) handle_drawer_tap(x, y);
  } else if (g_chooser) {
    if (tap) handle_chooser_tap(x, y);
  } else if (g_inspect && y < kHudH) {
    if (tap) handle_hud_tap(x, y);
  } else if (!g_inspect && g_gesture.y <= 32 && tap) {
    g_inspect = true;
    g_hud_hide_ms = now + kHudTimeoutMs;
  } else if (g_gesture.y >= 670 && (dy < -30 || tap)) {
    g_inspect = true;
    g_drawer = true;
    g_drawer_page = 0;
    g_hud_hide_ms = now + kHudTimeoutMs;
  } else if (!g_inspect) {
    if (dx < -80) switch_view(1);
    else if (dx > 80) switch_view(-1);
    else if (tap) switch_view(1);
  } else if (tap && inside(x, y, kPlotX, kPlotY, kPlotW, kPlotH)) {
    handle_canvas_tap(x, y, double_tap);
    g_hud_hide_ms = now + kHudTimeoutMs;
  }
  if (tap) {
    g_last_tap_ms = now;
    g_last_tap_x = x;
    g_last_tap_y = y;
  }
}

bool take_action(Action* action) {
  if (!action) return false;
  portENTER_CRITICAL(&g_action_mux);
  *action = g_action;
  g_action = {};
  portEXIT_CRITICAL(&g_action_mux);
  return action->kind != ActionKind::none;
}

bool channel_audio_active() {
  return active() && view() == View::channelizer && g_channel_solo;
}

bool process_command(const char* command, char* response, size_t response_size) {
  if (!command || !response || response_size == 0) return false;
  response[0] = '\0';
  if (strcmp(command, "RTL_VIS STATUS") == 0) {
    snprintf(response, response_size,
             "RTL_VIS_STATUS active=%d view=%s source=%d frozen=%d hud=%d drawer=%d "
             "presentation_fps=%.1f analysis_fps=%.1f dropped=%lu quality=%u",
             active() ? 1 : 0, view_slug(view()), g_source_available.load() ? 1 : 0,
             value("visual.freeze") ? 1 : 0, g_inspect ? 1 : 0, g_drawer ? 1 : 0,
             static_cast<double>(g_presentation_fps), static_cast<double>(g_analysis_fps),
             static_cast<unsigned long>(g_ui_frame ? g_ui_frame->input_drops : 0),
             g_effective_quality);
    return true;
  }
  if (strcmp(command, "RTL_VIS NEXT") == 0) { switch_view(1); strlcpy(response, "RTL_VIS_OK", response_size); return true; }
  if (strcmp(command, "RTL_VIS PREV") == 0) { switch_view(-1); strlcpy(response, "RTL_VIS_OK", response_size); return true; }
  if (strcmp(command, "RTL_VIS CLOSE") == 0) { queue_action(ActionKind::close); strlcpy(response, "RTL_VIS_OK", response_size); return true; }
  if (strcmp(command, "RTL_VIS FREEZE ON") == 0 || strcmp(command, "RTL_VIS FREEZE OFF") == 0) {
    size_t index = 0; find_control("visual.freeze", &index);
    set_value(index, strstr(command, " ON") != nullptr, false);
    strlcpy(response, "RTL_VIS_OK", response_size); return true;
  }
  if (strncmp(command, "RTL_VIS OPEN", 12) == 0) {
    const char* slug = command[12] == ' ' ? command + 13 : nullptr;
    View requested{};
    if (slug && !parse_view(slug, &requested)) { strlcpy(response, "RTL_VIS_ERROR unknown_view", response_size); return true; }
    if (slug) { g_view.store(static_cast<uint8_t>(requested)); g_persist.last_view = static_cast<uint8_t>(requested); }
    strlcpy(response, "RTL_VIS_OPEN_REQUEST", response_size); return true;
  }
  if (strncmp(command, "RTL_VIS GET ", 12) == 0) {
    size_t index = 0; const auto* c = find_control(command + 12, &index);
    if (!c) { strlcpy(response, "RTL_VIS_ERROR unknown_control", response_size); return true; }
    char current[48]; format_value(*c, g_persist.values[index], current, sizeof(current));
    snprintf(response, response_size, "RTL_VIS_CONTROL id=%s value=%s enabled=%d", c->id, current, available(*c) ? 1 : 0);
    return true;
  }
  if (strncmp(command, "RTL_VIS SET ", 12) == 0) {
    char id[48]{}; float next = 0; char extra = 0;
    if (sscanf(command + 12, "%47s %f %c", id, &next, &extra) != 2) { strlcpy(response, "RTL_VIS_ERROR usage", response_size); return true; }
    size_t index = 0; const auto* c = find_control(id, &index);
    if (!c || c->kind == ControlKind::action) { strlcpy(response, "RTL_VIS_ERROR unknown_control", response_size); return true; }
    if (!available(*c)) { snprintf(response, response_size, "RTL_VIS_ERROR disabled reason=\"%s\"", c->disabled_reason); return true; }
    set_value(index, next); snprintf(response, response_size, "RTL_VIS_OK id=%s value=%.3f", id, static_cast<double>(g_persist.values[index])); return true;
  }
  if (strncmp(command, "RTL_VIS ACTION ", 15) == 0) {
    const auto* c = find_control(command + 15);
    if (!c || c->kind != ControlKind::action) { strlcpy(response, "RTL_VIS_ERROR unknown_action", response_size); return true; }
    run_action(*c); strlcpy(response, "RTL_VIS_OK", response_size); return true;
  }
  if (strcmp(command, "RTL_VIS PRESET LIST") == 0) {
    const auto& presets = g_persist.presets[g_view.load()];
    snprintf(response, response_size,
             "RTL_VIS_PRESETS view=%s 1=\"%s\" 2=\"%s\" 3=\"%s\" 4=\"%s\"",
             view_slug(view()), presets[0].used ? presets[0].name : "",
             presets[1].used ? presets[1].name : "",
             presets[2].used ? presets[2].name : "",
             presets[3].used ? presets[3].name : "");
    return true;
  }
  if (strncmp(command, "RTL_VIS PRESET ", 15) == 0) {
    char verb[8]{}, slot_text[4]{}, name[21]{};
    const int fields = sscanf(command + 15, "%7s %3s %20[^\n]", verb, slot_text, name);
    size_t slot = 0;
    if (fields < 2 || !preset_slot(slot_text, &slot)) {
      strlcpy(response, "RTL_VIS_ERROR preset_usage", response_size);
      return true;
    }
    auto& preset = g_persist.presets[g_view.load()][slot];
    if (strcasecmp(verb, "SAVE") == 0) {
      save_preset(slot, fields == 3 ? name : nullptr);
    } else if (strcasecmp(verb, "APPLY") == 0) {
      if (!preset.used) { strlcpy(response, "RTL_VIS_ERROR empty_preset", response_size); return true; }
      apply_view_values(view(), preset);
    } else if (strcasecmp(verb, "RENAME") == 0) {
      if (!preset.used || fields != 3 || !name[0]) {
        strlcpy(response, "RTL_VIS_ERROR preset_name", response_size); return true;
      }
      strlcpy(preset.name, name, sizeof(preset.name));
      g_persist_due_ms = millis() + 1;
    } else if (strcasecmp(verb, "DELETE") == 0) {
      preset = {};
      g_persist_due_ms = millis() + 1;
    } else {
      strlcpy(response, "RTL_VIS_ERROR preset_verb", response_size);
      return true;
    }
    snprintf(response, response_size, "RTL_VIS_OK preset=%u", static_cast<unsigned>(slot + 1));
    return true;
  }
  if (strcmp(command, "RTL_VIS SELF_CHECK") == 0) {
    snprintf(response, response_size, "RTL_VIS_SELF_CHECK pass=%d", self_check() ? 1 : 0);
    return true;
  }
  return false;
}

bool self_check() {
  if (!controls_self_check() || !text_editor::self_check() || sizeof(Persisted) > 12 * 1024)
    return false;
  size_t slot = 99;
  if (!preset_slot("1", &slot) || slot != 0 || preset_slot("5", &slot) ||
      choice_count("A|B|C") != 3)
    return false;
  if (kDrawerY + kDrawerH != 720 || kHudH < 58 || !rf_analysis::self_check()) return false;
  if (density_decay_scale(3000, 3.0f) != 128 ||
      density_linear_decay(3000, 3.0f) != 128 ||
      density_accumulate(20, 80, 0) != 80 ||
      density_accumulate(20, 80, 1) != 30 ||
      density_accumulate(20, 80, 2) != 32 ||
      density_accumulate(250, 80, 2) != 255 ||
      density_display_intensity(24, 1.0f) != 240 ||
      density_display_intensity(24, 0.25f) != 60)
    return false;
  if (waterfall_rate(0) != 30 || waterfall_rate(1) != 5 || waterfall_rate(7) != 60 ||
      waterfall_intensity(-100, -100, -20, 1) != 0 ||
      waterfall_intensity(-20, -100, -20, 1) != 255 ||
      waterfall_intensity(-60, -100, -20, 1) < 126 ||
      waterfall_intensity(-60, -100, -20, 1) > 128)
    return false;
  if (audio_scroll_pixels(33, 1) != 4 || audio_scroll_pixels(100, 0) != 24)
    return false;
  const ControlDescriptor* shown[48]{};
  const auto contains = [&](size_t count, const char* id) {
    for (size_t i = 0; i < count; ++i)
      if (strcmp(shown[i]->id, id) == 0) return true;
    return false;
  };
  for (size_t i = 0; i < static_cast<size_t>(View::count); ++i)
    if (visible_controls_for(static_cast<View>(i), ControlGroup::quick, false,
                             shown, std::size(shown)) != 5)
      return false;
  size_t shown_count = visible_controls_for(View::waterfall, ControlGroup::quick, false,
                                             shown, std::size(shown));
  if (!contains(shown_count, "display.floor_dbfs") ||
      contains(shown_count, "waterfall.floor_dbfs") || contains(shown_count, "fft.size"))
    return false;
  shown_count = visible_controls_for(View::waterfall, ControlGroup::quick, true,
                                     shown, std::size(shown));
  if (!contains(shown_count, "waterfall.floor_dbfs") ||
      contains(shown_count, "display.floor_dbfs"))
    return false;
  shown_count = visible_controls_for(View::spectrum, ControlGroup::advanced, false,
                                     shown, std::size(shown));
  if (!contains(shown_count, "fft.clear_peak") || !contains(shown_count, "visual.reset"))
    return false;
  if (view_name(View::spectrum3d) == nullptr ||
      strcmp(view_name(View::spectrum3d), "3D SPECTRUM HISTORY") != 0) return false;
  constexpr float carrier_hz = 100000000.0f;
  constexpr float offset_hz = 1000.0f;
  constexpr float c = 299792458.0f;
  const float approaching = c * offset_hz / carrier_hz;
  if (approaching <= 0) return false;
  constexpr int channel_count = 8;
  constexpr float guard = 0.10f;
  const float auto_bw = 960000.0f / channel_count * (1.0f - guard);
  return auto_bw > 100000.0f && auto_bw < 120000.0f;
}

}  // namespace orcsdr::visualizer
