#include "lora_native_decoder.hpp"
#include "lora_preamble_detector.hpp"
#include "lora_linux/fft.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>
#include "rtl_receiver.hpp"
#include <string>
int main(int argc,char** argv){
  for(int n:{8,128,2048,32768}){
    std::vector<float> data(n*2),w(n);constexpr double pi=3.14159265358979323846;
    for(int j=0;j<n;++j){data[j*2]=std::cos(2*pi*3*j/n);data[j*2+1]=std::sin(2*pi*3*j/n);}
    assert(dsps_gen_w_r2_fc32(w.data(),n)==0);
    assert(dsps_bit_rev_fc32_ansi(w.data(),n/2)==0);
    assert(dsps_fft2r_fc32_ansi_(data.data(),n,w.data())==0);
    assert(dsps_bit_rev_fc32_ansi(data.data(),n)==0);
    int peak=0;float power=0;
    for(int j=0;j<n;++j){float p=data[j*2]*data[j*2]+data[j*2+1]*data[j*2+1];if(p>power){power=p;peak=j;}}
    assert(peak==3&&std::abs(std::sqrt(power)-n)<n*.001);
  }
  assert(orcsdr::lora_native::initialize());
  assert(!orcsdr::lora_native::fft_table_in_psram());
  assert(orcsdr::lora_native::self_check());
  assert(orcsdr::lora_preamble::configure(11));
  std::vector<uint8_t> silence(8192,128);
  for(int n=0;n<64;++n)assert(!orcsdr::lora_preamble::feed(silence.data(),silence.size()).fired);
  assert(orcsdr::lora_preamble::status().windows>0);
  orcsdr::lora_preamble::reset();
  constexpr double rate=960000,bw=250000,symbol=2048/bw,pi=3.14159265358979323846;
  bool fired=false;uint64_t sample=0;
  for(int block=0;block<40;++block){
    for(size_t j=0;j<silence.size()/2;++j,++sample){
      const double t=std::fmod(sample/rate,symbol);
      const double phase=2*pi*(-bw*.5*t+bw/(2*symbol)*t*t);
      silence[j*2]=uint8_t(std::lround(127.5+100*std::cos(phase)));
      silence[j*2+1]=uint8_t(std::lround(127.5+100*std::sin(phase)));
    }
    fired|=orcsdr::lora_preamble::feed(silence.data(),silence.size()).fired;
  }
  assert(fired);
  assert(!orcsdr::lora_preamble::configure(6));
  if(argc==2&&std::string(argv[1])=="--device"){
    orc::linux_sdr::RtlReceiver radio;radio.open(0);radio.configure(906875000,960000);
    std::vector<uint8_t> capture(960000*2);
    size_t used=0;
    while(used<capture.size()){
      const auto bytes=radio.read_cu8(capture.data()+used,std::min<size_t>(8192,capture.size()-used));
      orcsdr::lora_preamble::feed(capture.data()+used,bytes);used+=bytes;
    }
    std::vector<orcsdr::lora_native::Packet> packets(orcsdr::lora_native::kMaxPacketsPerCapture);
    orcsdr::lora_native::Stats stats;orcsdr::lora_native::Config config;
    const auto found=orcsdr::lora_native::decode_capture_in_place(capture.data(),capture.size(),960000,11,250000,906875000,config,packets.data(),packets.size(),&stats);
    assert(!stats.workspace_failed);
    std::cout<<"LORA_DEVICE RF "<<radio.frequency_hz()<<" RATE "<<radio.sample_rate_sps()
      <<" SAMPLES "<<used/2<<" WINDOWS "<<orcsdr::lora_preamble::status().windows
      <<" PACKETS "<<found<<" CRC_OK "<<stats.crc_ok<<" DECODE_MS "<<stats.decode_millis<<"\n";
  }
  std::cout<<"LoRa Linux FFT, native protocol/AES self-check, preamble silence PASS\n";
}
