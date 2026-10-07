#include "weather_noaa.hpp"
#include <cstdio>
#include <cstdlib>

[[noreturn]] static void fail(const char* e, int l){ std::fprintf(stderr,"FAIL line=%d check=%s\n",l,e); std::exit(1);}
#define CHECK(x) do{ if(!(x)) fail(#x,__LINE__);}while(false)

int main(){
  using namespace orcsdr::weather::noaa;
  const uint32_t expected[kChannelCount] = {162400000u,162425000u,162450000u,162475000u,162500000u,162525000u,162550000u};
  for(size_t i=0;i<kChannelCount;++i){
    CHECK(channel_hz(i) == expected[i]);
    CHECK(channel_index(expected[i]) == static_cast<int>(i));
  }
  CHECK(channel_index(162410000u) == -1);
  CHECK(nearest_channel_hz(162414000u) == 162425000u);
  CHECK(next_channel_hz(162550000u) == 162400000u);
  CHECK(previous_channel_hz(162400000u) == 162550000u);

  ScanPlan scan{};
  scan.start();
  CHECK(scan.active());
  for(size_t i=0;i<kChannelCount;++i){
    CHECK(scan.current_frequency_hz() == expected[i]);
    scan.offer(-70.0f + static_cast<float>(i));
    scan.advance();
  }
  CHECK(!scan.active());
  CHECK(scan.complete());
  CHECK(scan.strongest_index() == 6);
  CHECK(scan.strongest_frequency_hz() == 162550000u);
  CHECK(scan.sample_count() == kChannelCount);
  std::puts("weather_noaa_tests: PASS");
}
