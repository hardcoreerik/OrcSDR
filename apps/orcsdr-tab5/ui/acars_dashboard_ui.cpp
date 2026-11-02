#include "acars_dashboard_ui.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>

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
  aircraft_message_offset_ = 0;
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

void Controller::reflow_detail() {
  if (!detail_open_) return;
  const size_t lines = detail_line_count();
  const size_t visible = static_cast<size_t>(std::max(1, layout::kDetailLines));
  const size_t max_line = lines > visible ? lines - visible : 0;
  if (detail_line_ > max_line) {
    detail_line_ = max_line;
    bump();
  }
}

void Controller::reflow_aircraft(const Model& model) {
  if (!tail_selected_) return;
  const MessageRecord* rows[kAircraftRecentShown];
  size_t total = 0;
  (void)model.aircraft_messages(selected_tail_, rows, kAircraftRecentShown, 0, &total);
  const size_t visible = layout::kAcMessageRows;
  const size_t limit = total > visible ? total - visible : 0;
  if (aircraft_message_offset_ > limit) {
    aircraft_message_offset_ = limit;
    bump();
  }
}

void Controller::reflow_archive(const Model& model) {
  size_t total = 0;
  const MessageRecord* ignored = nullptr;
  (void)model.archive_query(query_, archive_offset_, &ignored, 0, &total);
  const size_t visible = static_cast<size_t>(layout::kArVisibleRows);
  const size_t limit = total > visible ? total - visible : 0;
  if (archive_offset_ > limit) archive_offset_ = limit;
}

bool Controller::detail_line_at(size_t index, const char** start, size_t* length) const {
  if (index >= detail_line_count()) return false;
  const size_t begin = index * layout::kDetailCols;
  *start = detail_text_ + begin;
  *length = std::min<size_t>(layout::kDetailCols, detail_text_len_ - begin);
  return true;
}

Action Controller::touch(int32_t x, int32_t y, Model& model) {
  (void)model.set_visible_message_rows(layout::kLiveVisibleRows);
  (void)model.set_visible_aircraft_rows(layout::kAcVisibleRows);
  if (!layout::kViewport.contains(x, y)) return {};
  if (y < layout::kViewport.y + layout::kHeaderH) return {}; // shared header: Home / Mute / Visualizer / Settings
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
  const size_t rows = std::min(s.message_count, layout::kLiveVisibleRows);
  for (size_t i = 0; i < rows; ++i)
    if (in(layout::live_row(static_cast<int>(i)), x, y)) {
      open_detail(s.messages[i].seq, model);
      return {ActionKind::changed, 0};
    }
  const int page = static_cast<int>(layout::kLiveVisibleRows);
  if (in(layout::kLiveNavUp, x, y)) return {model.scroll_messages(-page) ? ActionKind::changed : ActionKind::none, 0};
  if (in(layout::kLiveNavDown, x, y)) return {model.scroll_messages(page) ? ActionKind::changed : ActionKind::none, 0};
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
  const size_t visible = static_cast<size_t>(std::max(1, layout::kDetailLines));
  const size_t max_line = lines > visible ? lines - visible : 0;
  const size_t page = layout::kViewport == layout::Rect{0, 0, 1280, 720} ? 4 : visible;
  if (in(layout::kDetailRawUp, x, y) && detail_line_ > 0) {
    detail_line_ = detail_line_ >= page ? detail_line_ - page : 0;
    bump();
    return {ActionKind::changed, 0};
  }
  if (in(layout::kDetailRawDown, x, y) && detail_line_ < max_line) {
    detail_line_ = std::min(detail_line_ + page, max_line);
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
      aircraft_message_offset_ = 0;
      bump();
      return {ActionKind::changed, 0};
    }
  if (in(layout::kAcSort, x, y)) {
    model.set_sort(cycle(s.prefs.sort, static_cast<int>(AircraftSort::count)));
    bump();
    return {ActionKind::changed, 0};
  }
  const int page = static_cast<int>(layout::kAcVisibleRows);
  if (in(layout::kAcUp, x, y)) return {model.scroll_aircraft(-page) ? ActionKind::changed : ActionKind::none, 0};
  if (in(layout::kAcDown, x, y)) return {model.scroll_aircraft(page) ? ActionKind::changed : ActionKind::none, 0};
  if (tail_selected_) {
    const MessageRecord* list[kAircraftRecentShown];
    size_t total = 0;
    const size_t n = model.aircraft_messages(selected_tail_, list, layout::kAcMessageRows,
                                             aircraft_message_offset_, &total);
    for (size_t j = 0; j < n; ++j)
      if (in(layout::ac_msg_row(static_cast<int>(j)), x, y)) {
        open_detail(list[j]->seq, model);
        return {ActionKind::changed, 0};
      }
    const size_t limit = total > layout::kAcMessageRows ? total - layout::kAcMessageRows : 0;
    const size_t message_page = layout::kAcMessageRows;
    if (in(layout::kAcMsgUp, x, y) && aircraft_message_offset_ > 0) {
      aircraft_message_offset_ = aircraft_message_offset_ >= message_page ? aircraft_message_offset_ - message_page : 0;
      bump();
      return {ActionKind::changed, 0};
    }
    if (in(layout::kAcMsgDown, x, y) && aircraft_message_offset_ < limit) {
      aircraft_message_offset_ = std::min(aircraft_message_offset_ + message_page, limit);
      bump();
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
  for (size_t i = 0; i < std::min<size_t>(s.channel_count, layout::kChVisibleRows); ++i)
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
  const size_t page_rows = static_cast<size_t>(layout::kArVisibleRows);
  const size_t n = model.archive_query(query_, archive_offset_, rows, page_rows, &total);
  const size_t limit = total > page_rows ? total - page_rows : 0;
  if (in(layout::kArUp, x, y) && archive_offset_ > 0) {
    archive_offset_ = archive_offset_ >= page_rows ? archive_offset_ - page_rows : 0;
    bump();
    return {ActionKind::changed, 0};
  }
  if (in(layout::kArDown, x, y) && archive_offset_ < limit) {
    archive_offset_ = std::min(archive_offset_ + page_rows, limit);
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
  const layout::Rect saved = layout::kViewport;
  Model model;
  const size_t saved_rows = model.visible_message_rows();
  model.set_demo(true, DemoScenario::full);
  model.service();
  bool ok = true;
  Controller detail;
  for (size_t rows : {size_t{3}, size_t{6}, size_t{12}}) {
    (void)model.set_visible_message_rows(rows);
    while (model.scroll_messages(static_cast<int>(rows))) {}
    const Snapshot& end = model.snapshot();
    ok = end.message_offset + end.message_count == end.matching_count && end.message_count <= rows && ok;
    if (end.message_count) {
      const uint32_t selected_seq = end.messages[0].seq;
      ok = detail.open_detail(selected_seq, model) && detail.detail_record().seq == selected_seq && ok;
      (void)model.set_visible_message_rows(rows == 3 ? 6 : 3);
      ok = detail.detail_open() && detail.detail_record().seq == selected_seq && ok;
      detail.close_detail();
    }
  }
  for (size_t rows : {size_t{2}, size_t{6}, size_t{8}}) {
    (void)model.set_visible_aircraft_rows(rows);
    while (model.scroll_aircraft(static_cast<int>(rows))) {}
    const Snapshot& end = model.snapshot();
    ok = end.aircraft_offset + end.aircraft_count == end.aircraft_total && end.aircraft_count <= rows && ok;
  }
  (void)model.set_visible_aircraft_rows(kAircraftRows);
  while (model.scroll_aircraft(-static_cast<int>(kAircraftTable))) {}
  Controller aircraft;
  aircraft.select_view(View::aircraft);
  char selected_tail[8]{};
  for (const layout::Rect viewport : {layout::Rect{72, 72, 424, 1088}, layout::Rect{72, 72, 1088, 424}}) {
    ok = layout::set_viewport(viewport) && ok;
    (void)model.set_visible_aircraft_rows(layout::kAcVisibleRows);
    const Action picked = aircraft.touch(layout::ac_row(0).x + layout::ac_row(0).w / 2,
                                         layout::ac_row(0).y + layout::ac_row(0).h / 2, model);
    if (!selected_tail[0]) std::snprintf(selected_tail, sizeof(selected_tail), "%s", aircraft.selected_tail());
    ok = picked.kind == ActionKind::changed && std::strcmp(selected_tail, aircraft.selected_tail()) == 0 && ok;
    aircraft.reflow_aircraft(model);
    const MessageRecord* recent[kAircraftRecentShown]{};
    size_t total_recent = 0;
    (void)model.aircraft_messages(aircraft.selected_tail(), recent, kAircraftRecentShown, 0, &total_recent);
    if (total_recent > layout::kAcMessageRows && aircraft.aircraft_message_offset() == 0) {
      const Action next_message = aircraft.touch(layout::kAcMsgDown.x + layout::kAcMsgDown.w / 2,
          layout::kAcMsgDown.y + layout::kAcMsgDown.h / 2, model);
      const size_t expected_offset = std::min(layout::kAcMessageRows, total_recent - layout::kAcMessageRows);
      ok = next_message.kind == ActionKind::changed &&
           aircraft.aircraft_message_offset() == expected_offset && ok;
    }
  }
  (void)model.set_visible_message_rows(kMessageRows);
  while (model.scroll_messages(-static_cast<int>(kHistoryCapacity))) {}
  Controller selected;
  const Snapshot& visible = model.snapshot();
  const uint32_t selected_seq = visible.message_count > 1 ? visible.messages[1].seq : visible.messages[0].seq;
  ok = selected.open_detail(selected_seq, model) && ok;
  for (const layout::Rect viewport : {layout::Rect{72, 72, 424, 1088}, layout::Rect{72, 72, 1088, 424}}) {
    ok = layout::set_viewport(viewport) && ok;
    selected.reflow_detail();
    ok = selected.detail_open() && selected.detail_record().seq == selected_seq && ok;
  }
  const Action newer = selected.touch(layout::kDetailNewer.x + layout::kDetailNewer.w / 2,
                                      layout::kDetailNewer.y + layout::kDetailNewer.h / 2, model);
  ok = newer.kind == ActionKind::changed && selected.detail_record().seq != selected_seq && ok;
  const Action back = selected.touch(layout::kDetailBack.x + layout::kDetailBack.w / 2,
                                     layout::kDetailBack.y + layout::kDetailBack.h / 2, model);
  ok = back.kind == ActionKind::changed && !selected.detail_open() && ok;
  for (const layout::Rect viewport : {layout::Rect{72, 72, 424, 1088}, layout::Rect{72, 72, 1088, 424}}) {
    ok = layout::set_viewport(viewport) && ok;
    (void)model.set_visible_message_rows(layout::kLiveVisibleRows);
    while (model.scroll_messages(-static_cast<int>(kHistoryCapacity))) {}
    const layout::Rect live_tab = layout::tab(0);
    Controller c;
    c.select_view(View::setup);
    const Action action = c.touch(live_tab.x + live_tab.w / 2, live_tab.y + live_tab.h / 2, model);
    ok = action.kind == ActionKind::changed && c.view() == View::live &&
         layout::kLiveVisibleRows >= 1 && layout::live_row(static_cast<int>(layout::kLiveVisibleRows - 1)).inside(viewport) &&
         layout::kLiveNavUp.h >= 44 && layout::kLiveNavDown.h >= 44 && live_tab.inside(viewport) && ok;
    const size_t before = model.snapshot().message_offset;
    const Action page = c.touch(layout::kLiveNavDown.x + layout::kLiveNavDown.w / 2,
                                layout::kLiveNavDown.y + layout::kLiveNavDown.h / 2, model);
    if (model.snapshot().matching_count > layout::kLiveVisibleRows)
      ok = page.kind == ActionKind::changed && model.snapshot().message_offset == before + layout::kLiveVisibleRows && ok;
    Controller channels;
    channels.select_view(View::channels);
    const layout::Rect last_channel = layout::ch_row(layout::kChVisibleRows - 1);
    const Action channel_pick = channels.touch(last_channel.x + last_channel.w / 2,
                                               last_channel.y + last_channel.h / 2, model);
    ok = last_channel.inside(viewport) && last_channel.h >= 44 &&
         channel_pick.kind == ActionKind::tune_requested && ok;
    Controller archive;
    archive.select_view(View::archive);
    const MessageRecord* archive_rows[layout::kArRows]{};
    size_t archive_total = 0;
    (void)model.archive_query(archive.archive_query(), 0, archive_rows, layout::kArRows, &archive_total);
    if (archive_total > layout::kArVisibleRows) {
      const Action next_archive_page = archive.touch(layout::kArDown.x + layout::kArDown.w / 2,
          layout::kArDown.y + layout::kArDown.h / 2, model);
      const size_t expected = std::min(static_cast<size_t>(layout::kArVisibleRows),
                                       archive_total - static_cast<size_t>(layout::kArVisibleRows));
      ok = next_archive_page.kind == ActionKind::changed && archive.archive_offset() == expected && ok;
    }
    archive.reflow_archive(model);
    const size_t archive_n = model.archive_query(archive.archive_query(), archive.archive_offset(),
        archive_rows, static_cast<size_t>(layout::kArVisibleRows), &archive_total);
    if (archive_n) {
      const layout::Rect last_archive_row = layout::ar_row(static_cast<int>(archive_n - 1));
      const Action open_archived = archive.touch(last_archive_row.x + last_archive_row.w / 2,
          last_archive_row.y + last_archive_row.h / 2, model);
      ok = open_archived.kind == ActionKind::changed && archive.detail_open() &&
           archive.detail_record().seq == archive_rows[archive_n - 1]->seq && ok;
    }
    Controller setup;
    setup.select_view(View::setup);
    const Region region_before = model.snapshot().prefs.region;
    const layout::Rect region_setting = layout::setup_item(0, 0);
    const Action region_change = setup.touch(region_setting.x + region_setting.w / 2,
        region_setting.y + region_setting.h / 2, model);
    ok = region_setting.inside(viewport) && region_setting.h >= 44 &&
         region_change.kind == ActionKind::changed && model.snapshot().prefs.region != region_before && ok;
  }
  (void)model.set_visible_message_rows(saved_rows);
  (void)model.set_visible_aircraft_rows(kAircraftRows);
  ok = layout::set_viewport(saved) && ok;
  return layout::tab(0).x == saved.x && layout::tab(5).x + layout::tab(5).w == saved.x + saved.w && ok;
}

}  // namespace orcsdr::acars_ui
