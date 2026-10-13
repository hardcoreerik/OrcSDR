#include "acars_dashboard_layout.hpp"

namespace orcsdr::acars_ui::layout {
namespace {
struct Sink {
  Rect* out;
  size_t capacity;
  size_t n = 0;
  void add(const Rect& r) {
    if (n < capacity) out[n] = r;
    ++n;
  }
};
}  // namespace

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
        for (int i = 0; i < 12; ++i) s.add(live_row(i));
        break;
      case View::aircraft:
        s.add(kAcList); s.add(kAcControls); s.add(kAcSort); s.add(kAcUp); s.add(kAcDown); s.add(kAcCount);
        s.add(kAcDetail); s.add(kAcNote);
        for (int i = 0; i < 8; ++i) s.add(ac_row(i));
        for (int j = 0; j < 6; ++j) s.add(ac_msg_row(j));
        break;
      case View::channels:
        s.add(kChList); s.add(kChRegion); s.add(kChCustomLabel); s.add(kChMinus); s.add(kChPlus);
        s.add(kChClear); s.add(kChCustomValue); s.add(kChStatus); s.add(kChNotes);
        for (int i = 0; i < 6; ++i) s.add(ch_row(i));
        break;
      case View::activity:
        for (int i = 0; i < kActTiles; ++i) s.add(act_tile(i));
        s.add(kActYield); s.add(kActSignal);
        break;
      case View::archive:
        s.add(kArFilters); s.add(kArClear); s.add(kArList); s.add(kArUp); s.add(kArDown);
        for (int i = 0; i < kArFilterCount; ++i) s.add(ar_filter(i));
        for (int i = 0; i < kArRows; ++i) s.add(ar_row(i));
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
