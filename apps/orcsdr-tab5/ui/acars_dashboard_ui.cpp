#include "acars_dashboard_ui.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::acars_ui {
namespace {

using layout::Rect;

bool in(const Rect& r, int32_t x, int32_t y) { return r.contains(static_cast<int>(x), static_cast<int>(y)); }

template <typename T>
T cycle(T value, int count) {
  return static_cast<T>((static_cast<int>(value) + 1) % count);
}

}  // namespace

void Controller::reset() {
  view_ = View::live;
  detail_open_ = false;
  detail_record_ = MessageRecord{};
  detail_text_[0] = '\0';
  detail_text_len_ = 0;
  detail_line_ = 0;
  selected_tail_[0] = '\0';
  tail_selected_ = false;
  query_ = ArchiveQuery{};
  archive_offset_ = 0;
  bump();
}

void Controller::select_view(View view) {
  if (view >= View::count) return;
  view_ = view;
  detail_open_ = false;
  bump();
}

void Controller::close_detail() {
  if (!detail_open_) return;
  detail_open_ = false;
  bump();
}

void Controller::refresh_detail_text(const Model& model) {
  const bool plain = model.snapshot_prefs_format() == TextFormat::plain;
  bool cut = false;
  detail_text_len_ = plain ? sanitize_preview(detail_text_, sizeof(detail_text_), detail_record_.text,
                                              detail_record_.text_length, &cut)
                           : escape_text(detail_text_, sizeof(detail_text_), detail_record_.text,
                                         detail_record_.text_length, &cut);
}

bool Controller::open_detail(uint32_t seq, const Model& model) {
  const MessageRecord* m = model.find_message(seq);
  if (!m) return false;
  detail_record_ = *m;
  detail_open_ = true;
  detail_line_ = 0;
  refresh_detail_text(model);
  bump();
  return true;
}

size_t Controller::detail_line_count() const {
  if (detail_text_len_ == 0) return 0;
  return (detail_text_len_ + layout::kDetailCols - 1) / layout::kDetailCols;
}

bool Controller::detail_line_at(size_t index, const char** start, size_t* length) const {
  if (index >= detail_line_count()) return false;
  const size_t begin = index * layout::kDetailCols;
  *start = detail_text_ + begin;
  *length = std::min<size_t>(layout::kDetailCols, detail_text_len_ - begin);
  return true;
}

Action Controller::touch(int32_t x, int32_t y, Model& model) {
  if (y < layout::kHeaderH) return {};            // shared header: Home / Mute / Visualizer / Settings
  if (y >= layout::kTabsY) {
    for (int i = 0; i < layout::kTabCount; ++i)
      if (in(layout::tab(i), x, y)) {
        select_view(static_cast<View>(i));
        return {ActionKind::changed, 0};
      }
    return {};
  }
  if (detail_open_) return touch_detail(x, y, model);
  switch (view_) {
    case View::live: return touch_live(x, y, model);
    case View::aircraft: return touch_aircraft(x, y, model);
    case View::channels: return touch_channels(x, y, model);
    case View::archive: return touch_archive(x, y, model);
    case View::setup: return touch_setup(x, y, model);
    default: return {};
  }
}

Action Controller::touch_live(int32_t x, int32_t y, Model& model) {
  const Snapshot& s = model.snapshot();
  for (size_t i = 0; i < s.message_count; ++i)
    if (in(layout::live_row(static_cast<int>(i)), x, y)) {
      open_detail(s.messages[i].seq, model);
      return {ActionKind::changed, 0};
    }
  if (in(layout::kLiveNavUp, x, y)) return {model.scroll_messages(-6) ? ActionKind::changed : ActionKind::none, 0};
  if (in(layout::kLiveNavDown, x, y)) return {model.scroll_messages(6) ? ActionKind::changed : ActionKind::none, 0};
  return {};
}

Action Controller::touch_detail(int32_t x, int32_t y, Model& model) {
  if (in(layout::kDetailBack, x, y)) {
    close_detail();
    return {ActionKind::changed, 0};
  }
  if (in(layout::kDetailNewer, x, y) || in(layout::kDetailOlder, x, y)) {
    const uint32_t next = model.neighbor_seq(detail_record_.seq, in(layout::kDetailNewer, x, y));
    if (next == 0 || !open_detail(next, model)) return {};
    return {ActionKind::changed, 0};
  }
  const size_t lines = detail_line_count();
  const size_t max_line = lines > static_cast<size_t>(layout::kDetailLines) ? lines - layout::kDetailLines : 0;
  if (in(layout::kDetailRawUp, x, y) && detail_line_ > 0) {
    detail_line_ = detail_line_ >= 4 ? detail_line_ - 4 : 0;
    bump();
    return {ActionKind::changed, 0};
  }
  if (in(layout::kDetailRawDown, x, y) && detail_line_ < max_line) {
    detail_line_ = std::min(detail_line_ + 4, max_line);
    bump();
    return {ActionKind::changed, 0};
  }
  return {};
}

Action Controller::touch_aircraft(int32_t x, int32_t y, Model& model) {
  const Snapshot& s = model.snapshot();
  for (size_t i = 0; i < s.aircraft_count; ++i)
    if (in(layout::ac_row(static_cast<int>(i)), x, y)) {
      std::snprintf(selected_tail_, sizeof(selected_tail_), "%s", s.aircraft[i].tail);
      tail_selected_ = true;
      bump();
      return {ActionKind::changed, 0};
    }
  if (in(layout::kAcSort, x, y)) {
    model.set_sort(cycle(s.prefs.sort, static_cast<int>(AircraftSort::count)));
    bump();
    return {ActionKind::changed, 0};
  }
  if (in(layout::kAcUp, x, y)) return {model.scroll_aircraft(-4) ? ActionKind::changed : ActionKind::none, 0};
  if (in(layout::kAcDown, x, y)) return {model.scroll_aircraft(4) ? ActionKind::changed : ActionKind::none, 0};
  if (tail_selected_) {
    const MessageRecord* list[kAircraftRecentShown];
    const size_t n = model.aircraft_messages(selected_tail_, list, kAircraftRecentShown);
    for (size_t j = 0; j < n; ++j)
      if (in(layout::ac_msg_row(static_cast<int>(j)), x, y)) {
        open_detail(list[j]->seq, model);
        return {ActionKind::changed, 0};
      }
  }
  return {};
}

Action Controller::touch_channels(int32_t x, int32_t y, Model& model) {
  const Snapshot& s = model.snapshot();
  if (in(layout::kChRegion, x, y)) {
    model.set_region(cycle(s.prefs.region, static_cast<int>(Region::count)));
    bump();
    return {ActionKind::tune_requested, model.snapshot().requested_hz};
  }
  for (size_t i = 0; i < s.channel_count; ++i)
    if (in(layout::ch_row(static_cast<int>(i)), x, y)) {
      model.set_preset(i);
      bump();
      return {ActionKind::tune_requested, model.snapshot().requested_hz};
    }
  if (in(layout::kChMinus, x, y) || in(layout::kChPlus, x, y)) {
    const bool up = in(layout::kChPlus, x, y);
    const uint32_t base = s.requested_hz;
    const uint32_t next = up ? base + kCustomStepHz : base - kCustomStepHz;
    if (!model.set_custom_frequency(next)) return {};
    bump();
    return {ActionKind::tune_requested, next};
  }
  if (in(layout::kChClear, x, y) && s.custom_active) {
    model.set_custom_frequency(0);
    bump();
    return {ActionKind::tune_requested, model.snapshot().requested_hz};
  }
  return {};
}

Action Controller::touch_archive(int32_t x, int32_t y, Model& model) {
  if (!model.archive_browsable()) return {};      // no archive source: every control is disabled
  if (in(layout::ar_filter(0), x, y)) {
    model.archive_cycle_tail(query_.tail);
    archive_offset_ = 0;
    bump();
    return {ActionKind::changed, 0};
  }
  if (in(layout::ar_filter(1), x, y)) {
    model.archive_cycle_label(query_.label);
    archive_offset_ = 0;
    bump();
    return {ActionKind::changed, 0};
  }
  if (in(layout::kArClear, x, y) && (query_.tail[0] || query_.label[0])) {
    query_ = ArchiveQuery{};
    archive_offset_ = 0;
    bump();
    return {ActionKind::changed, 0};
  }
  size_t total = 0;
  const MessageRecord* rows[layout::kArRows];
  const size_t n = model.archive_query(query_, archive_offset_, rows, layout::kArRows, &total);
  const size_t limit = total > static_cast<size_t>(layout::kArRows) ? total - layout::kArRows : 0;
  if (in(layout::kArUp, x, y) && archive_offset_ > 0) {
    archive_offset_ = archive_offset_ >= 4 ? archive_offset_ - 4 : 0;
    bump();
    return {ActionKind::changed, 0};
  }
  if (in(layout::kArDown, x, y) && archive_offset_ < limit) {
    archive_offset_ = std::min(archive_offset_ + 4, limit);
    bump();
    return {ActionKind::changed, 0};
  }
  for (size_t i = 0; i < n; ++i)
    if (in(layout::ar_row(static_cast<int>(i)), x, y)) {
      open_detail(rows[i]->seq, model);
      return {ActionKind::changed, 0};
    }
  return {};
}

Action Controller::touch_setup(int32_t x, int32_t y, Model& model) {
  const Snapshot& s = model.snapshot();
  const Preferences& p = s.prefs;
  int item = -1;
  for (int c = 0; c < 2 && item < 0; ++c)
    for (int r = 0; r < 5; ++r)
      if (in(layout::setup_item(c, r), x, y)) { item = c * 5 + r; break; }
  switch (item) {
    case 0: model.set_region(cycle(p.region, static_cast<int>(Region::count))); break;
    case 1: model.set_text_format(cycle(p.format, static_cast<int>(TextFormat::count))); break;
    case 2: model.set_show_raw_text(!p.show_raw_text); break;
    case 3: model.set_highlight_corrected(!p.highlight_corrected); break;
    case 4: model.set_filter(cycle(p.filter, static_cast<int>(MessageFilter::count))); break;
    case 5: model.set_sort(cycle(p.sort, static_cast<int>(AircraftSort::count))); break;
    case 6: {
      const bool on = !model.demo();
      model.set_demo(on, DemoScenario::mixed);
      query_ = ArchiveQuery{};
      archive_offset_ = 0;
      tail_selected_ = false;
      detail_open_ = false;
      bump();
      return {ActionKind::demo_changed, on ? 1u : 0u};
    }
    case 7:
      if (!model.demo()) return {};               // scenario choice exists only inside DEMO mode
      model.set_demo(true, cycle(model.scenario(), static_cast<int>(DemoScenario::count)));
      tail_selected_ = false;
      query_ = ArchiveQuery{};
      archive_offset_ = 0;
      bump();
      return {ActionKind::changed, 0};
    default: return {};                           // 8, 9: planned features, intentionally inert
  }
  if (detail_open_) refresh_detail_text(model);
  bump();
  return {ActionKind::changed, 0};
}

bool ui_self_check() {
  Controller c;
  c.reset();
  return layout::tab(0).x == 0 && layout::tab(5).x + layout::tab(5).w == 1280 && c.view() == View::live;
}

}  // namespace orcsdr::acars_ui
