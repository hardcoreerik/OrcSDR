#include "../apps/orcsdr-tab5/ui/sstv_dial_state.hpp"
#include <cassert>
#include <cstdio>
int main() {
  orc::Packet p;p.role=orc::Role::receiver;p.type=orc::Type::radio_state;p.dashboard=18;
  orcsdr::sstv::Metrics m;m.session_id=27;m.receiving=true;m.row=120;m.rows=240;m.received_mode=orcsdr::sstv::Mode::robot36;
  m.state=orcsdr::sstv::SessionState::receiving;m.frames=3;m.signal=true;
  orcsdr::sstv_archive::Stats a;a.saved=2;a.failed=1;a.pending=1;
  orcsdr::sstv::dial_state(p,m,a);
  uint8_t wire[64];orc::encode(p,wire);orc::Packet decoded;
  assert(orc::decode(wire,64,decoded));auto s=orc::sstv_control::decode(decoded);
  assert(s.valid && s.session==27 && s.row==120 && s.rows==240 && s.mode==1 && s.state==4);
  assert(s.complete==3 && s.saved==2 && s.failed==1 && s.pending==1);
  assert(s.flags&orc::sstv_control::kReceiving);
  p.capabilities=0;assert(!orc::sstv_control::decode(p).valid); // legacy receiver
  p=decoded; // no synthetic pixels or invented signal-quality score
  p.item_count=241|(240u<<16);assert(!orc::sstv_control::decode(p).valid);
  p=decoded;p.view=8;assert(!orc::sstv_control::decode(p).valid);
  p=decoded;p.action=15;assert(!orc::sstv_control::decode(p).valid);
  p=decoded;p.flags|=128;assert(!orc::sstv_control::decode(p).valid);
  p=decoded;p.squelch=-1;assert(!orc::sstv_control::decode(p).valid);
  p=decoded;p.role=orc::Role::dial;assert(!orc::sstv_control::decode(p).valid);
  std::puts("sstv_dial_state_test PASS");
}
