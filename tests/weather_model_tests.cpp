#include "weather_model.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

[[noreturn]] static void fail(const char* e, int l){ std::fprintf(stderr,"FAIL line=%d check=%s\n",l,e); std::exit(1);}
#define CHECK(x) do{ if(!(x)) fail(#x,__LINE__);}while(false)

int main(){
  using namespace orcsdr::weather;
  CHECK(std::strcmp(source_label(SourceKind::direct_rf), "RF") == 0);
  CHECK(std::strcmp(source_label(SourceKind::online), "ONLINE") == 0);

  Observation missing{};
  missing.kind = ValueKind::temperature_c;
  missing.valid = false;
  CHECK(classify_freshness(missing, 10) == Freshness::unavailable);

  Observation temp{};
  temp.kind = ValueKind::temperature_c;
  temp.valid = true;
  temp.value = 0.0f;
  temp.meta.source = SourceKind::local_sensor;
  temp.meta.observed_uptime_ms = 1000;
  CHECK(classify_freshness(temp, 60) == Freshness::live);
  CHECK(classify_freshness(temp, 600) == Freshness::recent);
  CHECK(classify_freshness(temp, 4000) == Freshness::stale);
  CHECK(classify_freshness(temp, 20000) == Freshness::expired);

  Observation samples[3]{};
  for(int i=0;i<3;++i){
    samples[i].kind = ValueKind::temperature_c;
    samples[i].valid = true;
    samples[i].value = 10.0f + static_cast<float>(i) * 2.0f;
    samples[i].meta.source = SourceKind::local_sensor;
    samples[i].meta.age_seconds = 10u + static_cast<unsigned>(i) * 5u;
  }
  Consensus c{};
  CHECK(consensus(samples, 3, ValueKind::temperature_c, 2, &c));
  CHECK(c.valid);
  CHECK(c.input_count == 3);
  CHECK(std::fabs(c.value - 12.0f) < 0.001f);
  CHECK(std::fabs(c.spread - 4.0f) < 0.001f);
  CHECK(c.oldest_age_seconds == 20);
  CHECK(c.source == SourceKind::local_derived);
  CHECK(!consensus(samples, 1, ValueKind::temperature_c, 2, &c));

  std::puts("weather_model_tests: PASS");
}
