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
  if(!found_fmt||!found_data||format!=1||channels!=1||bits!=16||(rate!=48000&&rate!=12000)){
    std::fputs("Expected 48 kHz or 12 kHz, 16-bit, mono PCM WAV\n",stderr);
    std::fclose(fp);return 2;
  }
  orcsdr::sstv::start(145800000);
  std::array<int16_t,512> audio{};
  uint32_t highest_rows=0,decoded=0;
  const bool trace=std::getenv("SSTV_REPLAY_TRACE")!=nullptr;
  unsigned long long samples_seen=0,next_second=0;
  const size_t up=rate==12000?4:1;   // a 12 kHz decoder-rate capture: each sample four times, which the decoder averages back
  std::array<int16_t,512> wide{};
  while(data_left>=2){
    const size_t want=std::min<size_t>(audio.size()/up,data_left/2);
    const size_t got=std::fread(audio.data(),sizeof(int16_t),want,fp);
    if(!got)break;
    data_left-=static_cast<uint32_t>(got*2);
    size_t out=got;
    if(up==4){for(size_t k=0;k<got;++k)for(size_t j=0;j<4;++j)wide[k*4+j]=audio[k];out=got*4;}
    orcsdr::sstv::offer_audio_48k(up==4?wide.data():audio.data(),out);
    orcsdr::sstv::service(4000);
    highest_rows=std::max<uint32_t>(highest_rows,orcsdr::sstv::live_image().decoded_rows);
    // SSTV_REPLAY_TRACE=1: once per second of audio, what the decoder is doing (to see when and why it acquires).
    if(trace){
      samples_seen+=out;
      if(samples_seen/48000>next_second){
        next_second=samples_seen/48000;
        const auto tm=orcsdr::sstv::metrics();
        std::fprintf(stderr,"t=%2llus state=%-15s pulses=%4lu row=%3u rec=%d ident=%d score=%u locked=%u missed=%u\n",
          static_cast<unsigned long long>(next_second),orcsdr::sstv::state_name(tm.state),static_cast<unsigned long>(tm.observed_sync_pulses),
          tm.row,tm.receiving?1:0,static_cast<int>(tm.identification),static_cast<unsigned>(tm.identification_score),
          static_cast<unsigned>(tm.session_sync_locked),static_cast<unsigned>(tm.session_sync_missed));
      }
    }
    decoded=std::max<uint32_t>(decoded,orcsdr::sstv::metrics().row);
  }
  std::fclose(fp);
  {  // a real transmission is followed by silence, which lets the last samples flow through the decoder's filters
    std::array<int16_t,512> quiet{};
    for(unsigned k=0;k<24;++k){orcsdr::sstv::offer_audio_48k(quiet.data(),quiet.size());orcsdr::sstv::service(4000);}
  }
  for(unsigned n=0;n<4;++n)orcsdr::sstv::service(4000);
  // End of file is not end of image on air: close a final line only if its
  // pixel data fully arrived; nothing is padded to manufacture a full frame.
  orcsdr::sstv::end_of_stream();
  const auto stats=orcsdr::sstv::metrics();
  const auto completed=orcsdr::sstv::archived_image(0);
  std::printf("WAV=%s VIS=%u MODE=%s LINES=%u MAX_PROGRESS=%u FRAMES=%lu SYNC=%lu MISS=%lu OVERRUN=%lu"
    " PARTIAL=%d ID=%d END=%d SESSION_LOCKED=%u SESSION_MISSED=%u\n",
    argv[1],stats.vis,orcsdr::sstv::mode_name(
      completed.pixels?completed.mode:stats.received_mode),
    completed.pixels?completed.decoded_rows:stats.row,highest_rows,
    static_cast<unsigned long>(stats.frames),
    static_cast<unsigned long>(stats.sync_locked_lines),
    static_cast<unsigned long>(stats.sync_missed_lines),
    static_cast<unsigned long>(stats.audio_overruns),
    completed.pixels?(completed.partial?1:0):-1,static_cast<int>(completed.identification),
    static_cast<int>(completed.end_reason),
    static_cast<unsigned>(completed.sync_locked),static_cast<unsigned>(completed.sync_missed));
  // SSTV_REPLAY_PPM=<file> writes the decoded picture (colour) so a recording can be looked at, not just counted.
  if(const char* ppm=std::getenv("SSTV_REPLAY_PPM")) {
    if(completed.pixels) if(std::FILE* f=std::fopen(ppm,"wb")) {
      std::fprintf(f,"P6\n%u %u\n255\n",completed.width,completed.height);
      for(unsigned i=0;i<static_cast<unsigned>(completed.width)*completed.height;++i) {
        const uint16_t px=completed.pixels[i];
        std::fputc(((px>>11)&31)<<3|((px>>11)&31)>>2,f);
        std::fputc(((px>>5)&63)<<2|((px>>5)&63)>>4,f);
        std::fputc((px&31)<<3|(px&31)>>2,f);
      }
      std::fclose(f);
    }
  }
  if(std::getenv("SSTV_REPLAY_TRACE")){   // every image the decoder archived, newest first
    for(size_t k=0;k<orcsdr::sstv::archive_count();++k){
      const auto im=orcsdr::sstv::archived_image(k);
      std::fprintf(stderr,"archived[%zu]: rows=%u partial=%d locked=%u missed=%u end=%d start_unknown=%d\n",k,static_cast<unsigned>(im.decoded_rows),im.partial?1:0,static_cast<unsigned>(im.sync_locked),static_cast<unsigned>(im.sync_missed),static_cast<int>(im.end_reason),im.start_unknown?1:0);
    }
  }
  int ret=stats.audio_overruns==0?0:1;
  if(argc>=3&&stats.vis!=std::atoi(argv[2]))ret=1;
  if(argc>=4&&std::max<uint32_t>(decoded,completed.decoded_rows)<
     static_cast<uint32_t>(std::atoi(argv[3])))ret=1;
  orcsdr::sstv::stop();
  orcsdr::sstv::clear_gallery();
  return ret;
}
