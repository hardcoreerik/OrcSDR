# esp-rtl-tcp: our own rtl_tcp for OrcSDR boards

Status: design only, 2026-10-30. No code. Owner decision: break down the original rtl_tcp and write our own `esp-rtl-tcp`; it is
application-layer, never part of the `esp-rtl-sdr` driver (the driver stays independent: no HTTP, WebSocket, Ethernet, Wi-Fi or rtl_tcp in it).

## 1. Provenance

The protocol and behaviour below were read from the original rtl_tcp in osmocom/librtlsdr (GPL-2.0-or-later, Steve Markgraf and Hoernchen). It was read for
what it does, not to copy it: `esp-rtl-tcp` is written from this behaviour description, the way `esp-rtl-sdr` is a clean-room driver and not a librtlsdr port. No
third-party application code is used or merged. Anything below that the original source did not state is marked.

## 2. What rtl_tcp does (the compatibility target)

- **Header**, sent once on connect, 12 bytes: magic `RTL0` (4 raw bytes), tuner type (u32, big-endian), tuner gain count (u32, big-endian); zero if the query fails.
- **Commands** from the client, 5 bytes each: command code (u8) and parameter (u32, big-endian). Unknown codes are ignored.

| Code | Sets | Our mapping (radio_service / driver) |
|---|---|---|
| 0x01 | centre frequency, Hz | `radio::tune_to` |
| 0x02 | sample rate, Hz | `radio::set_sample_rate` (the console accepts 960 kS/s and 2.4 MS/s; other rates are refused or rounded, see section 5) |
| 0x03 | tuner gain mode (manual/auto) | `set_gain_auto` / `set_gain_manual` |
| 0x04 | tuner gain (tenth dB) | `set_gain_manual` |
| 0x05 | frequency correction, ppm | `set_ppm` |
| 0x06 | IF stage gain (stage in the high 16 bits, signed gain in the low 16) | not supported by the driver's tuners today: accepted and ignored, counted |
| 0x07 | test mode | ignored, counted |
| 0x08 | RTL2832 AGC | `set_rtl_agc` |
| 0x09 | direct sampling | only where the dongle profile supports it; otherwise ignored, counted |
| 0x0A | offset tuning | ignored, counted |
| 0x0B / 0x0C | RTL / tuner crystal frequency | ignored, counted (never changed from the network) |
| 0x0D | tuner gain by index into the gain list | index into `radio::gain().steps` |
| 0x0E | bias tee | `set_bias_tee` (the same GPIO-0 convention the original uses) |

- **Defaults:** port 1234; original binds 127.0.0.1; 2,048,000 S/s; 100 MHz; gain 0 = auto; device 0. One client at a time (listen backlog 1); a second connection waits.
- **Samples:** the raw buffers the library delivers, as they arrive. (The original source does not say the encoding; the librtlsdr convention is unsigned 8-bit
  interleaved I/Q, which is also what our driver and `radio_service` produce as CU8, so the stream is passed through unchanged. Not verified against a client yet.)
- **Buffering in the original:** a linked list of heap copies of each block; a sender thread drains it; when the queue reaches its limit (default 500 blocks) the
  **oldest block is dropped with no counter**, only console text. A slow client therefore loses data silently. A command thread reads commands separately.
  No `TCP_NODELAY` or buffer sizes; linger on; sender gives up after 5 s with no data.
- **Session end:** socket error or timeout ends the session; the server goes back to listening.

## 3. What we do differently

1. **Counted loss.** Each block carries the receiver's sequence number (from the sink API). When a client is slow we drop whole blocks, count them, and expose the
   counters (`blocks_sent`, `blocks_dropped`, `bytes_sent`, `max_queue_bytes`, `commands_ignored`, `clients_total`). No silent loss.
2. **Bounded by bytes, in PSRAM.** The queue is a byte budget (configurable, default 1 MB, about 0.2 s at 2.4 MS/s), not a block count in internal RAM. It is a normal queued
   sink from `IQ_SINKS_DESIGN.md`, so a slow client costs only its own blocks and never the receiver or audio.
3. **Safe by default.** The original is unauthenticated and local-only by default. On a board it would expose IQ and radio control to the whole LAN, so: **off by default**,
   enabled only from the authenticated serial console (and later the signed web session), **one client**, optional **client allow-list** (IP), and **listen-only mode** that
   ignores every tuning command (default) unless control is explicitly enabled. Control commands that change the receiver are logged.
4. **Control arbitration.** The screens, the web UI and an rtl_tcp client all want the same receiver. With control enabled, the most recent commander wins and the
   owner-visible status says who holds it; with listen-only the stream follows whatever the board is doing, and the header/`tuner` fields are refreshed on a rate or
   frequency change so the client can track it.
5. **Sample-rate honesty.** `radio_service` runs 960 kS/s or 2.4 MS/s. A client asking for another rate gets the nearest supported one, and the server reports the rate
   it actually runs in its status (the original silently trusts the library).
6. **Both directions.** The server is one half. The client half ("virtual remote receiver") connects to another board's server, reads the header, and feeds the CU8 into a
   network-backed receiver in `radio_service`, so a board with a display and no dongle can use another board's radio. That needs `radio_service` to accept a receiver
   backend that is not USB (per-receiver object exists after the multi-dongle work; the backend split does not).

## 4. Architecture

- `components/esp_rtl_tcp` (own repository later, like `esp-rtl-sdr`), no dependency on OrcSDR. It owns: the listener, the 12-byte header, the command parser, the byte-budgeted
  send queue and its counters, and a **control interface** (`set_frequency`, `set_sample_rate`, `set_gain_mode`, `set_gain`, `set_ppm`, `set_agc`, `set_bias_tee`, `gain_list`,
  `tuner_type`, `current_rate`) that the host application implements. IQ comes in through a plain `push(block)` call.
- OrcSDR glue (in the console): an adapter implementing the control interface over `radio_service`, and registering `push` as a **queued sink**. Enable/disable and status are
  commands in the shared table (`RTL_TCP ON|OFF|STATUS`, `/api/v1/rtltcp`), so serial and the web UI see the same state.
- Dependency direction as for the driver: `esp-rtl-tcp` never depends on OrcSDR; OrcSDR adapts.

## 5. Throughput per board (arithmetic, not measured)

CU8 is 2 bytes per sample: 960 kS/s is 15.4 Mbit/s, 2.048 MS/s is 32.8 Mbit/s, 2.4 MS/s is 38.4 Mbit/s, before TCP overhead.

| Board | Link | Fits |
|---|---|---|
| Waveshare ESP32-P4-ETH (COM29) | 100M Ethernet (no driver in the console yet) | 2.4 MS/s at about 40 % of the link; two dongles at 2.4 MS/s (77 Mbit/s) is tight; three exceed it |
| Waveshare ESP32-P4-WIFI6-DB (COM31) | Wi-Fi via C5 over SDIO at 10 MHz, 4-bit (40 Mbit/s at most, less in practice) | 960 kS/s plausible; 2.4 MS/s does not fit at this clock (OrcNode builds used 40 MHz) |
| Module-DEV-KIT / POE-ETH | C6 Wi-Fi over SDIO; POE-ETH also has Ethernet | as above |

Multi-node designs that pull several dongles to one device need channelised or decimated streams, lower rates, or gigabit; plain rtl_tcp carries the full rate of one dongle per connection.

## 6. Tests (none run)

Header bytes on connect (netcat); each command code changes the right receiver setting and unknown codes are ignored and counted; a slow client (throttled reader) produces counted
drops and no receiver drops; disconnect and reconnect; second client waits; listen-only ignores tuning; memory before/after with a client attached; interoperability with at least two
real clients that speak rtl_tcp (a GUI SDR application and a GNU Radio osmosdr source); throughput per board at 960 kS/s and 2.4 MS/s with measured drops.

## 7. Decisions for the owner

1. Own repository `esp-rtl-tcp` from the start, or inside OrcSDR until it settles?
2. Listen-only by default (proposed) or control allowed once enabled?
3. Is the remote virtual receiver (client half) in scope for the first version?
4. Licence of `esp-rtl-tcp`: AGPL-3.0 like the driver (proposed).
