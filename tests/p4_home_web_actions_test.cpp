#include "home_web_actions.hpp"
#include <cassert>
#include <cstdio>
using namespace home_app;
using namespace orcsdr;
int main() {
  home::Action a{};home::Snapshot s{};
  s.tune_min_hz=100000;s.tune_max_hz=1700000000;
  for(const char* forbidden:{"volume","mute","screen","open_settings","open_dashboard"})
    assert(web_actions::parse(forbidden,0,100000,1700000000,&a)==WebAction::not_available);
  assert(web_actions::parse("bogus",0,100000,1700000000,&a)==WebAction::bad_action);
  assert(web_actions::parse(nullptr,0,100000,1700000000,&a)==WebAction::bad_action);
  assert(web_actions::parse("tune",99999,100000,1700000000,&a)==WebAction::bad_value);
  assert(web_actions::parse("tune",96100000,100000,1700000000,&a)==WebAction::ok);
  assert(a.kind==home::ActionKind::tune_frequency&&a.value==96100000);
  assert(web_actions::validate(a,s,false)==WebAction::not_available);
  assert(web_actions::validate(a,s,true)==WebAction::ok);
  for(const char* name:{"step_up","step_down","span_up","span_down"}) {
    assert(web_actions::parse(name,1,100000,1700000000,&a)==WebAction::bad_value);
    assert(web_actions::parse(name,0,100000,1700000000,&a)==WebAction::ok);
    assert(web_actions::validate(a,s,true)==WebAction::ok);
  }
  assert(web_actions::parse("step_size_up",0,100000,1700000000,&a)==WebAction::ok);
  s.step_adjustable=false;
  assert(web_actions::validate(a,s,true)==WebAction::not_available);
  s.step_adjustable=true;assert(web_actions::validate(a,s,true)==WebAction::ok);
  assert(web_actions::parse("mode_set",6,100000,1700000000,&a)==WebAction::bad_value);
  assert(web_actions::parse("mode_set",5,100000,1700000000,&a)==WebAction::ok);
  assert(web_actions::validate(a,s,true)==WebAction::not_available);
  s.mode_selectable=true;assert(web_actions::validate(a,s,true)==WebAction::ok);
  s.filter_kind=filter_standards::Kind::wfm;
  assert(web_actions::parse("filter_set",1,100000,1700000000,&a)==WebAction::ok);
  assert(web_actions::validate(a,s,true)==WebAction::bad_value);
  a.value=filter_standards::standards(s.filter_kind).standard_hz;
  assert(web_actions::validate(a,s,true)==WebAction::ok);
  s.filter_kind=filter_standards::Kind::fixed;assert(web_actions::validate(a,s,true)==WebAction::not_available);
  assert(web_actions::parse("gain_auto",0,100000,1700000000,&a)==WebAction::ok);
  assert(web_actions::validate(a,s,true)==WebAction::not_available);
  s.driver_ready=true;s.gain_auto_available=true;assert(web_actions::validate(a,s,true)==WebAction::ok);
  assert(web_actions::parse("gain_tenth_db",uint32_t(-99),100000,1700000000,&a)==WebAction::ok);
  s.gain_available=true;s.gain_step_count=2;s.gain_steps_tenth_db[0]=-99;s.gain_steps_tenth_db[1]=402;
  assert(web_actions::validate(a,s,true)==WebAction::ok);
  a.value=403;assert(web_actions::validate(a,s,true)==WebAction::bad_value);
  assert(web_actions::parse("rtl_agc",2,100000,1700000000,&a)==WebAction::bad_value);
  assert(web_actions::parse("rtl_agc",1,100000,1700000000,&a)==WebAction::ok);
  assert(web_actions::validate(a,s,true)==WebAction::not_available);
  s.rtl_agc_available=true;assert(web_actions::validate(a,s,true)==WebAction::ok);
  s.driver_ready=false;assert(web_actions::validate(a,s,true)==WebAction::not_available);
  a.kind=home::ActionKind::sound_toggle;assert(web_actions::validate(a,s,true)==WebAction::bad_action);
  std::puts("P4 Home web action admission and owner capability checks PASS");
}
