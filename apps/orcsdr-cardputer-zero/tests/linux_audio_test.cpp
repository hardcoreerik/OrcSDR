#include "linux_audio.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>
int main(){
 orc::zero::AudioSink sink;
 assert(sink.open("null",true));assert(!sink.audible());
 std::array<int16_t,2048> input{};input.fill(12000);
 assert(sink.write_mono(input.data(),input.size(),0,false));
 const auto& at0=sink.diagnostic_output();
 assert(std::all_of(at0.begin(),at0.begin()+input.size(),[](auto v){return v==0;}));
 assert(sink.write_mono(input.data(),input.size(),40,false));
 const auto& at40=sink.diagnostic_output();
 assert(at40[2047]>4000 && at40[2047]<5000);
 assert(sink.write_mono(input.data(),input.size(),100,true));
 const auto& atMute=sink.diagnostic_output();
 assert(std::all_of(atMute.begin(),atMute.begin()+input.size(),[](auto v){return v==0;}));
 assert(sink.frames_written()==3*input.size());assert(sink.underruns()==0);
 sink.discard();sink.close();
 assert(!sink.opened());
 std::cout<<"null PCM test: zero-volume silence, gain bound, immediate mute, frames=6144 PASS\n";
}