// Robustness: the decoder is fed non-signal audio (noise, clipped noise, DC, silence, hopping tones) the way a
// live receiver feeds it between transmissions. It must never stall, and no single service call may be slow.
// Real RF exposed this class of input for the first time; synthetic tests only ever carried valid SSTV.
#include "sstv_receiver.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <algorithm>
using namespace orcsdr::sstv;
using clk=std::chrono::steady_clock;
static int run(const char* name,int kind,double amp,int seconds){
  stop(); clear_gallery(); set_mode(Mode::auto_vis); set_auto_strategy(AutoStrategy::vis_sync); start(145500000);
  std::mt19937 rng(1234); std::normal_distribution<double> n(0,1);
  std::vector<int16_t> buf(512); double last=0,ph=0; double worst=0,total=0; unsigned long samples=0;
  const unsigned long target=48000ul*seconds; double tone=1900;
  while(samples<target){
    for(auto&s:buf){
      double x=0;
      switch(kind){
        case 0:x=n(rng)*amp;break;                                 // white noise
        case 1:{double w=n(rng)*amp;x=0.9*w+0.1*last;last=x;break;}// post-filtered noise
        case 2:{double w=n(rng)*amp*4;x=std::max(-15000.0,std::min(15000.0,w));break;} // clipped
        case 3:x=amp;break;                                        // DC
        case 4:{if((samples&0x3fff)==0)tone=1000+rng()%1500;ph+=2*M_PI*tone/48000;x=std::sin(ph)*amp+n(rng)*amp*0.3;break;} // hopping tone + noise
      }
      s=static_cast<int16_t>(std::max(-32768.0,std::min(32767.0,x)));++samples;
    }
    offer_audio_48k(buf.data(),buf.size());
    auto t0=clk::now(); service(4000); double ms=std::chrono::duration<double,std::milli>(clk::now()-t0).count();
    worst=std::max(worst,ms); total+=ms;
    if(ms>250){std::printf("%s: HANG-LIKE service call %.0f ms\n",name,ms);return 1;}
  }
  auto m=metrics();
  std::printf("%-28s ok  worst_service=%.2f ms avg=%.3f ms/chunk  vis_bad=%lu gaps=%lu state=%s\n",name,worst,total/(samples/512.0),(unsigned long)m.bad_vis,(unsigned long)m.audio_gaps,state_name(m.state));
  return 0;
}
int main(){
  int bad=0;
  bad|=run("white amp 200",0,200,40); bad|=run("white amp 3000",0,3000,40); bad|=run("white amp 12000",0,12000,40);
  bad|=run("filtered amp 2000",1,2000,40); bad|=run("clipped",2,4000,40); bad|=run("dc 8000",3,8000,20); bad|=run("silence",3,0,20);
  bad|=run("hopping tone+noise",4,6000,60);
  std::puts(bad?"FUZZ: FAIL":"FUZZ: PASS");return bad;
}
