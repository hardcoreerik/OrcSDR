#include "acars_dashboard_layout.hpp"

#include <algorithm>

namespace orcsdr::acars_ui::layout {
namespace {
constexpr Rect kLegacyViewport{0, 0, 1280, 720};

struct Sink {
  Rect* out;
  size_t capacity;
  size_t n = 0;
  void add(const Rect& r) {
    if (r.w <= 0 || r.h <= 0) return;
    if (n < capacity) out[n] = r;
    ++n;
  }
};
}  // namespace

int kScreenW = 1280;
int kScreenH = 720;
int kHeaderH = 104;
Rect kContext{20, 110, 1240, 40};
int kContentY = 158;
int kContentBottom = 638;
int kTabsY = 646;
Rect kViewport = kLegacyViewport;
size_t kLiveVisibleRows = 12;
Rect kLiveList{20, 158, 800, 480};
Rect kLiveStatus{836, 158, 424, 232};
Rect kLiveNav{836, 402, 424, 92};
Rect kLiveNavUp{848, 446, 196, 40};
Rect kLiveNavDown{1052, 446, 196, 40};
Rect kLiveNotice{836, 506, 424, 132};
Rect kDetailBar{20, 158, 1240, 48};
Rect kDetailBack{28, 162, 130, 40};
Rect kDetailNewer{988, 162, 124, 40};
Rect kDetailOlder{1124, 162, 128, 40};
Rect kDetailFields{20, 214, 400, 424};
Rect kDetailRaw{436, 214, 824, 268};
Rect kDetailRawUp{1100, 220, 70, 34};
Rect kDetailRawDown{1178, 220, 70, 34};
Rect kDetailRawText{448, 262, 800, 212};
int kDetailLines = 8;
int kDetailLineH = 26;
int kDetailCols = 57;
Rect kDetailInterp{436, 490, 824, 148};
Rect kAcList{20, 158, 660, 480};
Rect kAcControls{696, 158, 564, 60};
Rect kAcSort{704, 164, 200, 48};
Rect kAcUp{916, 164, 70, 48};
Rect kAcDown{994, 164, 70, 48};
Rect kAcCount{1072, 164, 180, 48};
Rect kAcDetail{696, 226, 564, 412};
Rect kAcMsgUp{};
Rect kAcMsgDown{};
Rect kAcNote{704, 612, 548, 24};
size_t kAcVisibleRows = 8;
size_t kAcMessageRows = 6;
Rect kChList{20, 158, 760, 480}, kChRegion{28, 166, 330, 48};
Rect kChCustomLabel{28, 572, 100, 52}, kChMinus{136, 572, 90, 52}, kChPlus{232, 572, 90, 52};
Rect kChClear{328, 572, 110, 52}, kChCustomValue{446, 572, 326, 52};
Rect kChStatus{796, 158, 464, 300}, kChNotes{796, 470, 464, 168};
int kChVisibleRows = kChRows;
Rect kActYield{20, 418, 616, 220}, kActSignal{652, 418, 608, 220};
Rect kArFilters{20, 158, 400, 480}, kArClear{28, 548, 384, 52}, kArList{436, 158, 824, 480};
Rect kArUp{1100, 162, 70, 32}, kArDown{1180, 162, 70, 32};
int kArVisibleRows = kArRows;

Rect tab(int i) {
  if (i < 0 || i >= kTabCount) return {};
  const int x0 = kViewport.x + i * kViewport.w / kTabCount;
  const int x1 = kViewport.x + (i + 1) * kViewport.w / kTabCount;
  return {x0, kTabsY, x1 - x0, kViewport.y + kViewport.h - kTabsY};
}

Rect live_row(int i) {
  if (i < 0 || static_cast<size_t>(i) >= kLiveVisibleRows) return {};
  const int top = kLiveList.y + (kViewport == kLegacyViewport ? 38 : 36);
  const int row_h = kLiveVisibleRows ? (kLiveList.y + kLiveList.h - top) / static_cast<int>(kLiveVisibleRows) : 0;
  return {kLiveList.x + 8, top + i * row_h, kLiveList.w - 16, row_h};
}

Rect ac_row(int i) {
  if (i < 0 || static_cast<size_t>(i) >= kAcVisibleRows) return {};
  if (kViewport == kLegacyViewport) return {28, 200 + i * 54, 644, 54};
  const int top = kAcList.y + (kViewport == kLegacyViewport ? 42 : 34);
  const int row_h = kAcVisibleRows ? (kAcList.y + kAcList.h - top) / static_cast<int>(kAcVisibleRows) : 0;
  return {kAcList.x + 8, top + i * row_h, kAcList.w - 16, row_h};
}

Rect ac_msg_row(int j) {
  if (j < 0 || static_cast<size_t>(j) >= kAcMessageRows) return {};
  if (kViewport == kLegacyViewport) return {704, 276 + j * 56, 548, 56};
  const int top = kAcDetail.y + (kViewport == kLegacyViewport ? 50 : 32);
  const int bottom = kAcNote.h > 0 ? kAcNote.y - 4 : kAcMsgUp.y - 4;
  const int row_h = kAcMessageRows ? (bottom - top) / static_cast<int>(kAcMessageRows) : 0;
  return {kAcDetail.x + 8, top + j * row_h, kAcDetail.w - 16, row_h};
}

Rect ch_row(int i) {
  if (i < 0 || i >= kChVisibleRows) return {};
  if (kViewport == kLegacyViewport) return {28, 226 + i * 48, 744, 48};
  const int top = kChRegion.y + kChRegion.h + 4;
  const int bottom = kChCustomLabel.h ? kChCustomLabel.y - 4 : kChList.y + kChList.h;
  const int row_h = kChVisibleRows ? (bottom - top) / kChVisibleRows : 0;
  return {kChList.x + 8, top + i * row_h, kChList.w - 16, row_h};
}

Rect act_tile(int i) {
  if (i < 0 || i >= kActTiles) return {};
  if (kViewport == kLegacyViewport) return {20 + (i % 4) * 312, 158 + (i / 4) * 128, 300, 116};
  const bool portrait = kViewport.h > kViewport.w;
  if (portrait) {
    const int gap = 6, cols = 2, w = (kViewport.w - 16 - gap) / cols;
    const int row_h = std::max(78, std::min(92, (kContentBottom - kContentY - 300) / 4));
    return {kViewport.x + 8 + (i % cols) * (w + gap), kContentY + (i / cols) * (row_h + gap), w, row_h};
  }
  const int gap = 6, cols = 4, w = (kViewport.w - 16 - (cols - 1) * gap) / cols;
  const int tile_h = std::max(60, (kContentBottom - kContentY - 30 - gap) / 2);
  return {kViewport.x + 8 + (i % cols) * (w + gap), kContentY + (i / cols) * (tile_h + gap), w, tile_h};
}

Rect ar_filter(int i) {
  if (i < 0 || i >= kArFilterCount) return {};
  if (kViewport == kLegacyViewport) return {28, 200 + i * 68, 384, 60};
  if (kViewport.h > kViewport.w) return {kArFilters.x + 8, kArFilters.y + 34 + i * 48, kArFilters.w - 16, 44};
  if (i < 2) return {kArFilters.x + 6, kArFilters.y + 8 + i * 50, kArFilters.w - 12, 44};
  return {kArFilters.x + 8, kArFilters.y + 100 + (i - 2) * 24, kArFilters.w - 16, 22};
}

Rect ar_row(int i) {
  if (i < 0 || i >= kArVisibleRows) return {};
  if (kViewport == kLegacyViewport) return {444, 200 + i * 54, 808, 54};
  const int header = kViewport.h > kViewport.w ? 46 : 50;
  const int top = kArList.y + header;
  const int bottom = kArList.y + kArList.h - 4;
  const int row_h = kArVisibleRows ? (bottom - top) / kArVisibleRows : 0;
  return {kArList.x + 6, top + i * row_h, kArList.w - 12, row_h};
}

Rect setup_item(int column, int row) {
  if (column < 0 || column >= 2 || row < 0 || row >= 5) return {};
  const int item = column * 5 + row;
  if (kViewport == kLegacyViewport) return {20 + column * 624, 166 + row * 92, 616, 84};
  if (kViewport.h > kViewport.w) {
    const int gap = 6, h = (kContentBottom - kContentY - 9 * gap) / 10;
    return {kViewport.x + 8, kContentY + item * (h + gap), kViewport.w - 16, h};
  }
  const int gap = 2, h = (kContentBottom - kContentY - 4 * gap) / 5;
  const int w = (kViewport.w - 16 - gap) / 2;
  return {kViewport.x + 8 + column * (w + gap), kContentY + row * (h + gap), w, h};
}

bool set_viewport(Rect viewport) {
  if (viewport.w < 280 || viewport.h < 240 || viewport.x < 0 || viewport.y < 0) return false;
  kViewport = viewport;
  kScreenW = viewport.w;
  kScreenH = viewport.h;
  if (viewport == kLegacyViewport) {
    kHeaderH = 104; kContext = {20, 110, 1240, 40}; kContentY = 158;
    kContentBottom = 638; kTabsY = 646; kLiveVisibleRows = 12;
    kLiveList = {20, 158, 800, 480}; kLiveStatus = {836, 158, 424, 232};
    kLiveNav = {836, 402, 424, 92}; kLiveNavUp = {848, 446, 196, 40};
    kLiveNavDown = {1052, 446, 196, 40}; kLiveNotice = {836, 506, 424, 132};
    kDetailBar = {20, 158, 1240, 48}; kDetailBack = {28, 162, 130, 40};
    kDetailNewer = {988, 162, 124, 40}; kDetailOlder = {1124, 162, 128, 40};
    kDetailFields = {20, 214, 400, 424}; kDetailRaw = {436, 214, 824, 268};
    kDetailRawUp = {1100, 220, 70, 34}; kDetailRawDown = {1178, 220, 70, 34};
    kDetailRawText = {448, 262, 800, 212}; kDetailLines = 8; kDetailLineH = 26; kDetailCols = 57;
    kDetailInterp = {436, 490, 824, 148};
    kAcList = {20, 158, 660, 480}; kAcControls = {696, 158, 564, 60};
    kAcSort = {704, 164, 200, 48}; kAcUp = {916, 164, 70, 48}; kAcDown = {994, 164, 70, 48};
    kAcCount = {1072, 164, 180, 48}; kAcDetail = {696, 226, 564, 412};
    kAcMsgUp = {}; kAcMsgDown = {};
    kAcNote = {704, 612, 548, 24}; kAcVisibleRows = 8; kAcMessageRows = 6;
    kChList = {20, 158, 760, 480}; kChRegion = {28, 166, 330, 48}; kChVisibleRows = 7;
    kChCustomLabel = {28, 572, 100, 52}; kChMinus = {136, 572, 90, 52}; kChPlus = {232, 572, 90, 52};
    kChClear = {328, 572, 110, 52}; kChCustomValue = {446, 572, 326, 52};
    kChStatus = {796, 158, 464, 300}; kChNotes = {796, 470, 464, 168};
    return true;
  }

  const bool portrait = viewport.h > viewport.w;
  kHeaderH = viewport.w < 800 ? 136 : 64;
  kTabsY = viewport.y + viewport.h - 74;
  kContext = {viewport.x + 8, viewport.y + kHeaderH + 4, viewport.w - 16, 40};
  kContentY = kContext.y + kContext.h + 6;
  kContentBottom = kTabsY - 6;
  const int content_h = kContentBottom - kContentY;
  const int detail_button_h = 44;
  const int detail_bar_h = 48;
  const int detail_y = kContentY;
  const int detail_bottom = kContentBottom;
  kDetailBar = {viewport.x + 8, detail_y, viewport.w - 16, detail_bar_h};
  const int detail_button_w = portrait ? 64 : 88;
  kDetailBack = {kDetailBar.x + 2, detail_y + 2, detail_button_w, detail_button_h};
  kDetailNewer = {kDetailBar.x + kDetailBar.w - 2 * detail_button_w - 8, detail_y + 2,
                  detail_button_w, detail_button_h};
  kDetailOlder = {kDetailBar.x + kDetailBar.w - detail_button_w - 2, detail_y + 2,
                  detail_button_w, detail_button_h};
  if (portrait) {
    const int x = viewport.x + 8, w = viewport.w - 16;
    kChRegion = {x, kContentY, w, 48};
    kChVisibleRows = std::min(kChRows, std::max(1, (content_h - 430) / 48));
    const int rows_y = kContentY + 54;
    const int custom_y = rows_y + kChVisibleRows * 48 + 4;
    kChList = {x, rows_y, w, custom_y - rows_y - 4};
    kChCustomLabel = {x, custom_y, 88, 44};
    kChMinus = {x + 92, custom_y, 64, 44}; kChPlus = {x + 160, custom_y, 64, 44};
    kChClear = {x + 228, custom_y, 72, 44}; kChCustomValue = {x + 304, custom_y, w - 304, 44};
    const int status_y = custom_y + 52;
    kChStatus = {x, status_y, w, 172};
    kChNotes = {x, status_y + 178, w, std::max(0, kContentBottom - (status_y + 178))};
  } else {
    const int x = viewport.x + 8, w = viewport.w - 16, gap = 8;
    const int list_w = w * 56 / 100, side_x = x + list_w + gap, side_w = w - list_w - gap;
    kChVisibleRows = 2;
    kChList = {x, kContentY, list_w, content_h};
    kChRegion = {x + 6, kContentY + 4, list_w - 12, 44};
    kChCustomLabel = {x + 4, kContentBottom - 46, 82, 44};
    kChMinus = {x + 90, kContentBottom - 46, 60, 44}; kChPlus = {x + 154, kContentBottom - 46, 60, 44};
    kChClear = {x + 218, kContentBottom - 46, 68, 44}; kChCustomValue = {x + 290, kContentBottom - 46, list_w - 294, 44};
    kChStatus = {side_x, kContentY, side_w, content_h * 48 / 100};
    kChNotes = {side_x, kChStatus.y + kChStatus.h + 6, side_w, kContentBottom - (kChStatus.y + kChStatus.h + 6)};
  }
  if (viewport == kLegacyViewport) {
    kActYield = {20, 418, 616, 220}; kActSignal = {652, 418, 608, 220};
    kArFilters = {20, 158, 400, 480}; kArClear = {28, 548, 384, 52}; kArList = {436, 158, 824, 480};
    kArUp = {1100, 162, 70, 32}; kArDown = {1180, 162, 70, 32}; kArVisibleRows = 8;
  } else if (portrait) {
    const int row_h = std::max(78, std::min(92, (kContentBottom - kContentY - 300) / 4));
    const int y = kContentY + 4 * (row_h + 6) + 4;
    kActYield = {viewport.x + 8, y, viewport.w - 16, 142};
    kActSignal = {viewport.x + 8, y + 148, viewport.w - 16,
                  std::max(0, kContentBottom - (y + 148))};
  } else {
    const int tile_h = std::max(60, (kContentBottom - kContentY - 30 - 6) / 2);
    kActYield = {viewport.x + 8, kContentY + 2 * (tile_h + 6), viewport.w - 16,
                 std::max(0, kContentBottom - (kContentY + 2 * (tile_h + 6)))};
    kActSignal = {};
  }
  if (!(viewport == kLegacyViewport) && portrait) {
    const int x = viewport.x + 8, w = viewport.w - 16;
    kArFilters = {x, kContentY, w, 286};
    kArClear = {x + 8, kArFilters.y + kArFilters.h - 48, w - 16, 44};
    const int list_y = kArFilters.y + kArFilters.h + 6;
    kArList = {x, list_y, w, kContentBottom - list_y};
    kArUp = {x + w - 204, list_y + 2, 96, 44};
    kArDown = {x + w - 104, list_y + 2, 96, 44};
    kArVisibleRows = std::max(1, std::min(8, (kArList.h - 58) / 54));
  } else if (!(viewport == kLegacyViewport)) {
    const int x = viewport.x + 8, w = viewport.w - 16, gap = 8;
    const int filter_w = w * 34 / 100;
    kArFilters = {x, kContentY, filter_w, content_h};
    kArClear = {x + 8, kArFilters.y + kArFilters.h - 48, filter_w - 16, 44};
    kArList = {x + filter_w + gap, kContentY, w - filter_w - gap, content_h};
    kArUp = {kArList.x + kArList.w - 204, kArList.y + 2, 96, 44};
    kArDown = {kArList.x + kArList.w - 104, kArList.y + 2, 96, 44};
    kArVisibleRows = std::max(1, std::min(2, (kArList.h - 58) / 54));
  }
  if (portrait) {
    const int top = detail_y + detail_bar_h + 6;
    const int fields_h = 240;
    kDetailFields = {viewport.x + 8, top, viewport.w - 16, fields_h};
    kDetailInterp = {viewport.x + 8, top + fields_h + 6, viewport.w - 16, 120};
    const int raw_y = kDetailInterp.y + kDetailInterp.h + 6;
    kDetailRaw = {viewport.x + 8, raw_y, viewport.w - 16, std::max(80, detail_bottom - raw_y)};
  } else {
    const int top = detail_y + detail_bar_h + 6;
    kDetailFields = {viewport.x + 8, top, viewport.w - 16, 64};
    const int raw_y = top + kDetailFields.h + 6;
    kDetailRaw = {viewport.x + 8, raw_y, viewport.w - 16, std::max(72, detail_bottom - raw_y)};
    kDetailInterp = {kDetailRaw.x + 8, kDetailRaw.y + kDetailRaw.h - 18, kDetailRaw.w - 16, 16};
  }
  kDetailRawUp = {kDetailRaw.x + kDetailRaw.w - 144, kDetailRaw.y + 4, 64, detail_button_h};
  kDetailRawDown = {kDetailRaw.x + kDetailRaw.w - 72, kDetailRaw.y + 4, 64, detail_button_h};
  kDetailRawText = {kDetailRaw.x + 10, kDetailRaw.y + 52, kDetailRaw.w - 20,
                    std::max(20, kDetailRaw.h - (portrait ? 86 : 70))};
  kDetailCols = std::max(1, std::min(96, (kDetailRawText.w - 8) / 14));
  kDetailLineH = portrait ? 24 : 18;
  kDetailLines = std::max(1, kDetailRawText.h / kDetailLineH);
  const int inner_x = viewport.x + 8;
  const int inner_w = viewport.w - 16;
  if (portrait) {
    kAcControls = {inner_x, kContentY, inner_w, 50};
    kAcSort = {inner_x + 2, kContentY + 3, 154, 44};
    kAcUp = {inner_x + 162, kContentY + 3, 58, 44};
    kAcDown = {inner_x + 224, kContentY + 3, 58, 44};
    kAcCount = {inner_x + 288, kContentY + 3, inner_w - 292, 44};
    kAcVisibleRows = std::min<size_t>(6, static_cast<size_t>(std::max(1, (content_h - 300) / 48)));
    const int list_y = kContentY + 56;
    kAcList = {inner_x, list_y, inner_w, 34 + static_cast<int>(kAcVisibleRows) * 48};
    const int aircraft_detail_y = kAcList.y + kAcList.h + 6;
    kAcDetail = {inner_x, aircraft_detail_y, inner_w, std::max(150, kContentBottom - aircraft_detail_y)};
    kAcMessageRows = 4;
    kAcMsgUp = {inner_x + 4, kAcDetail.y + kAcDetail.h - 48, (inner_w - 12) / 2, 44};
    kAcMsgDown = {kAcMsgUp.x + kAcMsgUp.w + 8, kAcMsgUp.y, inner_w - kAcMsgUp.w - 12, 44};
    kAcNote = {inner_x + 8, kAcDetail.y + kAcDetail.h - 78, inner_w - 16, 22};
  } else {
    const int gap = 8;
    kAcControls = {inner_x, kContentY, inner_w, 48};
    kAcSort = {inner_x + 2, kContentY + 2, 210, 44};
    kAcUp = {inner_x + 220, kContentY + 2, 64, 44};
    kAcDown = {inner_x + 290, kContentY + 2, 64, 44};
    kAcCount = {inner_x + 362, kContentY + 2, inner_w - 366, 44};
    const int list_y = kContentY + 54;
    const int list_w = (inner_w - gap) * 45 / 100;
    const int panel_h = kContentBottom - list_y;
    kAcVisibleRows = std::max<size_t>(1, std::min<size_t>(2, static_cast<size_t>((panel_h - 34) / 48)));
    kAcList = {inner_x, list_y, list_w, panel_h};
    kAcDetail = {inner_x + list_w + gap, list_y, inner_w - list_w - gap, panel_h};
    kAcMessageRows = 1;
    kAcMsgUp = {kAcDetail.x + 8, kAcDetail.y + kAcDetail.h - 46, (kAcDetail.w - 20) / 2, 44};
    kAcMsgDown = {kAcMsgUp.x + kAcMsgUp.w + 4, kAcMsgUp.y, kAcDetail.w - kAcMsgUp.w - 20, 44};
    kAcNote = {};
  }
  if (portrait) {
    // Stack the message list and status/paging cards; rows use two text lines.
    kLiveVisibleRows = std::min<size_t>(6, static_cast<size_t>(std::max(1, (content_h - 300) / 68)));
    const int list_h = 36 + static_cast<int>(kLiveVisibleRows) * 68;
    kLiveList = {viewport.x + 8, kContentY, viewport.w - 16, list_h};
    const int below = kLiveList.y + kLiveList.h + 6;
    kLiveStatus = {viewport.x + 8, below, viewport.w - 16, 74};
    kLiveNav = {viewport.x + 8, kLiveStatus.y + 80, viewport.w - 16, 54};
    const int gap = 6, button_w = (kLiveNav.w - gap) / 2;
    kLiveNavUp = {kLiveNav.x, kLiveNav.y + 5, button_w, 44};
    kLiveNavDown = {kLiveNav.x + button_w + gap, kLiveNav.y + 5, button_w, 44};
    kLiveNotice = {viewport.x + 8, kLiveNav.y + 60, viewport.w - 16,
                   std::max(0, kContentBottom - (kLiveNav.y + 60))};
  } else {
    // Keep list rows, status and both page controls at usable sizes in the short layout.
    const int gap = 8, live_inner_w = viewport.w - 16;
    const int list_w = live_inner_w * 64 / 100;
    const int side_x = viewport.x + 8 + list_w + gap;
    const int side_w = std::max(0, live_inner_w - list_w - gap);
    kLiveVisibleRows = std::max<size_t>(1, static_cast<size_t>((content_h - 44) / 54));
    kLiveVisibleRows = std::min<size_t>(3, kLiveVisibleRows);
    kLiveList = {viewport.x + 8, kContentY, list_w, content_h};
    kLiveStatus = {side_x, kContentY, side_w, 72};
    kLiveNav = {side_x, kContentY + 78, side_w, content_h - 78};
    kLiveNavUp = {side_x, kLiveNav.y, (side_w - 6) / 2, 44};
    kLiveNavDown = {kLiveNavUp.x + kLiveNavUp.w + 6, kLiveNav.y, side_w - kLiveNavUp.w - 6, 44};
    kLiveNotice = {};
  }
  return kLiveVisibleRows > 0 && kLiveList.w > 0 && kLiveList.h > 0 &&
         kLiveNavUp.h >= 44 && kLiveNavDown.h >= 44;
}

void reset_viewport() { (void)set_viewport(kLegacyViewport); }

size_t collect_rects(View view, bool detail, Rect* out, size_t capacity) {
  Sink s{out, capacity};
  s.add(kContext);
  for (int i = 0; i < kTabCount; ++i) s.add(tab(i));
  if (detail) {
    s.add(kDetailBar); s.add(kDetailBack); s.add(kDetailNewer); s.add(kDetailOlder);
    s.add(kDetailFields); s.add(kDetailRaw); s.add(kDetailRawUp); s.add(kDetailRawDown);
    s.add(kDetailRawText); s.add(kDetailInterp);
  } else {
    switch (view) {
      case View::live:
        s.add(kLiveList); s.add(kLiveStatus); s.add(kLiveNav); s.add(kLiveNavUp); s.add(kLiveNavDown);
        s.add(kLiveNotice);
        for (size_t i = 0; i < kLiveVisibleRows; ++i) s.add(live_row(static_cast<int>(i)));
        break;
      case View::aircraft:
        s.add(kAcList); s.add(kAcControls); s.add(kAcSort); s.add(kAcUp); s.add(kAcDown); s.add(kAcCount);
        s.add(kAcDetail); s.add(kAcMsgUp); s.add(kAcMsgDown); s.add(kAcNote);
        for (size_t i = 0; i < kAcVisibleRows; ++i) s.add(ac_row(static_cast<int>(i)));
        for (size_t j = 0; j < kAcMessageRows; ++j) s.add(ac_msg_row(static_cast<int>(j)));
        break;
      case View::channels:
        s.add(kChList); s.add(kChRegion); s.add(kChCustomLabel); s.add(kChMinus); s.add(kChPlus);
        s.add(kChClear); s.add(kChCustomValue); s.add(kChStatus); s.add(kChNotes);
        for (int i = 0; i < kChVisibleRows; ++i) s.add(ch_row(i));
        break;
      case View::activity:
        for (int i = 0; i < kActTiles; ++i) s.add(act_tile(i));
        s.add(kActYield); s.add(kActSignal);
        break;
      case View::archive:
        s.add(kArFilters); s.add(kArClear); s.add(kArList); s.add(kArUp); s.add(kArDown);
        for (int i = 0; i < kArFilterCount; ++i) s.add(ar_filter(i));
        for (int i = 0; i < kArVisibleRows; ++i) s.add(ar_row(i));
        break;
      case View::setup:
        for (int c = 0; c < 2; ++c)
          for (int r = 0; r < 5; ++r) s.add(setup_item(c, r));
        break;
      default:
        break;
    }
  }
  return s.n < capacity ? s.n : capacity;
}

}  // namespace orcsdr::acars_ui::layout
