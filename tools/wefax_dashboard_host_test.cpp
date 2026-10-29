#include <M5Unified.h>
#include "wefax_dashboard.hpp"
#include <cassert>
#include <cstdio>
FakeM5 M5;
int main(){
  using namespace orcsdr::wefax_dashboard;
  Snapshot snapshot;
  enter(snapshot);
  assert(active());
  assert(M5.Display.saw_waiting&&M5.Display.saw_no_receiver);
  assert(M5.Display.full_clears==0);
  assert(touch(10,10).kind==ActionKind::none);
  const int calls=M5.Display.partial_draws;
  update(snapshot);
  assert(M5.Display.partial_draws==calls);
  snapshot.verified_utc_epoch=1793232000ull;
  update(snapshot);
  assert(M5.Display.full_clears==0);
  snapshot.receiver_ready=true;
  update(snapshot);
  auto t=touch(240,640);
  assert(t.kind==ActionKind::none || t.kind==ActionKind::tune_usb);
  t=touch(300,650);
  assert(t.kind==ActionKind::tune_usb && t.dial_hz==8680100);
  touch(820,125);
  t=touch(300,650);
  assert(t.kind==ActionKind::tune_usb && t.dial_hz==4233100);
  leave();
  M5.Display.w=720;M5.Display.h=720;
  enter(snapshot);
  assert(M5.Display.full_clears==0);
  auto sq=touch(300,340);
  assert(sq.kind==ActionKind::tune_usb);
  update(snapshot);
  assert(M5.Display.full_clears==0);
  leave();
  std::puts("WEFAX_DASHBOARD_PASS: layout 1280/720, offline state, event-driven redraw, tune action");
}
