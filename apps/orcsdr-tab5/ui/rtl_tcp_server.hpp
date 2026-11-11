#pragma once

// Optional rtl_tcp server for OrcSDR: serves the locally attached dongle to
// external SDR software (SDR++, Gqrx, SatDump ...). It is an adapter above the
// esp-rtl-sdr public API; it does not touch the USB driver internals.
//
// Safety and ownership:
//  - Off by default. Enabling is an explicit, saved, authenticated action.
//  - It binds only to the address the owner names ("wifi" = the Tab5's own
//    Wi-Fi address). There is no default and no 0.0.0.0 shortcut.
//  - rtl_tcp has no authentication. Use it on a trusted network only.
//  - One client owns the dongle at a time; others are refused. While a client is
//    connected the local UI cannot start its own stream.
//  - The bias-T is never switched by a remote client.

#include <cstddef>
#include <cstdint>

#include <esp_rtl_sdr.h>

#include "rtl_tcp_server_session.hpp"

namespace orcsdr::rtl_tcp_server {

struct Hooks {
  esp_rtl_sdr_handle_t (*handle)();
  bool (*usb_ready)();
  // Primitives for taking the dongle from the local UI; the ownership policy
  // itself (stop, wait, block, resume) lives in this module.
  bool (*local_stream_busy)();       // a local stream is running or queued
  void (*request_local_stop)();      // ask it to stop, without auto-restart
  void (*allow_local_start)(bool);   // false while a network client owns the dongle
  void (*resume_local)();            // restart what the local UI was doing
  bool (*network_up)();
  const char* (*wifi_ip)();  // "" when there is no address
};

struct Status {
  rtl_tcp::ServerState state = rtl_tcp::ServerState::stopped;
  bool enabled = false;
  char bind[20]{};
  uint16_t port = rtl_tcp::kDefaultPort;
  bool client_active = false;
  uint32_t sample_rate_sps = 0;
  uint32_t frequency_hz = 0;
  uint32_t clients_accepted = 0;
  uint32_t clients_refused = 0;
  uint32_t stalled_clients = 0;
  uint32_t commands_applied = 0;
  uint32_t commands_unknown = 0;
  uint32_t commands_unsupported = 0;
  uint32_t command_errors = 0;
  uint64_t bytes_sent = 0;
  uint64_t iq_overrun_bytes = 0;  // IQ the network could not take, dropped whole
};

void begin(const Hooks& hooks);

// Called from the driver's IQ event. Returns true when the server took the block,
// so the caller must not also feed it to the local DSP.
bool offer_iq(const uint8_t* data, size_t bytes);

bool enabled();
Status status();

// RTL_TCPD STATUS | ENABLE <ipv4|wifi> [port] | DISABLE. Returns true when `line`
// was an RTL_TCPD command and writes a one-line reply.
bool handle_command(const char* line, char* reply, size_t reply_size);

}  // namespace orcsdr::rtl_tcp_server
