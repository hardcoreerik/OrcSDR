#include "compact/p25_screen.hpp"
#include "p25_voice.hpp"
#include <cassert>
#include <iostream>
using namespace orc::zero;
int main(){
  assert(orcsdr::p25core::self_check());
  assert(orcsdr::p25voice::Decoder::self_check());
  P25Screen p;p.applied=p.requested;p.render();const auto clears=p.frame.full_clears;
  const auto first=p.frame.pixels;
  p.input(Key::volume_up,0);p.render();
  assert(p.frame.pixels!=first&&p.frame.full_clears==clears);
  p.input(Key::frequency,0);
  for(char ch:std::string("460.125"))p.input(ch=='.'?Key::point:Key::digit,ch);
  p.input(Key::enter,0);assert(p.requested==460125000&&!p.entry_active);
  p.input(Key::frequency,0);p.input(Key::digit,'1');p.input(Key::enter,0);
  assert(p.entry_active&&p.requested==460125000&&!p.error.empty());
  p.input(Key::escape,0);assert(!p.entry_active);
  p.requested=24000000;p.input(Key::left,0);assert(p.requested==24000000);
  p.requested=1766000000;p.input(Key::right,0);assert(p.requested==1766000000);
  assert(!p.input(Key::escape,0));
  for(int n=0;n<3;++n)p.input(Key::gain,0);
  assert(p.modulation==orcsdr::p25core::Modulation::auto_detect);
  p.state.frame_sync=true;++p.revision;p.render();assert(p.frame.full_clears==clears);
  std::cout<<"P25 shared decoder/voice, frequency validation, bounded redraw PASS\n";
}
