#include "wefax_schedule.hpp"
#include <cassert>
#include <cstdio>
using namespace orcsdr::wefax_schedule;
int main() {
  assert(station_count()==2 && broadcast_count()==15);
  assert(station(0) && station(1) && !station(2));
  assert(frequency(0,4)->assigned_hz==22527000 && !frequency(0,5));
  assert(frequency(1,0)->published==Daypart::night);
  assert(usb_dial_hz(8682000)==8680100);
  assert(usb_dial_hz(6340500)==6338600);
  assert(usb_dial_hz(1000)==0);
  char t[9];format_utc_minute(140,t,sizeof t);assert(t[0]=='0'&&t[1]=='2'&&t[2]==':'&&t[3]=='2'&&t[4]=='0');
  format_utc_minute(1500,t,sizeof t);assert(t[0]==0);
  Upcoming next[4]{};
  const uint64_t midnight = 1793232000ull;
  const uint64_t base = (midnight / 86400) * 86400;
  const size_t n=next_broadcasts(base + 100*60,0,next,4);
  assert(n==4 && next[0].broadcast->utc_minute==103);
  assert(next[0].start_utc==base + 103*60);
  for(size_t i=1;i<n;i++)assert(next[i].start_utc>=next[i-1].start_utc);
  const size_t wrap=next_broadcasts(base + 23*3600+59*60,0,next,4);
  assert(wrap==4 && next[0].start_utc==base+86400+100*60);
  assert(next[0].broadcast->station_index==0);
  assert(next_broadcasts(0,-1,next,4)==0);
  assert(next_broadcasts(base,-2,next,4)==0);
  assert(next_broadcasts(base,2,next,4)==0);
  const size_t all=next_broadcasts(base+140*60,-1,next,4);
  assert(all==4 && next[0].broadcast->station_index==1 && next[0].broadcast->utc_minute==150);
  std::puts("WEFAX_SCHEDULE_PASS: NOAA station frequencies, offset, UTC ordering, rollover, unknown clock, bounds");
}
