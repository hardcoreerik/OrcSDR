#include "weather_service.hpp"
#include <cstdio>
#include <cstdlib>

[[noreturn]] static void fail(const char* e, int l){ std::fprintf(stderr,"FAIL line=%d check=%s\n",l,e); std::exit(1); }
#define CHECK(x) do{ if(!(x)) fail(#x,__LINE__); }while(false)

int main(){
  using namespace orcsdr::weather;
  Service service;
  CHECK(service.state().rf_state == RfState::idle);
  CHECK(service.state().selected_channel == 0);
  service.enter();
  CHECK(service.state().rf_state == RfState::idle);
  CHECK(service.select_channel(6));
  CHECK(service.selected_frequency_hz() == 162550000u);
  CHECK(!service.select_channel(7));
  service.begin_listen();
  CHECK(service.state().rf_state == RfState::listening);
  CHECK(service.state().active_frequency_hz == 162550000u);
  service.stop_rf();
  CHECK(service.state().rf_state == RfState::idle);
  service.begin_scan(1000);
  CHECK(service.state().rf_state == RfState::scanning);
  CHECK(service.scan_frequency_hz() == 162400000u);
  for(size_t i=0;i<noaa::kChannelCount;++i){
    service.record_scan_sample(-80.0f + static_cast<float>(i), 1000 + static_cast<uint32_t>(i)*100);
    if(i+1 < noaa::kChannelCount) CHECK(service.advance_scan());
    else CHECK(!service.advance_scan());
  }
  CHECK(service.state().rf_state == RfState::idle);
  CHECK(service.state().scan_complete);
  CHECK(service.state().strongest_channel == 6);
  CHECK(service.state().strongest_frequency_hz == 162550000u);
  CHECK(service.state().last_scan_uptime_ms == 1600u);
  CHECK(service.state().scan_samples == 7);
  service.cancel_scan();
  CHECK(service.state().rf_state == RfState::idle);
  std::puts("weather_service_tests: PASS");
}
