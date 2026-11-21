#include "acars_tuning.hpp"
#include <cassert>
#include <iostream>
using namespace orc::zero;
int main(){
 AcarsTuning t;assert(t.requested==131550000);
 assert(t.input(Key::right,0)&&t.requested==131575000);
 t.input(Key::left,0);assert(t.requested==131550000);
 t.input(Key::frequency,0);for(char c:std::string("131.725"))t.input(c=='.'?Key::point:Key::digit,c);
 t.input(Key::enter,0);assert(t.requested==131725000&&!t.editing);
 acars_dashboard::RadioRequest request;assert(acars_dashboard::make_radio_request(t.requested,request));
 assert(request.center_hz==131665000&&request.rate_sps==240000);
 for(const auto* bad:{"131.726","100","137.025"}){
  t.input(Key::frequency,0);for(char c:std::string(bad))t.input(c=='.'?Key::point:Key::digit,c);
  t.input(Key::enter,0);assert(t.editing&&t.requested==131725000&&!t.error.empty());t.input(Key::escape,0);
 }
 t.requested=118000000;t.input(Key::left,0);assert(t.requested==118000000);
 t.requested=137000000;t.input(Key::right,0);assert(t.requested==137000000);
 assert(!t.input(Key::down,0));
 std::cout<<"ACARS frequency entry, 25 kHz stepping, bounds and RF offset PASS\n";
}
