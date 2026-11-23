#pragma once
// Browser audio for the OrcSDR Web page: the node demodulates a receiver itself (its own demodulator, not the speaker's) and streams 48 kHz mono
// PCM to the page over the port-81 WebSocket (/api/v1/fm/audio, packets "OFA1"). This is what the page calls its "FM service".
//
// It is a web session, nothing more: it never touches the board's own speaker, its volume or its mute. The volume the page sets here is only the
// page's own number, kept so the page can show it again after a reload. Starting it moves the receiver to the station (the same as a web tune).
#include <cstddef>
#include <cstdint>
#include "orcsdr_dsp/demod.hpp"

namespace web_audio {
enum class Result : uint8_t { ok, bad_request, not_available };

struct Status {
  bool playing = false;
  int receiver = -1;
  uint32_t station_hz = 0, input_sps = 0;
  int volume = 0;           // the page's own 0..80
  bool client = false;      // a browser is connected to the audio socket
  float rf_dbfs = -120.f;   // channel power after the demodulator's channel filter
  uint32_t dsp_us = 0, block_us = 0, drops = 0;  // smoothed demodulation time per block, the block's length of signal, blocks the demodulator could not take
};

// `request_sps` 0 = the most the demodulator takes. The receiver is set to a rate the driver accepts and the demodulator can run (a multiple of 240 kS/s
// from 960 kS/s to 2.4 MS/s); the rate really used is in the reply. `mode` is "wfm", "nfm", "am", "usb" or "lsb". `peer` is the signed caller's address: only it may open the socket.
Result start(int rx, uint32_t station_hz, uint32_t request_sps, int volume, const char* mode, uint32_t peer, char* out, size_t cap);
void stop();                 // ends the session and closes the audio socket
bool set_volume(int volume); // false when no session is running
// Changes the demodulator of the running session without closing the socket (the filters restart, so there is one short click). Safe from any task;
// with no session running the request is dropped when the next session starts.
void set_mode(orcsdr::dsp::Mode mode);
Status status();
bool available();            // compiled in (needs the same WebSocket support as the channel stream)
}  // namespace web_audio
