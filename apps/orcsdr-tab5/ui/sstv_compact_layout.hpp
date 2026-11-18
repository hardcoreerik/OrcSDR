#pragma once
// Geometry of the compact (responsive) SSTV dashboard for views smaller than 1280 x 720 (LilyGO T-Display P4 portrait and landscape).
// Pure arithmetic, host-tested. Pages: RECEIVE (picture and status), IMAGES (gallery), TUNE (profiles, VFO, gain), SETUP.
#include <algorithm>
#include "dashboard_header_layout.hpp"

namespace orcsdr::sstv::compact {
using Rect = audio_header::HeaderRect;

constexpr int kTabCount = 4;
constexpr int kProfileCount = 5;     // sstv::kProfiles
constexpr int kGalleryCells = 6;

struct Layout {
  Rect viewport, content, tabs, tab[kTabCount];
  // RECEIVE
  Rect info, image, abort_button, save_button;
  // IMAGES
  Rect cell[kGalleryCells], gallery_prev, gallery_label, gallery_next, gallery_save, gallery_clear;
  // TUNE
  Rect profile[kProfileCount], frequency, nudge_down, nudge_up, step_button, demod_button, gain_auto, gain_down, gain_up, if_button, level;
  // SETUP
  Rect detection_button, mode_button, autosave_button, clear_button, setup_note;
  bool portrait = false, valid = false;
};

inline bool inside(Rect outer, Rect r) {
  return r.w > 0 && r.h > 0 && r.x >= outer.x && r.y >= outer.y && r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h;
}

inline Layout make_layout(Rect v) {
  Layout l{};
  l.viewport = v;
  l.portrait = v.h > v.w;
  if (v.w < 360 || v.h < 300 || v.w > 1280) return l;
  const auto header = audio_header::make_header_layout(v);
  if (!header.valid || header.legacy) return l;
  constexpr int gap = 8, tab_h = 48, btn_h = 48;
  l.tabs = {v.x, v.y + v.h - tab_h, v.w, tab_h};
  const int tab_w = (v.w - (kTabCount - 1) * 4) / kTabCount;
  for (int i = 0; i < kTabCount; ++i)
    l.tab[i] = {v.x + i * (tab_w + 4), l.tabs.y, i + 1 == kTabCount ? v.x + v.w - (v.x + i * (tab_w + 4)) : tab_w, tab_h};
  const int top = header.content_top;
  l.content = {v.x, top, v.w, l.tabs.y - 4 - top};
  const int bottom = l.content.y + l.content.h;

  // ---- RECEIVE ----
  const int half = (v.w - gap) / 2;
  if (!l.portrait) {
    const int info_w = v.w * 36 / 100;
    l.info = {v.x, l.content.y, info_w, l.content.h - btn_h - gap};
    l.image = {v.x + info_w + gap, l.content.y, v.w - info_w - gap, l.content.h};
    l.abort_button = {v.x, bottom - btn_h, (info_w - gap) / 2, btn_h};
    l.save_button = {v.x + (info_w - gap) / 2 + gap, bottom - btn_h, info_w - (info_w - gap) / 2 - gap, btn_h};
  } else {
    l.info = {v.x, l.content.y, v.w, 120};
    l.abort_button = {v.x, bottom - btn_h, half, btn_h};
    l.save_button = {v.x + half + gap, bottom - btn_h, v.w - half - gap, btn_h};
    l.image = {v.x, l.info.y + l.info.h + gap, v.w, bottom - btn_h - gap - (l.info.y + l.info.h + gap)};
  }

  // ---- IMAGES ----
  const int paging_y = bottom - btn_h;
  const int cols = l.portrait ? 2 : 3, rows = l.portrait ? 3 : 2;
  const int grid_h = paging_y - gap - l.content.y;
  const int cw = (v.w - (cols - 1) * gap) / cols, ch = (grid_h - (rows - 1) * gap) / rows;
  for (int i = 0; i < kGalleryCells; ++i) l.cell[i] = {v.x + (i % cols) * (cw + gap), l.content.y + (i / cols) * (ch + gap), cw, ch};
  const int bw = (v.w - 4 * gap) / 5;
  l.gallery_prev = {v.x, paging_y, bw, btn_h};
  l.gallery_label = {v.x + bw + gap, paging_y, bw, btn_h};
  l.gallery_next = {v.x + 2 * (bw + gap), paging_y, bw, btn_h};
  l.gallery_save = {v.x + 3 * (bw + gap), paging_y, bw, btn_h};
  l.gallery_clear = {v.x + 4 * (bw + gap), paging_y, v.w - 4 * (bw + gap), btn_h};

  // ---- TUNE ----
  const int row = 44;
  const int third = (v.w - 2 * gap) / 3;
  if (!l.portrait) {
    // Two columns: the profile list (and the level read-out) on the left, the VFO and receiver controls on the right.
    const int lw = (v.w - gap) / 2, rx = v.x + lw + gap, rw = v.w - lw - gap;
    for (int i = 0; i < kProfileCount; ++i) l.profile[i] = {v.x, l.content.y + i * (row + 4), lw, row};
    const int ly = l.content.y + kProfileCount * (row + 4) + 2;
    l.level = {v.x, ly, lw, std::max(30, bottom - ly)};
    int y = l.content.y;
    l.frequency = {rx, y, rw, 52};
    y += 52 + 6;
    const int rhalf = (rw - gap) / 2;
    l.nudge_down = {rx, y, rhalf, btn_h};
    l.nudge_up = {rx + rhalf + gap, y, rw - rhalf - gap, btn_h};
    y += btn_h + 6;
    l.step_button = {rx, y, rhalf, btn_h};
    l.demod_button = {rx + rhalf + gap, y, rw - rhalf - gap, btn_h};
    y += btn_h + 6;
    const int rthird = (rw - 2 * gap) / 3;
    l.gain_auto = {rx, y, rthird, btn_h};
    l.gain_down = {rx + rthird + gap, y, rthird, btn_h};
    l.gain_up = {rx + 2 * (rthird + gap), y, rw - 2 * (rthird + gap), btn_h};
    y += btn_h + 6;
    l.if_button = {rx, y, rw, btn_h};
  } else {
    int y = l.content.y;
    for (int i = 0; i < kProfileCount; ++i) l.profile[i] = {v.x, y + i * (row + 4), v.w, row};
    y += kProfileCount * (row + 4) + gap;
    l.frequency = {v.x, y, v.w, 56};
    y += 56 + gap;
    l.nudge_down = {v.x, y, half, btn_h};
    l.nudge_up = {v.x + half + gap, y, v.w - half - gap, btn_h};
    y += btn_h + gap;
    l.step_button = {v.x, y, half, btn_h};
    l.demod_button = {v.x + half + gap, y, v.w - half - gap, btn_h};
    y += btn_h + gap;
    l.gain_auto = {v.x, y, third, btn_h};
    l.gain_down = {v.x + third + gap, y, third, btn_h};
    l.gain_up = {v.x + 2 * (third + gap), y, v.w - 2 * (third + gap), btn_h};
    y += btn_h + gap;
    l.if_button = {v.x, y, v.w, btn_h};
    y += btn_h + gap;
    l.level = {v.x, y, v.w, std::max(30, bottom - y)};
  }

  // ---- SETUP ----
  l.detection_button = {v.x, l.content.y, v.w, 56};
  l.mode_button = {v.x, l.content.y + 64, v.w, 56};
  l.autosave_button = {v.x, l.content.y + 128, v.w, 56};
  l.clear_button = {v.x, l.content.y + 192, v.w, 56};
  l.setup_note = {v.x, l.content.y + 256, v.w, std::max(30, bottom - (l.content.y + 256))};

  l.valid = inside(v, l.content) && inside(v, l.tabs) && l.image.w >= 120 && l.image.h >= 90 && l.info.h >= 60 && inside(l.content, l.level) &&
            inside(l.content, l.setup_note) && inside(l.content, l.cell[kGalleryCells - 1]);
  return l;
}
}  // namespace orcsdr::sstv::compact
