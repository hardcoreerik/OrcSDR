#include "p25_profile_scan.hpp"
#include <cassert>
#include <iostream>
using orc::zero::P25ProfileScan;
int main(){
  P25ProfileScan scan;const uint32_t channels[]={851000000,851000000,851012500,10};
  assert(scan.set_channels(channels,4));assert(scan.count==2&&scan.channels[1]==851012500);
  scan.start(1000);assert(scan.service(999,false,-20)==0);
  assert(scan.service(1000,false,-80)==851000000);assert(scan.service(1749,true,-70)==0);
  assert(scan.service(1750,true,-35)==851012500);assert(scan.service(2499,false,-50)==0);
  assert(scan.service(2500,false,-50)==851000000);assert(!scan.running()&&scan.selected==0&&scan.self_check());
  std::cout<<"P25 bounded profile channel survey PASS\n";
}
