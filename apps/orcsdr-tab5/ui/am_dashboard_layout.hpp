#pragma once

#include "dashboard_header_layout.hpp"

namespace orcsdr::am {
using LayoutRect = audio_header::HeaderRect;

struct DashboardLayout {
  LayoutRect viewport, tabs, frequency, frequency_label, frequency_value, frequency_meta, signal, signal_meter,
      signal_text, tune_down, tune_up, step, direct;
  LayoutRect gain_auto, gain_slider, preset[6], preset_save, preset_previous,
      preset_page, preset_next, preset_modal, modal_cancel, modal_replace, modal_delete;
  LayoutRect finder_panel, finder_title, finder_scan, finder_spacing, finder_hint,
      scan_result[6], finder_discard, finder_save, scan_prompt, scan_prompt_button[3];
  LayoutRect spectrum_panel, spectrum_title, spectrum_readout, spectrum_plot, waterfall, filter_label,
      filter_slider, spectrum_gain_auto, spectrum_gain_slider, span_down, span_label, span_up;
  LayoutRect settings_panel, settings_title, settings_control[8], settings_status;
  LayoutRect settings_gain_auto, settings_gain_slider;
  int tab_height = 0;
  bool portrait = false, legacy = false, valid = false;
};

inline DashboardLayout make_layout(LayoutRect v) {
  DashboardLayout l{};
  l.viewport = v;
  l.portrait = v.h > v.w;
  if (v.x == 0 && v.y == 0 && v.w == 1280 && v.h == 720) {
    l.legacy = true;
    l.tabs = {0, 630, 1280, 90};
    l.frequency = {24, 150, 820, 230};
    l.signal = {860, 150, 396, 230};
    l.frequency_label = {45, 155, 775, 24}; l.frequency_value = {45, 184, 775, 80}; l.frequency_meta = {45, 302, 775, 56};
    l.signal_meter = {884, 220, 330, 30}; l.signal_text = {884, 255, 348, 40};
    l.tune_down = {24, 398, 300, 76}; l.tune_up = {334, 398, 300, 76};
    l.step = {644, 398, 300, 76}; l.direct = {954, 398, 302, 76};
    for (int i = 0; i < 6; ++i) l.preset[i] = {24 + i * 166, 494, 154, 70};
    l.preset_save = {1020, 494, 236, 70};
    l.preset_previous = {710, 570, 70, 48};
    l.preset_page = {785, 570, 140, 48};
    l.preset_next = {930, 570, 70, 48};
    l.gain_auto = {1128, 304, 86, 42}; l.gain_slider = {884, 338, 220, 18};
    l.preset_modal = {290, 205, 700, 300};
    l.modal_cancel = {320, 400, 200, 70}; l.modal_replace = {540, 400, 200, 70};
    l.modal_delete = {760, 400, 200, 70};
    l.finder_panel = {24, 150, 1232, 450}; l.finder_title = {48, 174, 800, 32};
    l.finder_scan = {48, 210, 260, 70}; l.finder_spacing = {328, 210, 260, 70};
    l.finder_hint = {610, 210, 600, 70};
    for (int i = 0; i < 6; ++i)
      l.scan_result[i] = {48 + (i % 3) * 390, 310 + (i / 3) * 90, 370, 70};
    l.finder_discard = {610, 515, 290, 60}; l.finder_save = {920, 515, 290, 60};
    l.scan_prompt = {250, 190, 780, 350};
    l.scan_prompt_button[0] = {280, 425, 220, 70};
    l.scan_prompt_button[1] = {530, 425, 220, 70};
    l.scan_prompt_button[2] = {780, 425, 220, 70};
    l.spectrum_panel = {24, 140, 1232, 470}; l.spectrum_title = {46, 158, 360, 28}; l.spectrum_readout = {420, 158, 800, 28};
    l.spectrum_plot = {47, 247, 1186, 143}; l.waterfall = {46, 420, 1188, 95};
    l.filter_label = {55, 530, 200, 24}; l.filter_slider = {300, 530, 620, 18};
    l.span_down = {390, 565, 70, 42}; l.span_label = {475, 565, 330, 42};
    l.span_up = {820, 565, 70, 42};
    l.settings_panel = {24, 150, 1232, 450}; l.settings_title = {48, 174, 800, 32};
    l.settings_control[0] = {48, 220, 250, 70}; l.settings_control[1] = {320, 220, 180, 70};
    l.settings_control[2] = {520, 220, 180, 70}; l.settings_control[3] = {48, 310, 310, 70};
    l.settings_control[4] = {48, 400, 310, 70}; l.settings_control[5] = {380, 400, 310, 70};
    l.settings_control[6] = {720, 220, 512, 70}; l.settings_control[7] = {720, 310, 512, 70};
    l.settings_status = {48, 476, 1184, 112};
    l.settings_gain_auto = {1128, 486, 86, 42}; l.settings_gain_slider = {884, 520, 220, 18};
    l.valid = true;
    return l;
  }

  if (v.w > 1280) return l;

  const auto header = audio_header::make_header_layout(v);
  if (!header.valid) return l;
  l.legacy = false;
  const int gap = 8;
  l.tab_height = l.portrait ? 52 : 48;
  l.tabs = {v.x, v.y + v.h - l.tab_height, v.w, l.tab_height};
  const int top = header.content_top + gap;
  const int bottom = l.tabs.y - gap;
  const int content_h = bottom - top;
  l.finder_panel = {v.x + gap, top, v.w - 2 * gap, content_h};
  l.finder_title = {l.finder_panel.x + 8, top + 4, l.finder_panel.w - 16, 28};
  const int prompt_w = std::min(780, v.w - 32);
  const int prompt_h = std::min(360, content_h - 16);
  l.scan_prompt = {v.x + (v.w - prompt_w) / 2, top + (content_h - prompt_h) / 2,
                   prompt_w, prompt_h};
  {
    const int modal_gap = 8, button_w = (l.scan_prompt.w - 32 - 2 * modal_gap) / 3;
    const int button_y = l.scan_prompt.y + l.scan_prompt.h - 60;
    for (int i = 0; i < 3; ++i)
      l.scan_prompt_button[i] = {l.scan_prompt.x + 16 + i * (button_w + modal_gap),
                                 button_y, button_w, 44};
  }
  l.settings_panel = l.finder_panel;
  l.settings_title = l.finder_title;
  l.settings_status = {l.settings_panel.x + 8, bottom - 44, l.settings_panel.w - 16, 36};
  l.spectrum_panel = l.finder_panel;
  l.spectrum_title = l.finder_title;
  if (l.portrait) {
    if (content_h < 700 || v.w < 400) return l;
    const int x = v.x + gap, w = v.w - 2 * gap;
    l.frequency = {x, top, w, 136};
    l.signal = {x, top + 144, w, 108};
    l.frequency_label = {x + 12, top + 6, w - 24, 24};
    l.frequency_value = {x + 8, top + 34, w - 16, 50};
    l.frequency_meta = {x + 8, top + 91, w - 16, 32};
    l.signal_meter = {x + 12, top + 176, w - 24, 18};
    l.signal_text = {x + 8, top + 198, w - 16, 42};
    const int row_y = top + 260, half = (w - gap) / 2;
    l.tune_down = {x, row_y, half, 52}; l.tune_up = {x + half + gap, row_y, w - half - gap, 52};
    l.step = {x, row_y + 60, half, 52}; l.direct = {x + half + gap, row_y + 60, w - half - gap, 52};
    l.gain_auto = {x, row_y + 120, 96, 48};
    l.gain_slider = {x + 108, row_y + 120, w - 108, 48};
    const int preset_y = row_y + 176;
    const int cell_w = (w - gap) / 2;
    for (int i = 0; i < 6; ++i)
      l.preset[i] = {x + (i % 2) * (cell_w + gap), preset_y + (i / 2) * 48, cell_w, 44};
    const int footer_y = preset_y + 164;
    const int nav_w = 56, save_w = w - 2 * nav_w - 2 * gap;
    l.preset_previous = {x, footer_y, nav_w, 44};
    l.preset_page = {x + nav_w + gap, footer_y, save_w, 44};
    l.preset_next = {x + nav_w + gap + save_w + gap, footer_y, nav_w, 44};
    l.preset_save = {x, footer_y + 52, w, 44};
    const int fw = (w - gap) / 2, fy = top + 44;
    l.finder_scan = {x, fy, fw, 52}; l.finder_spacing = {x + fw + gap, fy, w - fw - gap, 52};
    l.finder_hint = {x, fy + 58, w, 44};
    for (int i = 0; i < 6; ++i) l.scan_result[i] = {x, fy + 106 + i * 58, w, 52};
    l.finder_discard = {x, bottom - 48, fw, 44};
    l.finder_save = {x + fw + gap, bottom - 48, w - fw - gap, 44};
    const int sx = x, sw = w;
    const int plot_h = std::max(180, (content_h - 260) / 2);
    l.spectrum_title = {sx + 8, top + 4, sw - 16, 24};
    l.spectrum_readout = {sx + 8, top + 28, sw - 16, 32};
    l.spectrum_plot = {sx, top + 64, sw, plot_h};
    l.waterfall = {sx, l.spectrum_plot.y + plot_h + 8, sw, plot_h / 2};
    const int filter_y = l.waterfall.y + l.waterfall.h + 8;
    l.filter_label = {sx, filter_y, 80, 44};
    l.filter_slider = {sx + 84, filter_y + 12, sw - 172, 20};
    const int gain_y = filter_y + 52;
    l.spectrum_gain_auto = {sx, gain_y, 96, 44};
    l.spectrum_gain_slider = {sx + 108, gain_y, sw - 108, 44};
    l.span_down = {sx, gain_y + 52, 64, 44};
    l.span_label = {sx + 72, gain_y + 52, sw - 144, 44};
    l.span_up = {sx + sw - 64, gain_y + 52, 64, 44};
    const int cols = 2, control_gap = 8, control_w = (w - control_gap) / cols;
    const int settings_y = top + 48;
    for (int i = 0; i < 8; ++i)
      l.settings_control[i] = {x + (i % cols) * (control_w + control_gap),
          settings_y + (i / cols) * 56, control_w, 48};
    const int settings_gain_y = settings_y + 4 * 56 + 8;
    l.settings_gain_auto = {x, settings_gain_y, 96, 44};
    l.settings_gain_slider = {x + 108, settings_gain_y, w - 108, 44};
    l.settings_status = {x, settings_gain_y + 52, w, 36};
    const int modal_w = std::min(700, w - 16), modal_h = std::min(360, v.h - 24);
    l.preset_modal = {v.x + (v.w - modal_w) / 2, v.y + (v.h - modal_h) / 2,
                      modal_w, modal_h};
    const int modal_gap = 8, modal_button_w = (modal_w - 32 - 2 * modal_gap) / 3;
    const int modal_button_y = l.preset_modal.y + modal_h - 60;
    l.modal_cancel = {l.preset_modal.x + 16, modal_button_y, modal_button_w, 44};
    l.modal_replace = {l.modal_cancel.x + modal_button_w + modal_gap, modal_button_y,
                       modal_button_w, 44};
    l.modal_delete = {l.modal_replace.x + modal_button_w + modal_gap, modal_button_y,
                      modal_button_w, 44};
  } else {
    if (content_h < 248 || v.w < 800) return l;
    const int x = v.x + gap, w = v.w - 2 * gap;
    const int half = (w - gap) / 2;
    const int row_h = 48;
    l.frequency = {x, top, w, 70};
    l.signal = l.frequency;
    l.frequency_value = {x + 8, top + 2, w * 2 / 3 - 16, 36};
    l.frequency_meta = {x + w * 2 / 3, top + 2, w / 3 - 16, 40};
    l.signal_meter = {x + 8, top + 48, w / 2 - 12, 16};
    l.signal_text = {x + w / 2, top + 48, w / 2 - 8, 16};
    const int row_y = top + 78;
    l.tune_down = {x, row_y, (w - 3 * gap) / 4, row_h};
    l.tune_up = {l.tune_down.x + l.tune_down.w + gap, row_y, l.tune_down.w, row_h};
    l.step = {l.tune_up.x + l.tune_up.w + gap, row_y, l.tune_down.w, row_h};
    l.direct = {l.step.x + l.step.w + gap, row_y, w - (l.step.x + l.step.w + gap - x), row_h};
    const int preset_y = row_y + 56;
    const int cell_w = (w - 5 * gap) / 6;
    for (int i = 0; i < 6; ++i) l.preset[i] = {x + i * (cell_w + gap), preset_y, cell_w, 44};
    l.preset_previous = {x, preset_y + 52, 56, 44};
    l.preset_page = {x + 64, preset_y + 52, 180, 44};
    l.preset_next = {x + 252, preset_y + 52, 56, 44};
    l.preset_save = {x + 316, preset_y + 52, w - 316, 44};
    const int modal_w = std::min(700, w - 16), modal_h = 300;
    l.preset_modal = {v.x + (v.w - modal_w) / 2, v.y + (v.h - modal_h) / 2,
                      modal_w, modal_h};
    const int modal_gap = 8, modal_button_w = (modal_w - 32 - 2 * modal_gap) / 3;
    const int modal_button_y = l.preset_modal.y + modal_h - 60;
    l.modal_cancel = {l.preset_modal.x + 16, modal_button_y, modal_button_w, 44};
    l.modal_replace = {l.modal_cancel.x + modal_button_w + modal_gap, modal_button_y,
                       modal_button_w, 44};
    l.modal_delete = {l.modal_replace.x + modal_button_w + modal_gap, modal_button_y,
                      modal_button_w, 44};
    const int fw = (w - 16) / 3, fy = top + 36;
    l.finder_scan = {x, fy, 190, 44}; l.finder_spacing = {x + 198, fy, 190, 44};
    l.finder_hint = {x + 396, fy, w - 396, 44};
    for (int i = 0; i < 6; ++i)
      l.scan_result[i] = {x + (i % 3) * (fw + 8), fy + 52 + (i / 3) * 56, fw, 48};
    l.finder_discard = {x, bottom - 44, (w - gap) / 2, 44};
    l.finder_save = {x + (w + gap) / 2, bottom - 44, (w - gap) / 2, 44};
    const int plot_y = top + 32;
    l.spectrum_title = {x, top + 2, w / 3, 22};
    l.spectrum_readout = {x + w / 3, top + 2, w * 2 / 3, 22};
    l.spectrum_plot = {x, plot_y + 8, w, 64};
    l.waterfall = {x, plot_y + 72, w, 44};
    const int filter_y = plot_y + 120;
    l.filter_label = {x, filter_y, 72, 36};
    l.filter_slider = {x + 76, filter_y + 8, w - 164, 18};
    const int gain_y = filter_y + 40;
    l.spectrum_gain_auto = {x, gain_y, 112, 44};
    l.spectrum_gain_slider = {x + 124, gain_y, w - 124, 44};
    l.span_down = {x, gain_y + 44, 56, 44};
    l.span_label = {x + 64, gain_y + 44, w - 128, 44};
    l.span_up = {x + w - 56, gain_y + 44, 56, 44};
    const int cols = 4, settings_gap = 8, control_w = (w - 3 * settings_gap) / cols;
    const int settings_y = top + 40;
    for (int i = 0; i < 8; ++i)
      l.settings_control[i] = {x + (i % cols) * (control_w + settings_gap),
          settings_y + (i / cols) * 52, control_w, 44};
    const int settings_gain_y = settings_y + 104;
    l.settings_gain_auto = {x, settings_gain_y, 112, 44};
    l.settings_gain_slider = {x + 124, settings_gain_y, w - 124, 44};
    l.settings_status = {x, settings_gain_y + 48, w, 36};
    l.gain_auto = {x, preset_y + 104, 112, 44};
    l.gain_slider = {x + 124, preset_y + 104, w - 124, 44};
    (void)half;
  }

  const LayoutRect required[] = {l.tabs, l.frequency, l.frequency_value,
      l.frequency_meta, l.signal, l.signal_meter, l.signal_text, l.tune_down, l.tune_up,
      l.step, l.direct, l.gain_auto, l.gain_slider, l.preset_save, l.preset_previous,
      l.preset_page, l.preset_next, l.preset_modal, l.modal_cancel, l.modal_replace,
      l.modal_delete};
  l.valid = true;
  for (const auto r : required) if (!audio_header::header_inside(v, r)) l.valid = false;
  for (const auto r : l.preset) if (!audio_header::header_inside(v, r)) l.valid = false;
  const LayoutRect responsive[] = {l.finder_panel,l.finder_title,l.finder_scan,l.finder_spacing,
      l.finder_hint,l.finder_discard,l.finder_save,l.scan_prompt,l.spectrum_panel,
      l.spectrum_title,l.spectrum_readout,l.spectrum_plot,l.waterfall,l.filter_label,l.filter_slider,
      l.spectrum_gain_auto,l.spectrum_gain_slider,
      l.span_down,l.span_label,l.span_up,l.settings_panel,l.settings_title,l.settings_status,
      l.settings_gain_auto,l.settings_gain_slider};
  for (const auto r : responsive) if (!audio_header::header_inside(v,r)) l.valid = false;
  for (const auto r : l.scan_result) if (!audio_header::header_inside(v,r)) l.valid = false;
  for (const auto r : l.scan_prompt_button) if (!audio_header::header_inside(v,r)) l.valid = false;
  for (const auto r : l.settings_control) if (!audio_header::header_inside(v,r)) l.valid = false;
  const LayoutRect buttons[] = {l.tune_down,l.tune_up,l.step,l.direct,l.gain_auto,
      l.preset_save,l.preset_previous,l.preset_next,l.modal_cancel,l.modal_replace,
      l.modal_delete,l.finder_scan,l.finder_spacing,l.finder_discard,l.finder_save,
      l.span_down,l.span_up,l.settings_gain_auto};
  for (const auto r : buttons) if (r.w < 44 || r.h < 44) l.valid = false;
  for (const auto r : l.preset) if (r.w < 44 || r.h < 44) l.valid = false;
  for (const auto r : l.scan_result) if (r.w < 44 || r.h < 44) l.valid = false;
  for (const auto r : l.scan_prompt_button) if (r.w < 44 || r.h < 44) l.valid = false;
  for (const auto r : l.settings_control) if (r.w < 44 || r.h < 44) l.valid = false;
  return l;
}

inline LayoutRect tab_rect(const DashboardLayout& l, int index) {
  constexpr int count = 4;
  if (index < 0 || index >= count) return {};
  const int width = l.tabs.w / count;
  const int x = l.tabs.x + index * width;
  return {x, l.tabs.y,
          index + 1 == count ? l.tabs.x + l.tabs.w - x : width,
          l.tabs.h};
}
}  // namespace orcsdr::am
