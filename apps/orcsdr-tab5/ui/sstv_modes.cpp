#include "sstv_receiver.hpp"
// Auditable SSTV mode clock table.
//
// Sources:
//   Oona Räisänen's slowrx modespec.c (ISC):
//   https://github.com/oyvindln/slowrx/blob/master/modespec.c
//   Rust slowrx modespec.rs (translation, per-mode source line notes):
//   https://docs.rs/slowrx/latest/src/slowrx/modespec.rs.html
//   N7CXI "A Technical Description of SSTV Modes" (Dayton paper)
//   https://sstv.org.uk/docs/modes/
//   PD50/90/160/290: https://sstv.org.uk/docs/modes/pd/
//   F4HWN UV-K1/K5 V3 overlay generator (Apache-2.0), independently
//   cross-checked from its segment durations and VIS assignments:
//   https://github.com/armel/uv-k1-k5v3-firmware-custom/blob/main/App/apps/sstv/test/modes.py
//   Pixel dwell calculated from documented line families, not frame duration.
// All times use integer microseconds or nanoseconds, eliminating float tables.
namespace orcsdr::sstv {
namespace {
constexpr ModeSpec kSpecs[] = {
  // ID, name, VIS, size, transmitted line, sync, porch, sep, dwell, family
  {Mode::robot36,  "ROBOT 36",    8, 320,240, 150000, 9000, 3000, 6000,  275000, 0},
  {Mode::robot72,  "ROBOT 72",   12, 320,240, 300000, 9000, 3000, 4700,  431250, 0},
  {Mode::martin_m1,"MARTIN M1",  44, 320,256, 446446, 4862,  572,  572,  457600, 1},
  {Mode::martin_m2,"MARTIN M2",  40, 320,256, 226798, 4862,  572,  572,  228800, 1},
  {Mode::scottie_s1,"SCOTTIE S1",60,320,256,428220,9000,1500,1500,432000, 2},
  {Mode::scottie_s2,"SCOTTIE S2",56,320,256,277692,9000,1500,1500,275200, 2},
  {Mode::scottie_dx,"SCOTTIE DX",76,320,256,1050300,9000,1500,1500,1080000, 2},
  {Mode::pd50,    "PD 50",       93, 320,256, 388160,20000,2080,0,286000, 3},
  {Mode::pd90,    "PD 90",       99, 320,256, 703040,20000,2080,0,532000, 3},
  {Mode::pd120,   "PD 120",      95, 640,496, 508480,20000,2080,0,190000, 3},
  {Mode::pd160,   "PD 160",      98, 512,400, 804416,20000,2080,0,382000, 3},
  {Mode::pd180,   "PD 180",      96, 640,496, 754240,20000,2080,0,286000, 3},
  {Mode::pd240,   "PD 240",      97, 640,496,1000000,20000,2080,0,382000, 3},
  {Mode::pd290,   "PD 290",      94, 800,616, 937280,20000,2080,0,286000, 3},
};
}
const ModeSpec* mode_spec(Mode mode) {
  for (const auto& s : kSpecs) if (s.mode == mode) return &s;
  return nullptr;
}
const ModeSpec* vis_spec(uint8_t vis) {
  for (const auto& s : kSpecs) if (s.vis == vis) return &s;
  return nullptr;
}
const char* mode_name(Mode mode) {
  return mode == Mode::auto_vis ? "AUTO VIS" :
      (mode_spec(mode) ? mode_spec(mode)->name : "UNKNOWN");
}
} // namespace orcsdr::sstv
