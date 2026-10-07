#include "weather_model.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
[[noreturn]] static void fail(const char* expr, int line) { std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expr); std::exit(1); }
#define CHECK(x) do { if (!(x)) fail(#x, __LINE__); } while (0)
int main() {
  using namespace orcsdr::weather;
  CHECK(kNoaaWeatherChannelCount == 7);
  const uint32_t expected[] = {162400000u,162425000u,162450000u,162475000u,162500000u,162525000u,162550000u};
  for (size_t i=0;i<7;++i) CHECK(noaa_channel_hz(i)==expected[i]);
  CHECK(noaa_channel_index(162475000u)==3);
  CHECK(noaa_channel_index(162476000u)==kInvalidChannel);
  CHECK(nearest_noaa_channel(162476000u)==162475000u);
  CHECK(step_noaa_channel(162400000u,-1)==162550000u);
  CHECK(step_noaa_channel(162550000u,1)==162400000u);
  FreshnessPolicy policy{30,300,1800};
  CHECK(classify_freshness(false,0,policy)==Freshness::unavailable);
  CHECK(classify_freshness(true,30,policy)==Freshness::live);
  CHECK(classify_freshness(true,31,policy)==Freshness::recent);
  CHECK(classify_freshness(true,301,policy)==Freshness::stale);
  CHECK(classify_freshness(true,1801,policy)==Freshness::expired);
  Observation o{}; CHECK(!o.valid); o.valid=true; o.meta.source=SourceKind::direct_rf;
  CHECK(std::strcmp(source_label(SourceKind::direct_rf),"RF")==0);
  CHECK(std::strcmp(source_label(SourceKind::local_sensor),"LOCAL")==0);
  CHECK(std::strcmp(source_label(SourceKind::cache),"CACHE")==0);
  CHECK(std::strcmp(source_label(SourceKind::online),"ONLINE")==0);
  CHECK(default_online_policy()==OnlinePolicy::disabled);
  CHECK(valid_online_policy(0) && valid_online_policy(1) && valid_online_policy(2) && !valid_online_policy(7));
  char age[24]{}; format_age(age,sizeof(age),42); CHECK(std::strcmp(age,"42s ago")==0); format_age(age,sizeof(age),125); CHECK(std::strcmp(age,"2m ago")==0); format_age(age,sizeof(age),7200); CHECK(std::strcmp(age,"2h ago")==0);
  std::puts("weather_model_tests: PASS"); return 0;
}
