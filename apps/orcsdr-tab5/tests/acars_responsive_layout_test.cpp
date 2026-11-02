#include "acars_dashboard_layout.hpp"
#include "acars_dashboard_ui.hpp"
#include "dashboard_header_layout.hpp"

#include <cassert>

using namespace orcsdr::acars_ui;

namespace {
void check_viewport(layout::Rect viewport, size_t expected_rows, size_t expected_aircraft_rows) {
  assert(layout::set_viewport(viewport));
  assert(layout::kLiveVisibleRows == expected_rows);
  const auto header = orcsdr::audio_header::make_header_layout(
      {viewport.x, viewport.y, viewport.w, viewport.h});
  assert(header.valid);
  assert(header.brand.x >= viewport.x && header.brand.y >= viewport.y);
  assert(header.content_top <= layout::kContext.y);
  assert(layout::kDetailBack.h >= 44 && layout::kDetailNewer.h >= 44 && layout::kDetailOlder.h >= 44);
  assert(layout::kDetailRawUp.h >= 44 && layout::kDetailRawDown.h >= 44);
  assert(layout::kDetailLines >= 1 && layout::kDetailCols >= 1);
  layout::Rect rects[32]{};
  const size_t count = layout::collect_rects(View::live, false, rects, 32);
  assert(count <= 32);
  for (size_t i = 0; i < count; ++i) assert(rects[i].inside(viewport));
  for (size_t i = 0; i < layout::kLiveVisibleRows; ++i)
    assert(layout::live_row(static_cast<int>(i)).h >= 44);
  assert(layout::kLiveNavUp.h >= 44 && layout::kLiveNavDown.h >= 44);
  assert(layout::tab(0).inside(viewport));
  assert(layout::tab(5).inside(viewport));
  assert(layout::tab(5).x + layout::tab(5).w == viewport.x + viewport.w);
  const size_t detail_count = layout::collect_rects(View::live, true, rects, 32);
  assert(detail_count <= 32);
  for (size_t i = 0; i < detail_count; ++i) assert(rects[i].inside(viewport));
  assert(layout::kAcVisibleRows == expected_aircraft_rows);
  assert(layout::kAcMessageRows >= 1);
  assert(layout::kAcUp.h >= 44 && layout::kAcDown.h >= 44);
  assert(layout::kAcMsgUp.h >= 44 && layout::kAcMsgDown.h >= 44);
  for (size_t i = 0; i < layout::kAcVisibleRows; ++i)
    assert(layout::ac_row(static_cast<int>(i)).h >= 44);
  for (size_t i = 0; i < layout::kAcMessageRows; ++i)
    assert(layout::ac_msg_row(static_cast<int>(i)).h >= 44);
  const size_t aircraft_count = layout::collect_rects(View::aircraft, false, rects, 32);
  assert(aircraft_count <= 32);
  for (size_t i = 0; i < aircraft_count; ++i) assert(rects[i].inside(viewport));
  assert(layout::kChVisibleRows >= 1 && layout::kChVisibleRows <= layout::kChRows);
  assert(layout::kChMinus.h >= 42 && layout::kChPlus.h >= 42 && layout::kChClear.h >= 42);
  for (int i = 0; i < layout::kChVisibleRows; ++i) {
    const auto row = layout::ch_row(i);
    assert(row.inside(viewport) && row.h >= 44);
  }
  const size_t channels_count = layout::collect_rects(View::channels, false, rects, 32);
  assert(channels_count <= 32);
  for (size_t i = 0; i < channels_count; ++i) assert(rects[i].inside(viewport));
  for (int i = 0; i < layout::kActTiles; ++i) {
    const auto tile = layout::act_tile(i);
    assert(tile.inside(viewport) && tile.w >= 44 && tile.h >= 44);
  }
  const size_t activity_count = layout::collect_rects(View::activity, false, rects, 32);
  assert(activity_count <= 32);
  for (size_t i = 0; i < activity_count; ++i) assert(rects[i].inside(viewport));
  assert(layout::kArVisibleRows >= 1 && layout::kArVisibleRows <= layout::kArRows);
  if (!(viewport == layout::Rect{0, 0, 1280, 720}))
    assert(layout::kArUp.h >= 44 && layout::kArDown.h >= 44);
  for (int i = 0; i < layout::kArVisibleRows; ++i) {
    const auto row = layout::ar_row(i);
    assert(row.inside(viewport) && row.h >= 44);
  }
  const size_t archive_count = layout::collect_rects(View::archive, false, rects, 32);
  assert(archive_count <= 32);
  for (size_t i = 0; i < archive_count; ++i) assert(rects[i].inside(viewport));
  for (int item = 0; item < layout::kSetupItems; ++item) {
    const auto r = layout::setup_item(item / 5, item % 5);
    assert(r.inside(viewport) && r.h >= 44);
  }
  const size_t setup_count = layout::collect_rects(View::setup, false, rects, 32);
  assert(setup_count <= 32);
  for (size_t i = 0; i < setup_count; ++i) assert(rects[i].inside(viewport));
}
}  // namespace

int main() {
  check_viewport({72, 72, 424, 1088}, 6, 6);
  check_viewport({72, 72, 1088, 424}, 3, 2);
  assert(layout::set_viewport({0, 0, 1280, 720}));
  assert(layout::kLiveVisibleRows == 12);
  assert(layout::kAcVisibleRows == 8 && layout::kAcMessageRows == 6);
  assert(layout::tab(0).x == 0 && layout::tab(5).x + layout::tab(5).w == 1280);
  assert(orcsdr::acars_ui::ui_self_check());
  return 0;
}
