#include "settings.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
int main(){
 const std::string path="/tmp/orc-zero-settings-"+std::to_string(getpid())+".conf";
 orc::zero::UserSettings input{};input.frequency_hz=105700000;input.volume=47;input.step_index=4;
 input.gain_auto=false;input.gain_tenth_db=90;input.audio_device="plughw:1,0";
 assert(orc::zero::save_settings(path,input));
 orc::zero::UserSettings output{};
 assert(orc::zero::load_settings(path,output));
 assert(output.frequency_hz==input.frequency_hz&&output.volume==47&&output.step_index==4&&
        !output.gain_auto&&output.gain_tenth_db==90&&output.audio_device=="plughw:1,0");
 {std::ofstream corrupt(path);corrupt<<"version=1\nfrequency_hz=999999999\n";}
 assert(!orc::zero::load_settings(path,output));
 assert(output.frequency_hz==input.frequency_hz);
 ::unlink(path.c_str());std::cout<<"versioned XDG settings roundtrip/corruption PASS\n";
}