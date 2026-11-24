// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace orcsdr::nxdn::voice {
using Bits=std::array<uint8_t,288>;
using Wire=std::array<uint8_t,9>;
// Caller supplies one fully measured, PN9-descrambled traffic frame, after LICH
// validation. The existing decoder owns FSW/LICH/SACCH; never infer validity
// from voice FEC. Epoch includes retune, loss, polarity/mode changes.
struct Frame {uint64_t start=0;uint32_t epoch=0;bool rate96=false,lich_valid=false,sacch_valid=false;uint8_t rfct=0,function=0,steal=0,direction=0,ran=0,structure=0;std::array<uint8_t,18> fragment{};Bits body{};};
enum class Privacy:uint8_t {unknown,clear,protected_voice,unsupported};
struct Call {uint16_t source=0,destination=0;uint8_t type=0,option=0,cipher=0,key=0;Privacy privacy=Privacy::unknown;};
struct Output {std::array<Wire,16> frames{};uint8_t count=0;Call call{};uint32_t call_epoch=0;uint64_t first_dibit=0;};
struct Stats {uint32_t accepted=0,held=0,facch_ok=0,facch_bad=0,late_calls=0,resets=0;};
// Fixed memory, no codec, tuner, audio or I/O ownership. Called on serialized receiver worker.
class Gate {
 public:
  void reset() noexcept;
  Output consume(const Frame&) noexcept;
  const Stats& stats() const noexcept{return stats_;}
  const Call& call()const noexcept{return call_;}
 private:
  void invalidate()noexcept;
  bool have_=false,rate96_=false,prev_voice96_=false;uint64_t prev_=0;uint32_t epoch_=0,call_epoch_=0;uint8_t rfct_=0,ran_=0,direction_=0,parts_=0;
  uint64_t held_start_=0;std::array<uint8_t,72> l3_{};std::array<Wire,16> held_{};uint8_t held_n_=0;Call call_{};Stats stats_{};
};
// Independently checked FACCH1 control payload (80 bits); correction count is not CRC.
bool facch(const uint8_t* bits,std::array<uint8_t,10>& payload,uint8_t& distance) noexcept;
}
