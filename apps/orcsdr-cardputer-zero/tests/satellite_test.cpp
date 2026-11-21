#include "compact/satellite_screen.hpp"
#include "iq_spectrum.hpp"
#include <cassert>
#include <iostream>
int main(){
 orc::zero::SatelliteScreen s;
 assert(s.tuning.requested==137500000);
 s.tuning.applied=s.tuning.requested;s.signal.connection=orc::zero::Connection::ready;
 orcnode_fft_t fft{};orcnode_fft_init(&fft);std::array<uint8_t,2048> iq{};iq.fill(128);
 orc::zero::spectrum_row(fft,iq.data(),s.signal.spectrum);
 s.signal.live_spectrum=true;s.signal.waterfall_rows=1;s.signal.waterfall[0]=s.signal.spectrum;
 s.render(0);const auto before=s.frame.pixels;const auto clears=s.frame.full_clears;
 s.input(orc::zero::Key::volume_up,0);s.render(1);
 assert(s.frame.pixels!=before&&s.frame.full_clears==clears);
 s.input(orc::zero::Key::frequency,0);
 for(char c:std::string("145.8"))s.input(c=='.'?orc::zero::Key::point:orc::zero::Key::digit,c);
 s.input(orc::zero::Key::enter,0);assert(s.tuning.requested==145800000);
 assert(!s.input(orc::zero::Key::escape,0));
 assert(s.frame.pixels[80*320+160]==s.frame.yellow);
 std::cout<<"Satellite receive controls, shared spectrum, bounded redraw PASS\n";
}
