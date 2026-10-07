#include "weather_noaa.hpp"
#include <cstdio>
#include <cstdlib>
[[noreturn]] static void fail(const char* e,int l){std::fprintf(stderr,"FAIL line=%d check=%s\n",l,e);std::exit(1);}
#define CHECK(x) do{if(!(x))fail(#x,__LINE__);}while(0)
int main(){using namespace orcsdr::weather; ScanAccumulator scan; scan.begin(1000); CHECK(scan.active()); CHECK(scan.next_frequency_hz()==162400000u); const float levels[]={-70,-65,-55,-30,-50,-60,-68}; for(size_t i=0;i<7;++i) CHECK(scan.record(i,noaa_channel_hz(i),levels[i],1000+static_cast<uint32_t>(i)*250)); CHECK(!scan.active()); auto result=scan.result(); CHECK(result.complete); CHECK(result.strongest_index==3); CHECK(result.strongest_frequency_hz==162475000u); CHECK(result.strongest_dbfs==-30.0f); CHECK(result.sample_count==7); CHECK(result.completed_uptime_ms==2500u); CHECK(!scan.record(0,162400000u,-10,3000)); scan.begin(5000); CHECK(!scan.record(1,162400000u,-20,5001)); CHECK(scan.result().sample_count==0); std::puts("weather_noaa_tests: PASS");}
