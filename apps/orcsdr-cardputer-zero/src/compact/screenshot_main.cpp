#include "compact_renderer.hpp"
#include <cmath>
#include <iostream>
int main(int argc,char**argv){if(argc<3)return 2;orc::zero::ControlModel m;auto& s=m.s;s.simulated=true;s.connection=orc::zero::Connection::ready;
s.applied_hz=101700000;s.requested_hz=s.applied_hz;s.applied_rate=2560000;s.audio_ready=true;s.receiver_name="Realtek RTL2838";
for(int i=0;i<256;++i){double x=(i-150)/30.;double p=210*std::exp(-x*x*2.3)+15+13*std::sin(i*.24);s.spectrum[i]=uint8_t(std::clamp(p,0.,255.));}
for(int j=0;j<36;++j)for(int i=0;i<256;++i)s.waterfall[j][i]=uint8_t(std::clamp<int>(s.spectrum[i]+(j*31+i*3)%25-12,0,255));s.waterfall_rows=36;
std::string mode=argv[1];if(mode=="weak"){s.spectrum.fill(5);for(auto& row:s.waterfall)row.fill(4);}
else if(mode=="disconnected"){s.simulated=false;s.connection=orc::zero::Connection::disconnected;s.live_spectrum=false;}
else if(mode=="entry"){s.page=orc::zero::Page::entry;s.entry="103.9";}
else if(mode=="menu")s.page=orc::zero::Page::menu;
else if(mode=="gain"){s.page=orc::zero::Page::gain;s.gain_auto=false;s.gain_tenth_db=90;}
else if(mode=="audio")s.page=orc::zero::Page::audio;
else if(mode=="muted"){s.page=orc::zero::Page::audio;s.muted=true;}
else if(mode=="noaudio"){s.page=orc::zero::Page::audio;s.audio_ready=false;}
else if(mode=="help")s.page=orc::zero::Page::help;
else if(mode=="long"){s.page=orc::zero::Page::error;s.error="RTL-SDR USB PERMISSION DENIED";s.requested_hz=108000000;s.applied_hz=108000000;}
orc::zero::CompactRenderer r;r.render(s);if(!r.save_ppm(argv[2]))return 3;std::cout<<"rendered "<<mode<<" 320x170\n";}