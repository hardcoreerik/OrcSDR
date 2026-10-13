#pragma once
// ACARS dashboard interaction state: current tab, message detail page, scroll positions, archive filters,
// and the touch -> model-change logic. Pure C++ (no M5GFX), so every control is host-tested.
// Drawing (acars_dashboard.cpp) reads this state and the model snapshot; it never decides behaviour.
#include <cstddef>
#include <cstdint>

#include "acars_dashboard_layout.hpp"
#include "acars_dashboard_model.hpp"

namespace orcsdr::acars_ui {

enum class ActionKind : uint8_t {
  none,
  tune_requested,   // value = requested Hz. A request only: no radio is claimed to have moved.
  demo_changed,     // value = 1 demo on, 0 off
  changed           // a preference or view changed; nothing else to do
};
struct Action {
  ActionKind kind = ActionKind::none;
  uint32_t value = 0;
};

constexpr size_t kAircraftRecentShown = 6;

class Controller {
 public:
  void reset();
  View view() const { return view_; }
  bool detail_open() const { return detail_open_; }
  // Increases on every UI-state change that needs a repaint (tab, detail, scroll, selection, filters).
  uint32_t revision() const { return revision_; }

  // Touch in screen coordinates. Taps in the shared header (y < 104) are not this dashboard's.
  Action touch(int32_t x, int32_t y, Model& model);
  void select_view(View view);
  bool open_detail(uint32_t seq, const Model& model);
  void close_detail();

  // ---- state for the renderer
  const MessageRecord& detail_record() const { return detail_record_; }
  bool detail_held(const Model& model) const { return model.find_message(detail_record_.seq) != nullptr; }
  const char* detail_text() const { return detail_text_; }
  size_t detail_text_length() const { return detail_text_len_; }
  size_t detail_line() const { return detail_line_; }
  size_t detail_line_count() const;
  bool detail_line_at(size_t index, const char** start, size_t* length) const;   // wrapped line
  const char* selected_tail() const { return selected_tail_; }
  const ArchiveQuery& archive_query() const { return query_; }
  size_t archive_offset() const { return archive_offset_; }
  void refresh_detail_text(const Model& model);

 private:
  void bump() { ++revision_; }
  Action touch_live(int32_t x, int32_t y, Model& model);
  Action touch_detail(int32_t x, int32_t y, Model& model);
  Action touch_aircraft(int32_t x, int32_t y, Model& model);
  Action touch_channels(int32_t x, int32_t y, Model& model);
  Action touch_archive(int32_t x, int32_t y, Model& model);
  Action touch_setup(int32_t x, int32_t y, Model& model);

  View view_ = View::live;
  bool detail_open_ = false;
  uint32_t revision_ = 1;
  MessageRecord detail_record_{};
  char detail_text_[kEscapedMaxChars]{};
  size_t detail_text_len_ = 0;
  size_t detail_line_ = 0;
  char selected_tail_[8]{};
  bool tail_selected_ = false;
  ArchiveQuery query_{};
  size_t archive_offset_ = 0;
};

bool ui_self_check();

}  // namespace orcsdr::acars_ui
