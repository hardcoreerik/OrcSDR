#include "compact_renderer.hpp"
#include "acars_renderer.hpp"
#include "adsb_renderer.hpp"
#include "airband.hpp"
#include <cassert>
#include <iostream>
template<class Pixels>void check(const Pixels& pixels){
 for(int y=0;y<28;++y)for(int x=0;x<28;++x)assert(pixels[(y+1)*320+x+1]==orc::zero::kOrcMascot28[y*28+x]);
}
int main(){
 orc::zero::CompactRenderer home;orc::zero::Snapshot state;home.render(state);check(home.pixels);
 auto clears=home.full_clears;state.volume=15;home.render(state);check(home.pixels);assert(home.full_clears==clears);
 orc::zero::acars_dashboard::Renderer ac;orc::zero::acars_dashboard::Snapshot a;ac.render(a,0);check(ac.pixels());
 a.counters.valid=1;++a.revision;ac.render(a,1);check(ac.pixels());
 a.detail=true;++a.revision;ac.render(a,2);check(ac.pixels());
 orc::zero::adsb::Renderer ads;orc::zero::adsb::Snapshot d;ads.render(d,0);check(ads.pixels());
 d.counters.crc_ok=1;++d.revision;ads.render(d,0);check(ads.pixels());
 orc::zero::airband::Renderer air;orc::zero::airband::Snapshot b;std::array<uint16_t,256> palette{};
 air.render(b,palette.data());check(air.framebuffer());b.selected_hz+=25000;air.render(b,palette.data());check(air.framebuffer());
 std::cout<<"Persistent 28px mascot on FM, ACARS/detail, ADS-B and Airband updates PASS\n";
}
