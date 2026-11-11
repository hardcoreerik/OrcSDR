#pragma once
// PlutoSDR source over USB (see pluto_service.cpp). Compiled to a no-op unless CONFIG_ORCSDR_PLUTO_SOURCE is set.
#include <cstdint>

namespace orcsdr::pluto {
// Starts the task that waits for a Pluto and, when one is attached and no RTL-SDR is streaming, runs it as receiver 0.
bool start();

// Everything the status record shows (RTL_PLUTO STATUS over serial, GET /api/v1/pluto over the web). Plain numbers, no driver types.
struct Snapshot {
  bool compiled = false, present = false;
  char state[12] = {}, device[48] = {}, serial[40] = {}, version[12] = {};
  bool high_speed = false, caps_valid = false, hardware_tx = false, tx_enabled = false;
  uint64_t tune_min_hz = 0, tune_max_hz = 0;
  uint32_t rate_min_sps = 0, rate_max_sps = 0, rate_link_max_sps = 0, out_rate_min_sps = 0, bw_min_hz = 0, bw_max_hz = 0;
  int32_t gain_min_db10 = 0, gain_max_db10 = 0, ppm_min_milli = 0, ppm_max_milli = 0;
  uint32_t adc_bits = 0, rx_channels = 0;
  bool controls_valid = false, gain_manual = false, dc_rf = false, dc_bb = false, quad = false;
  uint64_t center_requested_hz = 0, center_actual_hz = 0;
  int64_t center_error_hz = 0;
  uint32_t rate_requested = 0, rate_out = 0, rate_converter = 0, decimation = 0, bandwidth_hz = 0, xo_hz = 0;
  int32_t gain_db10 = 0, rssi_db10 = 0, ppm_milli = 0;
  uint64_t samples_received = 0, samples_delivered = 0;
  uint32_t source_drops = 0, stale_blocks = 0, usb_errors = 0, sessions = 0, in_sps = 0, out_sps = 0;
};
Snapshot snapshot();
}  // namespace orcsdr::pluto
