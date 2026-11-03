#include "../ui/pocsag_dashboard_layout.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

using orcsdr::pocsag::layout::Layout;
using orcsdr::pocsag::layout::Rect;
using orcsdr::pocsag::layout::make_layout;

static bool inside(Rect o, Rect r) {
  return r.w > 0 && r.h > 0 && r.x >= o.x && r.y >= o.y &&
         r.x + r.w <= o.x + o.w && r.y + r.h <= o.y + o.h;
}

static void check(Rect viewport) {
  const Layout l = make_layout(viewport);
  if (!l.valid || l.legacy) {
    std::fprintf(stderr,"invalid geometry for %dx%d at %d,%d\n",viewport.w,viewport.h,viewport.x,viewport.y);
    std::fprintf(stderr,"content=%d,%d %dx%d tabs=%d,%d %dx%d live=%d,%d %dx%d scan=%d,%d %dx%d detail=%d,%d %dx%d title=%d,%d %dx%d rows=%d firstactivity=%d,%d %dx%d pager=%d,%d %dx%d\n",
      l.content.x,l.content.y,l.content.w,l.content.h,l.tabs.x,l.tabs.y,l.tabs.w,l.tabs.h,
      l.live_inbox.x,l.live_inbox.y,l.live_inbox.w,l.live_inbox.h,l.scan.x,l.scan.y,l.scan.w,l.scan.h,
      l.detail.x,l.detail.y,l.detail.w,l.detail.h,l.detail_title.x,l.detail_title.y,l.detail_title.w,l.detail_title.h,
      l.list_row_count,l.activity_row[0].x,l.activity_row[0].y,l.activity_row[0].w,l.activity_row[0].h,
      l.activity_previous.x,l.activity_previous.y,l.activity_previous.w,l.activity_previous.h);
    std::abort();
  }
  assert(inside(viewport,l.content) && inside(viewport,l.tabs));
  for (const Rect r : l.tab) {
    assert(inside(l.tabs,r) && r.w >= 44 && r.h >= 44);
  }
  for (const Rect r : {l.live_hero,l.live_status,l.live_inbox,l.scan,
       l.signal_stats,l.signal_plot,l.baud,l.polarity,l.list_title,l.detail_title,l.detail,
       l.list_previous,l.list_next,l.list_page,l.detail_previous,
       l.detail_next,l.detail_page}) assert(inside(l.content,r));
  assert(l.baud.h >= 44 && l.polarity.h >= 44);
  assert(inside(l.live_inbox,l.scan));
  assert(!orcsdr::pocsag::layout::overlap(l.scan,l.live_hero));
  assert(!orcsdr::pocsag::layout::overlap(l.scan,l.live_status));
  assert(!orcsdr::pocsag::layout::overlap(l.signal_plot,l.baud));
  assert(!orcsdr::pocsag::layout::overlap(l.signal_plot,l.polarity));
  assert(!orcsdr::pocsag::layout::overlap(l.detail,l.detail_previous));
  assert(!orcsdr::pocsag::layout::overlap(l.detail,l.detail_page));
  assert(!orcsdr::pocsag::layout::overlap(l.detail,l.detail_next));
  assert(l.list_row_count >= 1 && l.list_row_count <= 12);
  for (int i=0;i<l.list_row_count;++i) {
    assert(inside(l.content,l.list_rows[i]) && l.list_rows[i].h >= 52);
    assert(!orcsdr::pocsag::layout::overlap(l.list_rows[i],l.list_previous));
    assert(!orcsdr::pocsag::layout::overlap(l.list_rows[i],l.list_page));
    assert(!orcsdr::pocsag::layout::overlap(l.list_rows[i],l.list_next));
  }
  assert(l.activity_row_count >= 1 && l.activity_row_count <= 8);
  for(int i=0;i<l.activity_row_count;++i) {
    const Rect r=l.activity_row[i];
    assert(inside(l.content,r));
    assert(!orcsdr::pocsag::layout::overlap(r,l.activity_previous));
    assert(!orcsdr::pocsag::layout::overlap(r,l.activity_page));
    assert(!orcsdr::pocsag::layout::overlap(r,l.activity_next));
  }
  for(const Rect r:l.activity) assert(inside(l.content,r) && r.h>=60);
  if (l.portrait) {
    assert(l.tab[0].y==l.tab[2].y && l.tab[3].y==l.tab[4].y);
    assert(l.live_status.y >= l.live_hero.y+l.live_hero.h);
    assert(l.live_inbox.y >= l.live_status.y+l.live_status.h);
  } else {
    assert(l.tab[0].y==l.tab[4].y);
    assert(l.live_status.x >= l.live_hero.x+l.live_hero.w);
    assert(l.live_inbox.y >= l.live_hero.y+l.live_hero.h);
  }
  assert(l.detail_columns > 0 && l.detail_lines > 0);
}

int main() {
  const Layout legacy=make_layout({0,0,1280,720});
  assert(legacy.valid && legacy.legacy && legacy.tabs.y==646 &&
         legacy.tabs.h==74 && legacy.content.y==112 && legacy.content.h==522);
  check({0,0,800,480});
  check({72,72,424,1088});
  check({72,72,1088,424});
  std::puts("POCSAG layout geometry tests PASS");
}
