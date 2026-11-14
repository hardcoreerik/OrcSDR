#pragma once

// PlutoSDR (AD9361) as a local receiver for OrcSDR: a second class of USB receiver beside the RTL-SDR dongle, with a much wider tuning range
// (70 MHz to 6 GHz) and sample rates. This file is only the adapter. All device logic (USB, iiod, rate and decimation planning, ranges, gain,
// the rate-change barrier) lives in the standalone esp-pluto-sdr driver, which knows nothing about OrcSDR. Receive only: nothing here writes a
// transmit attribute.
//
// Like the remote rtl_tcp source it feeds the application's IQ ring through IqSink, so every demodulator and decoder downstream is shared.
// The RTL-SDR driver owns the USB host library; the Pluto driver adds one more client to it, so begin() must run after esp_rtl_sdr_install().

#include <cstddef>
#include <cstdint>

#include "rtl_tcp_source.hpp"

namespace orcsdr::pluto_source {

using IqSink = rtl_tcp_source::IqSink;

struct Events {
  void (*attached)();  // a Pluto enumerated and can stream (called from the watcher task; must not block)
  void (*detached)();  // the Pluto went away
  // Optional. Cuts and restores the USB port's 5 V rail, the only real reset a wedged Pluto gets. Returns false when it must not run now (a dongle
  // or another stream is using the port). Called from the watcher task, so it may block for a couple of seconds.
  bool (*power_cycle)() = nullptr;
  // Optional. In stop mode the power guard went critical and wants the stream stopped (true), or cleared (false) so the application may resume it.
  // See pluto_power_guard.hpp. Called from the watcher task, so it only sets flags.
  void (*power_guard)(bool tripped) = nullptr;
};

// Installs the driver and starts a watcher task. Returns false when the driver would not install. Does nothing on the USB bus until a Pluto
// appears, and never starts a stream by itself.
bool begin(const IqSink& sink, const Events& events);

// True when a Pluto has been used with this unit before (a flag kept in NVS; cleared when the recovery ladder gives up). The boot sequence uses
// it to wait for a Pluto, which needs 20 to 25 s to boot, instead of treating the missing dongle as the end of the search. Cheap after the first call.
bool previously_used();

// The boot sequence cut and restored the USB 5 V rail itself. Holds the recovery ladder off while the Pluto boots, so it does not cut it again.
void note_power_cycled();

// Low-battery awareness (pluto_power_guard.hpp). It acts only on a level sustained across several readings, never on one, and it never refuses a
// start: a manual start always works and just clears a guard stop.
//   off   nothing
//   warn  (default) battery_low() is true while the level is low or critical, so the screen can say so; the stream is never touched
//   stop  as warn, and when the level goes critical the stream is stopped while there is still battery; it resumes when charging or recovered
// Switching to off clears any state.
enum class GuardMode : uint8_t { off = 0, warn = 1, stop = 2 };
void set_guard_mode(GuardMode mode);
GuardMode guard_mode();
bool battery_low();    // the host battery has been low (or critical) and discharging over several readings
bool guard_stopped();  // the guard stopped the stream (stop mode) and it has not been cleared or restarted

bool ready();  // a Pluto is attached and can start a stream (or is streaming)
bool active();  // a stream from the Pluto is running or starting

// Starts a stream at the output rate (the driver plans converter rate and decimation) and centre frequency, then waits, bounded, for the
// first block. False on timeout, failure or cancel; the stream is stopped again in that case.
bool start_and_wait(uint32_t sample_rate_sps, uint32_t frequency_hz, uint32_t timeout_ms, bool (*cancelled)());
bool alive();  // the stream is delivering, or has been quiet for less than the driver's recovery window
void stop();   // bounded wait

// The output rate the running stream delivers (0 when no stream runs). Cheap enough for a screen refresh.
uint32_t output_rate_sps();

bool retune(uint32_t frequency_hz);
bool set_gain_auto();
bool set_gain_manual_tenths_db(int tenths_db);

struct Gain {
  bool known = false;
  bool manual = false;
  int tenths_db = 0;
  int min_tenths_db = 0;
  int max_tenths_db = 0;
  uint8_t step_count = 0;
  int steps[32]{};
};
Gain gain();

struct Status {
  bool present = false;
  bool streaming = false;
  uint32_t frequency_hz = 0;      // centre the device actually reports
  uint32_t sample_rate_sps = 0;   // output rate
  uint32_t tune_min_hz = 0;
  uint32_t tune_max_hz = 0;       // clamped to 32 bits; the device itself goes to 6 GHz
  uint64_t samples_delivered = 0;
  uint32_t source_drops = 0;
  uint32_t usb_errors = 0;
  uint32_t ring_drops = 0;        // blocks the IQ ring had no free slot for
  uint32_t recovery_cycles = 0;   // 5 V power cycles tried since the last healthy stream
  bool recovery_gave_up = false;  // the ladder is exhausted; the board needs a manual power cycle
  char product[48]{};
  char serial[40]{};
  char version[24]{};
};
Status status();

}  // namespace orcsdr::pluto_source
