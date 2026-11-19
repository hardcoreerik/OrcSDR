#include "compact_renderer.hpp"
#include "settings.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace orc::zero;
int main(){
 ControlModel m;m.handle(Key::dashboards);assert(m.s.page==Page::dashboards);
 m.handle(Key::enter);assert(m.s.page==Page::receive);m.hardware_ready(99100000,2560000,{},"RTL");m.s.live_spectrum=true;
 static_assert(CompactRenderer::spectrum.h+CompactRenderer::waterfall.h>=120);
 m.s.spectrum.fill(128);m.s.waterfall[0].fill(128);m.s.waterfall_rows=1;
 CompactRenderer r;r.render(m.s);auto first=r.pixels;auto clears=r.full_clears;
 for(int y=0;y<28;++y)for(int x=0;x<28;++x)
   assert(r.pixels[(1+y)*320+168+x]==kOrcBadge[y*2*kOrcBadgeSize+x*2]);
 assert(r.pixels[80*320+160]==CompactRenderer::yellow);
 assert(r.pixels[70*320+143]==CompactRenderer::cyan);
 r.render(m.s);assert(r.pixels==first&&r.full_clears==clears);
 m.handle(Key::palette);assert(m.s.palette==1&&m.dirty_settings);
 r.render(m.s);assert(r.pixels!=first&&r.full_clears==clears);
 m.s.applied_hz+=100000;m.s.volume=5;r.render(m.s);assert(r.full_clears==clears);
 for(unsigned i=1;i<orcsdr::waterfall_style::kPaletteCount;++i)m.handle(Key::palette);
 assert(m.s.palette==0);
 m.handle(Key::frequency);m.handle(Key::palette);assert(m.s.palette==0);
 const auto path="/tmp/orcsdr-palette-"+std::to_string(getpid())+".conf";
 UserSettings s;s.palette=3;assert(save_settings(path,s));UserSettings read;
 assert(load_settings(path,read)&&read.palette==3);
 {std::ofstream out(path);out<<"version=1\nfrequency_hz=99100000\n";}
 assert(load_settings(path,read)&&read.palette==0);
 {std::ofstream out(path);out<<"version=1\npalette=99\n";}
 assert(!load_settings(path,read));unlink(path.c_str());
 m.s.page=Page::dashboards;
 m.s.dashboard_selection=4;
 r.render(m.s);
 assert(r.pixels[117*320+42]==0x1264);
 m.s.page=Page::settings;
 r.render(m.s);
 const auto settings_pixels=r.pixels;
 const auto settings_clears=r.full_clears;
 m.handle(Key::volume_up);
 r.render(m.s);
 assert(r.pixels!=settings_pixels&&r.full_clears==settings_clears);
 std::cout<<"home layout, bounded redraw, palette cycle/persistence PASS\n";
}
