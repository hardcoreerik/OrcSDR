// SPDX-License-Identifier: AGPL-3.0-only
// Host-only test client; feeds raw CU8 or signed16LE interleaved I/Q to native
// allocation-free receiver. No RF device required. Writes optional raw AVLC hex.
// Usage: vdl2_replay_iq <input> <format:i16|cu8> <rate> [offset_hz] [--conjugate]
#include "vdl2_channel.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
using namespace orcsdr::vdl2;
namespace {
struct Count {unsigned frames=0,acars=0;};
void on_frame(void* context,const FrameView& f){
  auto* count=static_cast<Count*>(context);
  ++count->frames;
  std::printf("AVLC %u src=%07X dst=%07X ctl=%02X len=%zu raw=",
              count->frames,f.source,f.destination,f.control,f.raw_size);
  for(size_t i=0;i<f.raw_size;++i)std::printf("%02X",f.raw[i]);
  std::putchar('\n');
  if(f.is_acars && f.acars_message){
    ++count->acars;
    const auto& m=*f.acars_message;
    std::printf("ACARS tail=%.*s label=%.2s block=%c text=",
      7,m.tail,m.label,m.block_id);
    for(size_t i=0;i<m.text_length;++i){
      const unsigned char ch=static_cast<unsigned char>(m.text[i]);
      if(ch>=32 && ch<=126)std::putchar(int(ch));
      else std::printf("<%02X>",unsigned(ch));
    }
    std::putchar('\n');
  }
}
}
int main(int argc,char** argv){
 if(argc<4||argc>6){std::fprintf(stderr,"Usage: %s <file.i16|.cu8> <i16|cu8> <rate_sps> [offset_hz] [--conjugate]\n",argv[0]);return 2;}
 const bool i16=std::strcmp(argv[2],"i16")==0;
 if(!i16 && std::strcmp(argv[2],"cu8")!=0){std::fprintf(stderr,"Unsupported format\n");return 2;}
 char* end=nullptr;
 const unsigned long rate=std::strtoul(argv[3],&end,10);
 if(!end||*end!='\0'||rate>1050000 || rate<50000){std::fprintf(stderr,"Invalid rate\n");return 2;}
 int32_t offset=0;bool conj=false;
 for(int j=4;j<argc;++j){
   if(std::strcmp(argv[j],"--conjugate")==0)conj=true;
   else {
     char* stop=nullptr;const long v=std::strtol(argv[j],&stop,10);
     if(!stop||*stop!='\0'||v<-1000000||v>1000000){std::fprintf(stderr,"Invalid offset\n");return 2;}
     offset=int32_t(v);
   }
 }
 const uint32_t target=(rate==250000||rate==50000)?50000u:105000u;
 Channel rx;
 if(!rx.configure(uint32_t(rate),target,offset)){
   std::fprintf(stderr,"Unsupported input/output rate pair; allowed 50k/105k direct, 250k->50k or 1.05M->105k\n");return 2;
 }
 Count count{};rx.receiver().set_callback(on_frame,&count);
 std::FILE* f=std::fopen(argv[1],"rb");
 if(!f){std::perror("fopen");return 2;}
 constexpr size_t kSamples=4096;
 std::array<uint8_t,kSamples*4> in{};
 std::array<ComplexSample,kSamples> unpacked{};
 const size_t stride=i16?4:2;
 size_t samples=0;
 while(true){
   const size_t read=std::fread(in.data(),1,kSamples*stride,f);
   if(read%stride){std::fprintf(stderr,"Incomplete I/Q sample at file tail\n");std::fclose(f);return 2;}
   for(size_t j=0;j<read/stride;++j){
     float I=0,Q=0;
     if(i16){
       const uint8_t* p=&in[j*4];
       const int16_t vi=static_cast<int16_t>(uint16_t(p[0])|(uint16_t(p[1])<<8));
       const int16_t vq=static_cast<int16_t>(uint16_t(p[2])|(uint16_t(p[3])<<8));
       I=float(vi)/32768.f;Q=float(vq)/32768.f;
     } else {I=(float(in[j*2])-127.5f)/127.5f;Q=(float(in[j*2+1])-127.5f)/127.5f;}
     unpacked[j]={I,conj?-Q:Q};
   }
   rx.process_complex(unpacked.data(),read/stride);
   samples+=read/stride;
   if(read!=kSamples*stride){if(std::ferror(f)){std::perror("fread");std::fclose(f);return 2;}break;}
 }
 std::fclose(f);
 const auto& s=rx.receiver().stats();
 const auto& fs=rx.receiver().frames_stats();
 std::printf("VDL2_REPORT samples=%zu rate=%lu channel_rate=%u offset=%d duration=%.3f "
  "uw=%u lock=%u header_ok=%u header_fail=%u fec_ok=%u fec_fail=%u "
  "avlc_crc_ok=%u avlc_crc_bad=%u acars=%u frames=%u last_cfo=%.2f\n",
  samples,rate,target,offset,double(samples)/rate,s.uw_candidates,s.uw_locked,
  s.header_ok,s.header_fail,s.fec_ok,s.fec_fail,fs.frames_crc_ok,fs.frames_crc_bad,
  count.acars,count.frames,s.last_cfo_hz);
 return count.frames?0:1;
}
