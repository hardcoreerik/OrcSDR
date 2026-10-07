#include "weather_report_format.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

[[noreturn]] static void fail(const char* e, int l){ std::fprintf(stderr,"FAIL line=%d check=%s\n",l,e); std::exit(1);}
#define CHECK(x) do{ if(!(x)) fail(#x,__LINE__);}while(false)

int main(){
  using namespace orcsdr::weather;
  Observation o{};
  o.kind = ValueKind::temperature_c;
  o.valid = true;
  o.value = 12.5f;
  o.meta.source = SourceKind::direct_rf;
  o.meta.observed_utc = 1791392400u;
  o.meta.age_seconds = 3;
  std::strcpy(o.source_label, "NOAA \"RF\", KEC42");

  char csv[512]{};
  CHECK(report::encode_observation_csv(o, csv, sizeof(csv)));
  CHECK(std::strstr(csv, "temperature_c") != nullptr);
  CHECK(std::strstr(csv, "direct_rf") != nullptr);
  CHECK(std::strstr(csv, "\"NOAA \"\"RF\"\", KEC42\"") != nullptr);

  report::Session s{};
  std::strcpy(s.id, "20261007-103300-snapshot-0001");
  s.utc_valid = false;
  s.started_uptime_ms = 123456;
  s.online_policy = report::OnlinePolicy::disabled;
  char json[1024]{};
  CHECK(report::encode_session_json(s, json, sizeof(json)));
  CHECK(std::strstr(json, "\"utc_valid\":false") != nullptr);
  CHECK(std::strstr(json, "\"online_policy\":\"disabled\"") != nullptr);
  CHECK(std::strstr(json, "123456") != nullptr);

  char history[512]{};
  CHECK(report::encode_history_jsonl(s, "snapshot", history, sizeof(history)));
  CHECK(history[std::strlen(history)-1] == '\n');
  CHECK(std::strstr(history, "hardware_verified") == nullptr);

  char html[160]{};
  CHECK(report::escape_html("Springfield & <WX> \"A'\"", html, sizeof(html)));
  CHECK(std::strcmp(html, "Springfield &amp; &lt;WX&gt; &quot;A&apos;&quot;") == 0);
  char tiny[8]{};
  CHECK(!report::escape_html("&<>\\\"'", tiny, sizeof(tiny)));

  std::puts("weather_report_format_tests: PASS");
}
