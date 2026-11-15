// Receive-only demodulated-bit ingest for host protocol proof/replay.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_digital_voice_rx/metadata.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
using namespace orcsdr::digital_voice_rx;
int main(int argc,char** argv) {
 if(argc!=2 || (std::strcmp(argv[1],"ysf")!=0 && std::strcmp(argv[1],"dstar")!=0)){
  std::fprintf(stderr,"usage: orcsdr_dv_rx_cli {ysf|dstar} < hard-bits-01.bin\n"
      "Binary stdin: one byte 0x00 or 0x01 per demodulated on-air bit.\n");return 2;}
 const bool fusion=std::strcmp(argv[1],"ysf")==0;
 ysf::Scanner y;dstar::Scanner d;
 std::uint64_t at=0,seen=0;unsigned events=0;
 for(int c; (c=std::getchar())!=EOF;){
  if(c>1){std::fprintf(stderr,"invalid hard bit at index %llu\n",static_cast<unsigned long long>(seen));return 3;}
  if(fusion){ysf::Fich f{};if(y.push(static_cast<std::uint8_t>(c),f,at)){
   std::printf("{\"schema\":\"orcsdr-dv-rx-v1\",\"protocol\":\"ysf\",\"proof\":\"crc16_fec_valid\",\"bit_offset\":%llu,\"frame_type\":%u,\"call_mode\":%u,\"data_type\":%u,\"fich_raw\":%u}\n",
    static_cast<unsigned long long>(at),f.frame_type,f.call_mode,f.data_type,f.raw);++events;}}
  else {dstar::Header h{};if(d.push(static_cast<std::uint8_t>(c),h,at)){
   std::printf("{\"schema\":\"orcsdr-dv-rx-v1\",\"protocol\":\"dstar\",\"proof\":\"fcs16_fec_valid\",\"bit_offset\":%llu,\"source\":\"%.8s\",\"destination\":\"%.8s\",\"rpt1\":\"%.8s\",\"rpt2\":\"%.8s\"}\n",
      static_cast<unsigned long long>(at),h.my.data(),h.ur.data(),h.rpt1.data(),h.rpt2.data());++events;}}
  ++seen;
 }
 std::fprintf(stderr,"processed_bits=%llu validated_metadata_events=%u\n",static_cast<unsigned long long>(seen),events);
 return 0;
}
