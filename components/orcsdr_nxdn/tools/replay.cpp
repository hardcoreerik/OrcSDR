// OrcSDR receive-only CS16 IQ → NXDN evidence JSONL replay.
 // SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_nxdn/decoder.hpp"
#include "orcsdr_nxdn/frontend.hpp"
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace orcsdr::nxdn;
namespace {
struct Run {
    Decoder* decoder=nullptr;
    std::uint64_t frames=0,validated=0,voice_candidates=0;
    bool emit_candidates=false;
};
void frame_cb(const FrameObservation& x,void* arg) noexcept {
    auto& r=*static_cast<Run*>(arg);++r.frames;
    unsigned v=0;
    for(unsigned i=0;i<4;++i) if(x.voice_mask & (1U<<i)) ++v;
    r.voice_candidates+=v;
    if(!r.emit_candidates) return;
    std::printf("{\"kind\":\"lich_candidate\",\"mode\":\"NXDN%u\",\"frame_start_dibit\":%llu,\"fsw_dibit_errors\":%u,\"rfct\":%u,\"fct\":%u,\"direction\":%u,\"voice_codewords_unverified\":%u}\n",
       x.mode==Mode::nxdn48?48:96,
       static_cast<unsigned long long>(x.frame_start_dibit),x.fsw_symbol_errors,
       x.rf_channel,x.functional_channel,x.direction,v);
}
void valid_cb(const Event& x,void* arg) noexcept {
    auto& r=*static_cast<Run*>(arg);++r.validated;
    std::printf("{\"kind\":\"sacch_crc_valid\",\"mode\":\"NXDN%u\",\"frame_start_dibit\":%llu,\"rfct\":%u,\"fct\":%u,\"direction\":%u,\"ran\":%u,\"structure\":%u,\"viterbi_distance\":%u}\n",
       x.mode==Mode::nxdn48?48:96,
       static_cast<unsigned long long>(x.frame_start_dibit),
       x.rf_channel,x.functional_channel,x.direction,x.ran,x.structure,x.sacch_viterbi_distance);
}
void symbol_cb(std::uint8_t d,void* arg) noexcept {
    auto& r=*static_cast<Run*>(arg);
    (void)r.decoder->feed(&d,1,valid_cb,&r,frame_cb,&r);
}
std::int16_t i16le(const unsigned char* p) noexcept {
    const std::uint16_t u=static_cast<std::uint16_t>(p[0]|(p[1]<<8U));
    // Avoid implementation-defined unsigned-to-signed narrowing.
    const std::int32_t n=u>=32768U?static_cast<std::int32_t>(u)-65536:static_cast<std::int32_t>(u);
    return static_cast<std::int16_t>(n);
}
}
int main(int argc,char** argv) {
    if(argc<3||argc>5||(std::strcmp(argv[1],"48")&&std::strcmp(argv[1],"96"))) {
        std::fprintf(stderr,"Usage: orcsdr_nxdn_replay 48|96 input.cs16|- [--frames] [--chunk=N]\n"
               "Input: signed 16-bit LE IQ at 48000 complex samples/s.\n");
        return 2;
    }
    bool frames=false;std::size_t chunk=4096;
    for(int i=3;i<argc;++i) {
        if(!std::strcmp(argv[i],"--frames")) frames=true;
        else if(!std::strncmp(argv[i],"--chunk=",8)) {
            char* end=nullptr;errno=0;
            const unsigned long v=std::strtoul(argv[i]+8,&end,10);
            if(errno||!end||*end||!v||v>4096) return 2;
            chunk=static_cast<std::size_t>(v);
        } else return 2;
    }
    std::FILE* in=std::strcmp(argv[2],"-")?std::fopen(argv[2],"rb"):stdin;
    if(!in) {std::perror(argv[2]);return 2;}
    const Mode mode=!std::strcmp(argv[1],"48")?Mode::nxdn48:Mode::nxdn96;
    Decoder decoder(mode,1);Frontend front(mode);
    Run run{&decoder,0,0,0,frames};
    std::array<unsigned char,4096*4> bytes{};
    std::array<IQSample,4096> samples{};
    std::uint64_t total=0;int result=0;
    while(true) {
        const std::size_t n=std::fread(bytes.data(),1,chunk*4,in);
        if(n==0) {if(std::ferror(in)){std::perror("read CS16");result=2;}break;}
        if(n%4) {
            std::fprintf(stderr,"Truncated CS16 sample at byte %llu\n",
                         static_cast<unsigned long long>(total*4+n));
            result=2;break;
        }
        const std::size_t count=n/4;
        for(std::size_t i=0;i<count;++i) {
            samples[i].re=static_cast<float>(i16le(bytes.data()+4*i))/32768.0F;
            samples[i].im=static_cast<float>(i16le(bytes.data()+4*i+2))/32768.0F;
        }
        if(!front.feed(samples.data(),count,symbol_cb,&run)){result=2;break;}
        total+=count;
    }
    if(in!=stdin) std::fclose(in);
    const auto c=decoder.counters();
    std::fprintf(stderr,"NXDN%u samples=%llu dibits=%llu fsw_exact=%llu fsw_near=%llu lich=%llu sacch_crc_valid=%llu sacch_rejected=%llu voice_codeword_candidates=%llu carrier_hz=%.1f\n",
      mode==Mode::nxdn48?48:96,static_cast<unsigned long long>(total),
      static_cast<unsigned long long>(front.symbols_seen()),
      static_cast<unsigned long long>(c.exact_sync_windows),
      static_cast<unsigned long long>(c.near_sync_windows),
      static_cast<unsigned long long>(c.lich_valid),
      static_cast<unsigned long long>(c.sacch_valid),
      static_cast<unsigned long long>(c.sacch_rejected),
      static_cast<unsigned long long>(c.voice_codeword_candidates),front.carrier_offset_hz());
    if(std::ferror(stdout)||std::ferror(stderr)) result=2;
    return result;
}
