#include "control_model.hpp"
#include <cassert>
#include <iostream>
using namespace orc::zero;
int main(){
 ControlModel m;assert(m.s.applied_hz==0);m.hardware_ready(96100000,2560000,{-50,0,90,120},"RTL2832");
 m.handle(Key::right);assert(m.s.requested_hz==96200000&&m.s.applied_hz==96100000);
 m.tune_failed("driver rejected");assert(m.s.requested_hz==96100000&&m.s.page==Page::error);
 m.handle(Key::escape);m.handle(Key::frequency);for(char c:std::string("101.7"))m.handle(c=='.'?Key::point:Key::digit,c);
 m.handle(Key::escape);assert(m.s.applied_hz==96100000&&m.s.requested_hz==96100000);
 m.handle(Key::frequency);for(char c:std::string("101.7"))m.handle(c=='.'?Key::point:Key::digit,c);
 m.handle(Key::enter);assert(m.s.requested_hz==101700000&&m.s.applied_hz==96100000);
 m.tune_applied(101700000);assert(m.s.applied_hz==101700000);
 m.handle(Key::step);assert(m.s.step_hz==200000);m.handle(Key::gain);m.handle(Key::enter);m.handle(Key::right);
 assert(!m.s.gain_auto&&m.s.gain_tenth_db==90);
 m.handle(Key::mute);assert(m.s.muted);m.handle(Key::volume_down);assert(m.s.volume==25);
 m.disconnected("unplugged");assert(!m.s.live_spectrum&&m.s.connection==Connection::error);
 m.handle(Key::quit);assert(m.exit_requested);
 std::cout<<"control/state scenarios PASS\n";
}