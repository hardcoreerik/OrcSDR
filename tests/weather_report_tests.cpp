#include "weather_report.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
[[noreturn]] static void fail(const char* e,int l){std::fprintf(stderr,"FAIL line=%d check=%s\n",l,e);std::exit(1);}
#define CHECK(x) do{if(!(x))fail(#x,__LINE__);}while(0)
int main(){using namespace orcsdr::weather; ReportSnapshot r{}; r.created_utc=1791412345u; r.created_uptime_ms=12345; r.wallclock_valid=true; r.noaa_frequency_hz=162475000u; r.noaa_dbfs=-31.5f; r.noaa_valid=true; r.online_policy=OnlinePolicy::disabled; std::strcpy(r.location_label,"Springfield, OR"); char json[2048]{}; CHECK(encode_report_json(r,json,sizeof(json))); CHECK(std::strstr(json,"\"online_policy\":\"disabled\"")!=nullptr); CHECK(std::strstr(json,"162475000")!=nullptr); char csv[2048]{}; CHECK(encode_report_csv(r,csv,sizeof(csv))); CHECK(std::strstr(csv,"RF,162475000")!=nullptr); char html[4096]{}; std::strcpy(r.location_label,"A&B <field>"); CHECK(encode_report_html(r,html,sizeof(html))); CHECK(std::strstr(html,"A&amp;B &lt;field&gt;")!=nullptr); CHECK(std::strstr(html,"OFFLINE")!=nullptr); ReportSnapshot relative{}; relative.wallclock_valid=false; relative.created_uptime_ms=9000; CHECK(encode_report_json(relative,json,sizeof(json))); CHECK(std::strstr(json,"\"wallclock_valid\":false")!=nullptr); std::puts("weather_report_tests: PASS");}
