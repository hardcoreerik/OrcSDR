#pragma once
#include <cstring>
#include <algorithm>
#include "home_dashboard.hpp"

namespace home_app {
enum class WebAction : uint8_t { ok, bad_action, bad_value, not_available, queue_full };
namespace web_actions {
inline const char* result_name(WebAction result) {
  switch(result) {
    case WebAction::ok:return "ok";case WebAction::bad_action:return "bad_action";
    case WebAction::bad_value:return "bad_value";case WebAction::not_available:return "not_available";
    case WebAction::queue_full:return "queue_full";
  }
  return "unknown";
}
using Action=orcsdr::home::Action;
using Kind=orcsdr::home::ActionKind;
inline WebAction parse(const char* name,uint32_t value,uint32_t min_hz,uint32_t max_hz,Action* out) {
  if(!name||!out)return WebAction::bad_action;
  struct Entry {const char* name;Kind kind;bool value;};
  static constexpr Entry entries[]={
    {"tune",Kind::tune_frequency,true},{"step_up",Kind::step_up,false},
    {"step_down",Kind::step_down,false},{"step_size_up",Kind::step_size_up,false},
    {"step_size_down",Kind::step_size_down,false},{"span_up",Kind::span_up,false},
    {"span_down",Kind::span_down,false},{"mode_set",Kind::mode_set,true},
    {"filter_set",Kind::filter_set,true},{"gain_auto",Kind::gain_auto,false},
    {"gain_tenth_db",Kind::gain_tenth_db,true},{"rtl_agc",Kind::rtl_agc,true}};
  for(const auto& entry:entries)if(!std::strcmp(name,entry.name)) {
    if(!entry.value&&value)return WebAction::bad_value;
    if(entry.kind==Kind::tune_frequency&&(value<min_hz||value>max_hz))return WebAction::bad_value;
    if(entry.kind==Kind::mode_set&&value>5)return WebAction::bad_value;
    if(entry.kind==Kind::rtl_agc&&value>1)return WebAction::bad_value;
    *out={};out->kind=entry.kind;out->value=value;return WebAction::ok;
  }
  if(!std::strcmp(name,"volume")||!std::strcmp(name,"mute")||
      !std::strcmp(name,"screen")||!std::strncmp(name,"open_",5))return WebAction::not_available;
  return WebAction::bad_action;
}
// Rechecked by the UI owner immediately before the existing native handler.
inline WebAction validate(const Action& a,const orcsdr::home::Snapshot& s,bool home_active) {
  if(!home_active)return WebAction::not_available;
  switch(a.kind) {
    case Kind::tune_frequency:
      return a.value>=s.tune_min_hz&&a.value<=s.tune_max_hz?WebAction::ok:WebAction::bad_value;
    case Kind::step_up:case Kind::step_down:case Kind::span_up:case Kind::span_down:return WebAction::ok;
    case Kind::step_size_up:case Kind::step_size_down:
      return s.step_adjustable?WebAction::ok:WebAction::not_available;
    case Kind::mode_set:return s.mode_selectable?WebAction::ok:WebAction::not_available;
    case Kind::filter_set:{
      const auto& f=orcsdr::filter_standards::standards(s.filter_kind);
      if(!f.count)return WebAction::not_available;
      if(!a.value)return WebAction::ok;
      for(unsigned i=0;i<f.count;++i)if(f.presets_hz[i]==a.value)return WebAction::ok;
      return WebAction::bad_value;
    }
    case Kind::gain_auto:return s.driver_ready&&s.gain_auto_available?WebAction::ok:WebAction::not_available;
    case Kind::rtl_agc:return s.driver_ready&&s.rtl_agc_available?WebAction::ok:WebAction::not_available;
    case Kind::gain_tenth_db:
      if(!s.driver_ready||!s.gain_available)return WebAction::not_available;
      for(unsigned i=0;i<std::min<unsigned>(s.gain_step_count,sizeof(s.gain_steps_tenth_db)/sizeof(s.gain_steps_tenth_db[0]));++i)
        if(a.value==static_cast<uint32_t>(static_cast<int32_t>(s.gain_steps_tenth_db[i])))return WebAction::ok;
      return WebAction::bad_value;
    default:return WebAction::bad_action;
  }
}
}
}
