// Compare the original-sized and compact storage geometries on identical IQ.
#include "orcsdr_nxdn/frontend.hpp"
#include "dv_channelizer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>
using namespace orcsdr::nxdn;
static void sink(uint8_t d,void* p) noexcept {static_cast<std::vector<uint8_t>*>(p)->push_back(d);}
int main(int argc,char**argv){
  if(argc!=3)return 2;const std::string file=argv[1];const bool n48=std::string(argv[2])=="48";const auto mode=n48?Mode::nxdn48:Mode::nxdn96;
  std::vector<orcsdr::dv::Iq48> iq48;
  if(file=="noise"||file=="analog"){
    std::mt19937 gen(7721);std::normal_distribution<float> nd(0.f,.25f);double phase=0;
    for(unsigned i=0;i<48000*3;i++){if(file=="noise")iq48.push_back({nd(gen),nd(gen)});else{phase+=2*3.141592653589793*(500+1700*std::sin(i*.13))/48000;iq48.push_back({float(.5*std::cos(phase)),float(.5*std::sin(phase))});}}
  }else{
    std::ifstream f(file,std::ios::binary);if(!f)return 3;std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});if(b.empty())return 4;
    if(file.size()>=5&&file.substr(file.size()-5)==".cs16"){
      if(b.size()%4)return 5;
      auto sample=[&](size_t i){unsigned v=b[i]|unsigned(b[i+1])<<8;return (v>=32768?int(v)-65536:int(v))/32768.f;};
      for(size_t i=0;i<b.size();i+=4)iq48.push_back({sample(i),sample(i+2)});
    }else{
      if(b.size()%2)return 5;orcsdr::dv::Channelizer ch;std::vector<orcsdr::dv::Iq48> out(8192/5+2);
      for(size_t i=0;i<b.size();i+=16384){size_t n=std::min<size_t>(16384,b.size()-i);size_t k=ch.process_cu8(b.data()+i,n/2,out.data(),out.size());iq48.insert(iq48.end(),out.begin(),out.begin()+k);}if(ch.dropped_inputs())return 6;
    }
  }
  std::vector<IQSample> iq;
  if(n48){orcsdr::dv::Decimator2 d;std::vector<orcsdr::dv::Iq48> half(iq48.size()/2+2);auto n=d.process(iq48.data(),iq48.size(),half.data(),half.size());for(size_t i=0;i<n;i++)iq.push_back({half[i].re,half[i].im});}
  else for(auto x:iq48)iq.push_back({x.re,x.im});
  const double rate=n48?24000:48000;Frontend base(mode,rate);CompactFrontend small(mode,rate);
  std::vector<uint8_t> a,b;a.reserve(iq.size());b.reserve(iq.size());
  for(size_t chunk:{1u,37u,512u,4096u}){
    base.reset();small.reset();a.clear();b.clear();
    auto run=[&](auto& f,std::vector<uint8_t>& out){auto t=std::clock();for(size_t i=0;i<iq.size();i+=chunk)if(!f.feed(iq.data()+i,std::min(chunk,iq.size()-i),sink,&out))return -1.;return 1000.*(std::clock()-t)/CLOCKS_PER_SEC;};
    double ta=run(base,a),tb=run(small,b);if(ta<0||tb<0||a!=b||base.symbols_seen()!=small.symbols_seen()||base.carrier_offset_hz()!=small.carrier_offset_hz())return 7;
    std::printf("%s NXDN%s chunk=%zu dibits=%zu carrier=%.9f CPU_ms=%.3f/%.3f bytes=%zu/%zu identical\n",file.c_str(),argv[2],chunk,a.size(),base.carrier_offset_hz(),ta,tb,sizeof(Frontend),sizeof(CompactFrontend));
  }
  CompactFrontend invalid(Mode::nxdn48,48000);if(invalid.feed(iq.data(),1,sink,&b))return 8;
  Frontend general(Mode::nxdn48,48000);if(!general.feed(iq.data(),1,sink,&b))return 9;
}
