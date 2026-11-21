#include "acars_tuning.hpp"
#include <cassert>
#include <iostream>
using namespace orc::zero;
int main(){
 AcarsTuning t;assert(t.requested==131550000);
 assert(t.input(Key::right,0)&&t.preset_index==1);
 t.input(Key::left,0);assert(t.requested==131550000);
 t.input(Key::frequency,0);for(char c:std::string("131.725"))t.input(c=='.'?Key::point:Key::digit,c);
 t.input(Key::enter,0);assert(t.requested==131725000&&!t.editing&&t.custom_hz==131725000);
 const auto custom=t.custom_hz;
 t.input(Key::step,0);assert(t.region==orcsdr::acars_ui::Region::europe&&t.preset_index==0);
 assert(t.requested==orcsdr::acars_ui::preset(t.region,0).frequency_hz&&t.custom_hz==custom);
 t.input(Key::right,0);assert(t.preset_index==1&&t.custom_hz==custom);
 t.requested=custom;
 acars_dashboard::RadioRequest request;assert(acars_dashboard::make_radio_request(t.requested,request));
 assert(request.center_hz==131665000&&request.rate_sps==240000);
 for(const auto* bad:{"131.726","100","137.025"}){
  t.input(Key::frequency,0);for(char c:std::string(bad))t.input(c=='.'?Key::point:Key::digit,c);
  t.input(Key::enter,0);assert(t.editing&&t.requested==131725000&&!t.error.empty());t.input(Key::escape,0);
 }
 assert(!t.input(Key::down,0));
 std::cout<<"ACARS regional presets, custom tuning, bounds and RF offset PASS\n";
}
