#pragma once
// AIS dashboard M5Stack Tab5 layout. Pure constexpr geometry shared by M5GFX and host tests.
// 1280x720 landscape; y 0..104 is the shared OrcSDR header; bottom tabs begin at y 650.
#include <cstddef>
#include <cstdint>
namespace orcsdr::ais_ui {
enum class View : uint8_t { map, targets, messages, protocol, signal, events, setup, count };
namespace layout {
struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  constexpr bool contains(int px,int py) const { return px>=x&&py>=y&&px<x+w&&py<y+h; }
  constexpr bool overlaps(Rect b) const { return x<b.x+b.w&&b.x<x+w&&y<b.y+b.h&&b.y<y+h; }
};
constexpr int kWidth=1280,kHeight=720,kBodyY=114,kBodyBottom=638,kTabsY=650;
constexpr Rect kReservedStatus{420,12,446,54};
constexpr Rect kContext{20,114,1240,34};
constexpr Rect kContent{20,158,1240,478};
constexpr int kTabsCount=7,kTabGap=10,kTabW=168,kTabH=52;
constexpr Rect tab(int i) { return {20+i*(kTabW+kTabGap),kTabsY,kTabW,kTabH}; }
constexpr Rect mapMain{20,158,808,478}, mapSummary{840,158,420,218}, mapDetail{840,388,420,248};
constexpr Rect targetsTable{20,158,720,478}, targetsDetail{752,158,508,478};
constexpr Rect messagesTable{20,158,824,478}, messagesDetail{856,158,404,478};
constexpr Rect protocolIndex{20,158,488,478}, protocolDetail{520,158,740,478};
constexpr Rect signalLeftTop{20,158,610,222}, signalLeftBottom{20,392,610,244}, signalRight{642,158,618,478};
constexpr Rect eventsTable{20,158,752,478}, eventsDetail{784,158,476,478};
constexpr Rect setupReceiver{20,158,606,478},setupOutput{638,158,622,478};
constexpr bool fits(Rect r) { return r.x>=0&&r.y>=0&&r.w>0&&r.h>0&&r.x+r.w<=kWidth&&r.y+r.h<=kHeight; }
constexpr bool within_content(Rect r) { return fits(r)&&r.x>=kContent.x&&r.x+r.w<=kContent.x+kContent.w&&r.y>=kContent.y&&r.y+r.h<=kContent.y+kContent.h; }
static_assert(!kContent.overlaps(kReservedStatus));
static_assert(within_content(mapMain)&&within_content(mapSummary)&&within_content(mapDetail));
static_assert(within_content(targetsTable)&&within_content(targetsDetail));
static_assert(within_content(messagesTable)&&within_content(messagesDetail));
static_assert(within_content(protocolIndex)&&within_content(protocolDetail));
static_assert(within_content(signalLeftTop)&&within_content(signalLeftBottom)&&within_content(signalRight));
static_assert(within_content(eventsTable)&&within_content(eventsDetail));
static_assert(within_content(setupReceiver)&&within_content(setupOutput));
static_assert(fits(tab(0))&&fits(tab(6))&&!tab(0).overlaps(tab(1)));
static_assert(tab(6).x+tab(6).w==1256);
} // namespace layout
} // namespace orcsdr::ais_ui
