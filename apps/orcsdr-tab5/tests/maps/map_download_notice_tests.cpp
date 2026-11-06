#include "map_download_notice.hpp"
#include "focus_nav.hpp"
#include <M5Unified.h>
#include <cassert>
#include <algorithm>
#include <cstdio>
using namespace orcsdr;
bool visible(const char* value) {
  return std::find(M5.Display.strings.begin(),M5.Display.strings.end(),value)!=M5.Display.strings.end();
}
void enter(screens::Id id) {screens::begin_transition(id,millis());screens::finish_transition();}
int main() {
  using P=maps::downloads::Phase;
  maps::downloads::Snapshot s;
  enter(screens::Id::home);
  map_download_notice::update(s,screens::Id::home,false);
  assert(!map_download_notice::hit(360,40,screens::Id::home,false));
  s.phase=P::downloading;s.busy=true;s.completed_bytes=25;s.total_bytes=100;
  map_download_notice::update(s,screens::Id::home,false);
  assert(visible("MAP DL")&&visible("25%"));
  assert(map_download_notice::hit(360,40,screens::Id::home,false));
  assert(!map_download_notice::hit(317,40,screens::Id::home,false));
  assert(!map_download_notice::hit(412,40,screens::Id::home,false));
  auto paints=M5.Display.strings.size();
  map_download_notice::update(s,screens::Id::home,false);
  assert(M5.Display.strings.size()==paints);
  const int calls=M5.Display.string_draw_calls;
  map_download_notice::invalidate();map_download_notice::update(s,screens::Id::home,false);
  assert(M5.Display.string_draw_calls==calls+2);
  assert(M5.Display.strings.size()==paints&&visible("MAP DL")&&visible("25%"));
  for(int percent=26;percent<=30;++percent) {
    s.completed_bytes=percent;map_download_notice::update(s,screens::Id::home,false);
    assert(M5.Display.screen_clears==0);
  }
  assert(visible("30%"));
  assert(focus_nav::count()==1);
  map_download_notice::update(s,screens::Id::home,true);paints=M5.Display.strings.size();
  s.completed_bytes=40;map_download_notice::update(s,screens::Id::home,true);
  assert(M5.Display.strings.size()==paints&&!map_download_notice::hit(360,40,screens::Id::home,true));
  map_download_notice::update(s,screens::Id::home,false);assert(visible("40%"));
  enter(screens::Id::ft8);map_download_notice::invalidate();
  s.phase=P::verifying;s.verified_bytes=50;s.verify_total_bytes=100;
  map_download_notice::update(s,screens::Id::ft8,false);assert(visible("MAP CK")&&visible("50%"));
  assert(!map_download_notice::hit(360,40,screens::Id::home,false));
  assert(map_download_notice::hit(360,40,screens::Id::ft8,false));
  for(auto phase:{P::complete,P::cancelled,P::failed}) {
    s.phase=phase;s.busy=false;map_download_notice::update(s,screens::Id::ft8,false);
    assert(visible(phase==P::complete?"READY":phase==P::cancelled?"PAUSED":"ERROR"));
  }
  assert(!visible("READY")&&!visible("PAUSED")&&visible("ERROR"));
  for(auto screen:{screens::Id::maps,screens::Id::settings,screens::Id::documentation,screens::Id::visualizer}) {
    enter(screen);paints=M5.Display.strings.size();map_download_notice::update(s,screen,false);
    assert(M5.Display.strings.size()==paints&&!map_download_notice::hit(360,40,screen,false));
  }
  enter(screens::Id::home);map_download_notice::update(s,screens::Id::home,false);
  map_download_notice::update(s,screens::Id::home,false,{1600000,true});
  assert(visible("Nooelec XTR")&&visible("1.600 MHz")&&visible("Out of range")&&visible("Use upconverter"));
  assert(!map_download_notice::hit(360,40,screens::Id::home,false));
  const int warning_paints=M5.Display.string_draw_calls;
  for(int tick=0;tick<10;++tick) map_download_notice::update(s,screens::Id::home,false,{1600000,true});
  assert(M5.Display.string_draw_calls==warning_paints&&M5.Display.screen_clears==0);
  map_download_notice::update(s,screens::Id::home,false,{});
  assert(visible("ERROR")&&!visible("Out of range"));
  enter(screens::Id::am);
  map_download_notice::update(s,screens::Id::am,false,{1600000,true});
  assert(visible("Out of range")&&M5.Display.screen_clears==0);
  enter(screens::Id::home);
  s.phase=P::idle;map_download_notice::update(s,screens::Id::home,false);
  assert(!map_download_notice::hit(360,40,screens::Id::home,false)&&M5.Display.screen_clears==0);
  std::puts("MAP_DOWNLOAD_NOTICE_PASS values, redraw bounds, ownership, blocked inputs and terminal states");
}
