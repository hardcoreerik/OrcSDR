#include "fm_4b_layout.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace orcsdr::waveshare4b::fm;
int main(){
 static_assert(valid(),"geometry");
 static_assert(kTabs==5,"all original FM tabs");
 static_assert(!kIqOnly.demod&&!kIqOnly.rds&&!kIqOnly.audio,"no fake decoder");
 assert(kSpectrumPlot.w==666&&kWaterfallPlot.w==666);
 assert(kBody.disjoint(kTabsBar));
 for(unsigned i=0;i<kTabs;++i){
  assert(strlen(kTabNames[i])>0&&strstr(kTabRefs[i],".png")!=nullptr);
  auto r=kTabsRect[i];assert(r.hit(r.x+10,r.y+12));
  for(unsigned j=0;j<kTabs;++j)if(i!=j)assert(!kTabsRect[j].hit(r.x+10,r.y+12));
 }
 Model m{};
 for(unsigned i=0;i<kTabs;++i){assert(m.select(i));assert(index(m.selected)==i);}
 assert(!m.select(5));assert(m.selected==Tab::settings);
 assert(!m.can_show_audio()&&!m.can_show_rds()&&!m.can_record());
 assert(!in_fm_band(75100000));assert(in_fm_band(76100000));
 assert(in_fm_band(108000000));assert(!in_fm_band(108100000));
 Features incoherent{true,false,false,true,false,false,false};
 assert(!incoherent.valid());
 std::puts("FM_4B_TABS PASS: 5 distinct tabs, geometry, hitboxes, capabilities");
}
