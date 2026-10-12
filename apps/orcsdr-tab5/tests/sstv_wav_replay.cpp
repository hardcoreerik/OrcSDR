// Host playback of real 48 kHz mono PCM16 WAV capture through production
// audio tap -> SSTV state machine -> progressive image buffer.
// Usage: sstv-wav-replay <recorded.wav> [expected-vis] [min-rows]
// The WAV must be demodulated receiver audio (not complex IQ).
#include "sstv_receiver.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
namespace {
uint16_t u16(const unsigned char* p) {return static_cast<uint16_t>(p[0]|(p[1]<<8));}
uint32_t u32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0])|(static_cast<uint32_t>(p[1])<<8)|
         (static_cast<uint32_t>(p[2])<<16)|(static_cast<uint32_t>(p[3])<<24);
}
}
int main(int argc,char** argv) {
  if(argc<2){std::fprintf(stderr,"usage: %s recording.wav [expected-vis] [min-rows]\n",argv[0]);return 2;}
  auto* fp=std::fopen(argv[1],"rb");
  if(!fp){std::perror(argv[1]);return 2;}
  unsigned char header[12];
  if(std::fread(header,1,12,fp)!=12 ||std::memcmp(header,"RIFF",4)!=0||
     std::memcmp(header+8,"WAVE",4)!=0){std::fputs("Invalid RIFF WAVE\n",stderr);return 2;}
  uint16_t format=0,channels=0,bits=0;
  uint32_t rate=0,data_left=0;
  bool found_fmt=false,found_data=false;
  while(!found_data) {
    unsigned char chunk[8];
    if(std::fread(chunk,1,8,fp)!=8)break;
    const uint32_t n=u32(chunk+4);
    if(std::memcmp(chunk,"fmt ",4)==0){
      unsigned char fmt[16];
      if(n<16||std::fread(fmt,1,16,fp)!=16)break;
      format=u16(fmt);channels=u16(fmt+2);rate=u32(fmt+4);bits=u16(fmt+14);
      if(n>16&&std::fseek(fp,n-16,SEEK_CUR)!=0)break;
      found_fmt=true;
    }else if(std::memcmp(chunk,"data",4)==0){
      data_left=n;found_data=true;
      break;
    } else if(std::fseek(fp,n+(n&1u),SEEK_CUR)!=0)break;
  }
  if(!found_fmt||!found_data||format!=1||channels!=1||bits!=16||rate!=48000){
    std::fputs("Expected 48 kHz, 16-bit, mono PCM WAV\n",stderr);
    std::fclose(fp);return 2;
  }
  orcsdr::sstv::start(145800000);
  std::array<int16_t,512> audio{};
  uint32_t highest_rows=0,decoded=0;
  while(data_left>=2){
    const size_t want=std::min<size_t>(audio.size(),data_left/2);
    const size_t got=std::fread(audio.data(),sizeof(int16_t),want,fp);
    if(!got)break;
    data_left-=static_cast<uint32_t>(got*2);
    orcsdr::sstv::offer_audio_48k(audio.data(),got);
    orcsdr::sstv::service(4000);
    highest_rows=std::max<uint32_t>(highest_rows,orcsdr::sstv::live_image().decoded_rows);
    decoded=std::max<uint32_t>(decoded,orcsdr::sstv::metrics().row);
  }
  std::fclose(fp);
  for(unsigned n=0;n<4;++n)orcsdr::sstv::service(4000);
  const auto stats=orcsdr::sstv::metrics();
  const auto completed=orcsdr::sstv::archived_image(0);
  std::printf("WAV=%s VIS=%u MODE=%s LINES=%u MAX_PROGRESS=%u FRAMES=%lu SYNC=%lu MISS=%lu OVERRUN=%lu\n",
    argv[1],stats.vis,orcsdr::sstv::mode_name(
      completed.pixels?completed.mode:stats.received_mode),
    completed.pixels?completed.decoded_rows:stats.row,highest_rows,
    static_cast<unsigned long>(stats.frames),
    static_cast<unsigned long>(stats.sync_locked_lines),
    static_cast<unsigned long>(stats.sync_missed_lines),
    static_cast<unsigned long>(stats.audio_overruns));
  int ret=stats.audio_overruns==0?0:1;
  if(argc>=3&&stats.vis!=std::atoi(argv[2]))ret=1;
  if(argc>=4&&std::max<uint32_t>(decoded,completed.decoded_rows)<
     static_cast<uint32_t>(std::atoi(argv[3])))ret=1;
  orcsdr::sstv::stop();
  orcsdr::sstv::clear_gallery();
  return ret;
}
