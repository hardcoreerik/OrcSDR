#include "weather_model.hpp"
#include <cstdio>
#include <cstdlib>
[[noreturn]] static void fail(const char* e,int l){std::fprintf(stderr,"FAIL line=%d check=%s\n",l,e);std::exit(1);}
#define CHECK(x) do{if(!(x))fail(#x,__LINE__);}while(0)
int main(){using namespace orcsdr::weather; CHECK(static_cast<unsigned>(OnlinePolicy::disabled)==0); CHECK(default_online_policy()==OnlinePolicy::disabled); CHECK(kNoaaWeatherChannelCount==7); CHECK(noaa_channel_hz(0)==162400000u); CHECK(noaa_channel_hz(6)==162550000u); std::puts("weather_dashboard_state_tests: PASS");}
