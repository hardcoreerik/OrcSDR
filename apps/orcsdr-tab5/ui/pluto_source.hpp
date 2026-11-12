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
};

// Installs the driver and starts a watcher task. Returns false when the driver would not install. Does nothing on the USB bus until a Pluto
// appears, and never starts a stream by itself.
bool begin(const IqSink& sink, const Events& events);

bool ready();  // a Pluto is attached and can start a stream (or is streaming)
bool active();  // a stream from the Pluto is running or starting

// Starts a stream at the output rate (the driver plans converter rate and decimation) and centre frequency, then waits, bounded, for the
// first block. False on timeout, failure or cancel; the stream is stopped again in that case.
bool start_and_wait(uint32_t sample_rate_sps, uint32_t frequency_hz, uint32_t timeout_ms, bool (*cancelled)());
bool alive();  // the stream is delivering, or has been quiet for less than the driver's recovery window
void stop();   // bounded wait

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
  char product[48]{};
  char serial[40]{};
  char version[24]{};
};
Status status();

}  // namespace orcsdr::pluto_source
