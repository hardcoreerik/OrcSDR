// Offline WAV replay against the OrcSDR WEFAX C++ core.
// 16-bit little-endian mono PCM RIFF/WAVE at 12000 or 48000 samples/s only.
#include "wefax_core.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
uint16_t u16(const uint8_t* p) {return static_cast<uint16_t>(p[0] | uint16_t(p[1])<<8);}
uint32_t u32(const uint8_t* p) {return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;}
struct Audio {uint32_t rate=0; std::vector<int16_t> samples;};
Audio read_wav(const char* path) {
  std::ifstream in(path,std::ios::binary);
  uint8_t head[12]{};
  if (!in.read(reinterpret_cast<char*>(head),12) || std::memcmp(head,"RIFF",4) || std::memcmp(head+8,"WAVE",4))
    throw std::runtime_error("Expected RIFF/WAVE");
  Audio a;bool fmt=false, data=false;
  while (in && !data) {
    uint8_t chunk[8]; if(!in.read(reinterpret_cast<char*>(chunk),8)) break;
    auto n=u32(chunk+4);
    if(n>130000000) throw std::runtime_error("WAV chunk exceeds safety limit");
    if(!std::memcmp(chunk,"fmt ",4)) {
      if(n<16 || n>256) throw std::runtime_error("Bad PCM format chunk size");
      std::vector<uint8_t> v(n);
      if(!in.read(reinterpret_cast<char*>(v.data()),n)) throw std::runtime_error("Short format chunk");
      a.rate=u32(v.data()+4);
      if(u16(v.data())!=1 || u16(v.data()+2)!=1 || u16(v.data()+14)!=16 ||
         (a.rate!=12000 && a.rate!=48000) || u32(v.data()+8)!=a.rate*2 || u16(v.data()+12)!=2)
        throw std::runtime_error("Expected mono 16-bit PCM at 12 or 48 kHz");
      fmt=true;
    } else if (!std::memcmp(chunk,"data",4)) {
      if (!fmt || n%2 || n>60*48000*2) throw std::runtime_error("Unsupported WAV data length or ordering");
      std::vector<uint8_t> v(n);
      if (!in.read(reinterpret_cast<char*>(v.data()),n)) throw std::runtime_error("Truncated WAV data");
      a.samples.resize(n/2);
      for(size_t i=0;i<a.samples.size();++i) a.samples[i]=static_cast<int16_t>(u16(&v[2*i]));
      data=true;
    } else {
      in.seekg(n+(n&1u),std::ios::cur);
    }
    if(n&1u && !data) in.seekg(1,std::ios::cur);
  }
  if(!data || !fmt) throw std::runtime_error("WAV is missing fmt or data");
  return a;
}
struct Frame {
 std::vector<uint8_t> pixels;uint16_t width=0;uint32_t rows=0;
 static void accept(void* p,uint32_t row,const uint8_t* data,uint16_t width) {
  auto& f=*static_cast<Frame*>(p);
  if(row!=f.rows || (f.width && f.width!=width)) throw std::runtime_error("Row index/width mismatch");
  f.width=width;++f.rows;f.pixels.insert(f.pixels.end(),data,data+width);
 }
};
void pgm(const char* path,const Frame& f){
 std::ofstream out(path,std::ios::binary);
 if(!out) throw std::runtime_error("Cannot open output image");
 out<<"P5\n"<<f.width<<" "<<f.rows<<"\n255\n";
 out.write(reinterpret_cast<const char*>(f.pixels.data()),f.pixels.size());
 if(!out) throw std::runtime_error("Image write failed");
}
} // namespace
int main(int argc,char** argv) {
 if(argc<4) {
  std::fprintf(stderr,"Usage: %s input.wav output.pgm image_start_seconds [rows=10] [ioc=576] [lpm=120]\n",argv[0]);
  return 2;
 }
 try {
  const double seek=std::stod(argv[3]);
  const int rows=argc>4?std::stoi(argv[4]):10;
  const int ioc=argc>5?std::stoi(argv[5]):576;
  const int lpm=argc>6?std::stoi(argv[6]):120;
  if(!(seek>=0 && seek<=60) || rows<1 || rows>2000) throw std::runtime_error("Invalid start offset or row count");
  orcsdr::wefax::Format cfg;
  cfg.ioc=static_cast<uint16_t>(ioc);cfg.lpm=static_cast<uint16_t>(lpm);cfg.max_rows=static_cast<uint16_t>(rows);
  if(!orcsdr::wefax::valid_format(cfg)) throw std::runtime_error("Unsupported IOC/LPM");
  Audio a=read_wav(argv[1]);
  const size_t offset=static_cast<size_t>(seek*a.rate + 0.5);
  if(offset>=a.samples.size()) throw std::runtime_error("Start offset beyond WAV duration");
  Frame frame;orcsdr::wefax::Decoder decoder(cfg);
  decoder.set_row_sink(Frame::accept,&frame);
  decoder.start_manual();
  const size_t block=4096;
  for(size_t pos=offset;pos<a.samples.size() && decoder.metrics().receiving;pos+=block){
   const size_t n=std::min(block,a.samples.size()-pos);
   if(a.rate==12000) decoder.process_pcm_12k(a.samples.data()+pos,n);
   else decoder.process_pcm_48k(a.samples.data()+pos,n);
  }
  if(frame.rows==0) throw std::runtime_error("No complete rows decoded; not a valid result");
  pgm(argv[2],frame);
  std::printf("WEFAX_REPLAY result=%s rate=%u start_s=%.4f rows=%u/%d width=%u bytes=%zu tone_hz=%.2f\n",
      frame.rows==static_cast<uint32_t>(rows)?"COMPLETE":"PARTIAL",a.rate,seek,frame.rows,rows,frame.width,
      frame.pixels.size(),decoder.metrics().last_tone_hz);
  return frame.rows==static_cast<uint32_t>(rows)?0:3;
 } catch(const std::exception& e){std::fprintf(stderr,"WEFAX_REPLAY ERROR: %s\n",e.what());return 1;}
}
