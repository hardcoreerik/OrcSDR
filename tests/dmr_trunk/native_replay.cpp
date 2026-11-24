// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr_trunk/d1_adapter.hpp"
#include "dv_channelizer.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <string>
using namespace orcsdr::dmr;
struct Context {trunk::D1Adapter adapter; uint32_t hz=0; unsigned grants=0,headers=0,continuations=0; bool bad=false;};
void sink(const d1::integration::Event& ev,void* ptr) noexcept {
 auto& c=*static_cast<Context*>(ptr);const auto& e=ev.decoded;
 if(e.control_payload_valid&&e.data_type==4)++c.headers;
 if(e.control_payload_valid&&e.data_type==5)++c.continuations;
 trunk::Grant g{};auto ms=uint32_t(ev.burst_start_sample/48);
 if(c.adapter.consume(ev,1,c.hz,ms,true,g)==trunk::Parse::grant){
   ++c.grants;
   std::cout<<"grant "<<g.rx_hz<<" TS"<<unsigned(g.slot)<<" CC"<<unsigned(g.cc)<<" LPCN "<<g.absolute_lpcn<<" TG "<<g.target<<" SRC "<<g.source<<"\n";
   if(!g.absolute||g.rx_hz!=(c.hz==152685000?152682500u:c.hz)||g.slot!=2||g.target!=6011885||
      (c.hz==152157500&&(g.cc!=13||g.absolute_lpcn!=173||(g.source!=4106011&&g.source!=4106019)))||
      (c.hz==152685000&&(g.cc!=8||g.absolute_lpcn!=215||g.source!=4106019)))c.bad=true;
 }
}
int main(int argc,char**argv){
 if(argc!=2)return 2;std::string root=argv[1];unsigned positive=0;
 const char* names[]={"terra_152p1575_call_cc13.cu8","terra_152p685_call_cc8.cu8","terra_153p068_call_cc8.cu8","terra_152p1575_control_only.cu8","capmax_451p9125_control_crcfail.cu8","negative_nxdn_voice_chan_idle_454p125.cu8"};
 uint32_t hz[]={152157500,152685000,153068000,152157500,451912500,454125000};
 for(unsigned i=0;i<6;i++){
  std::ifstream f(root+"/"+names[i],std::ios::binary);if(!f)return 3;
  orcsdr::dv::Channelizer channel;d1::integration::Receiver rx;Context ctx;ctx.hz=hz[i];
  std::array<uint8_t,8192> bytes{};std::array<orcsdr::dv::Iq48,821> iq{};std::array<d1::ComplexSample,821> samples{};
  while(f.read(reinterpret_cast<char*>(bytes.data()),bytes.size())||f.gcount()){
   auto n=f.gcount();if(n%2)return 4;
   auto count=channel.process_cu8(bytes.data(),size_t(n)/2,iq.data(),iq.size());
   for(size_t j=0;j<count;j++)samples[j]={iq[j].re,iq[j].im};
   auto r=rx.process(samples.data(),count,sink,&ctx);if(!r.ok||r.consumed!=count)return 5;
  }
  std::cout<<names[i]<<" headers="<<ctx.headers<<" continuation="<<ctx.continuations<<" grants="<<ctx.grants<<"\n";
  if(ctx.bad||(i>=2&&ctx.grants))return 6;
  if(i<2)positive+=ctx.grants;
 }
 if(!positive){std::cerr<<"No native grants passed strict CACH/CRC gate\n";return 7;}
}
