#pragma once
#include "control/protocol.hpp"
namespace orc::sstv_control {
constexpr uint32_t kSchemaV1 = 1u << 30;
constexpr uint8_t kSignal=1, kReceiving=2, kSd=4, kGainAvailable=8, kGainAuto=16;
struct Snapshot {
  bool valid=false;
  uint8_t state=0,mode=0,flags=0;
  uint16_t row=0,rows=0,pending=0;
  uint32_t session=0,complete=0,saved=0,failed=0;
};
inline Snapshot decode(const Packet& p) {
  Snapshot s;
  if(p.type!=Type::radio_state || p.role!=Role::receiver || p.dashboard!=18 ||
     !(p.capabilities&kSchemaV1) || p.view>7 || p.action>14 || (p.flags&~31u) || p.squelch<0)return s;
  s.row=uint16_t(p.item_count);s.rows=uint16_t(p.item_count>>16);
  if(s.row>s.rows || s.rows>1024)return {};
  s.valid=true;s.state=p.view;s.mode=p.action;s.flags=p.flags;
  s.session=uint32_t(p.selected);s.complete=uint32_t(p.value);s.saved=p.revision;s.failed=p.capabilities&0xffffu;
  s.pending=uint16_t(p.squelch);
  return s;
}
inline const char* mode_name(uint8_t mode) {
  static const char* names[]={"AUTO","ROBOT36","ROBOT72","MARTIN M1","MARTIN M2","SCOTTIE S1","SCOTTIE S2","SCOTTIE DX","PD50","PD90","PD120","PD160","PD180","PD240","PD290"};
  return mode<15?names[mode]:"--";
}
inline const char* state_name(uint8_t state) {
  static const char* names[]={"WAITING","VIS HEADER","SYNC SEARCH","PROVISIONAL","RECEIVING","SYNC RECOVERY","FINALIZING","READY"};
  return state<8?names[state]:"--";
}
} // namespace orc::sstv_control
