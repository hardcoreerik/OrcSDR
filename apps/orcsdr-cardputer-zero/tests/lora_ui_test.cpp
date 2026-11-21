#include "compact/lora_screen.hpp"
#include <cassert>
#include <iostream>
using namespace orc::zero;
int main(){
 LoraScreen s;s.render();auto before=s.frame.pixels;auto clears=s.frame.full_clears;
 s.input(Key::gain,0);assert(s.sf==12);s.input(Key::gain,0);assert(s.sf==7);
 s.input(Key::frequency,0);for(char c:std::string("915.125"))s.input(c=='.'?Key::point:Key::digit,c);
 s.input(Key::enter,0);assert(!s.tuning.entry_active&&s.tuning.requested==915125000);
 s.input(Key::enter,0);assert(s.manual);
 orcsdr::lora_native::Packet p;p.sender=0x1234;std::snprintf(p.text,sizeof(p.text),"HELLO");
 for(int n=0;n<25;++n)s.add(p);assert(s.lines.size()==32);
 s.render();assert(s.frame.pixels!=before&&s.frame.full_clears==clears);
 assert(!s.input(Key::escape,0));
 std::cout<<"LoRa compact controls, frequency entry, bounded history/redraw PASS\n";
}
