# rtl_tcp interoperability: assessment and research record

Status: **client working on hardware, server direction not started.** The remote
`rtl_tcp` client is built into the Tab5 firmware and verified against a real server;
see "Measured results" for what is physical and what is simulated.

## Repository state when work began (2026-11-04)

| Item | Verified value |
|---|---|
| Working branch | `claude/orcsdr-rtl-tcp-interop-f12b5d`, based on `cdc8181a` |
| `origin/main` | `3376510f` (docs-only commit #231 after `cdc8181a`) |
| Driver pin | `esp_rtl_sdr` `43474869e061a78c209091f9b3a65c498ae64fb4` (v0.9.4) in `main/idf_component.yml` and `dependencies.lock` |
| Driver edits | none, and none planned |

## Where IQ goes today (all in `apps/orcsdr-tab5/ui/main.cpp`)

| Stage | Location | Note |
|---|---|---|
| Driver install | `initialize_rtl_sdr_host`, callback delivery, 3 x 32 KiB URBs, USB task core 0 | |
| Event entry | `on_rtl_driver_event`, `ESP_RTL_SDR_EVT_IQ_BLOCK` | Driver data is borrowed; copied in the callback |
| Ring | `rtl_ring_slots[3]`, each `sizeof(rtl_iq_processing)` = 33,280 B, PSRAM | `rtl_free_q` and `rtl_filled_q` hold slot indices and `RtlIqBlock` |
| Consumer | `rtl_dsp_task`, core 1 | One consumer for every decoder and demodulator |
| Drops | `rtl_iq_pipeline_drops`, `rtl_iq_pipeline_drop_samples` | Counted when no free slot |
| Receiver state | `radio_session.hpp` (`ReceiverState`, owner, generation) | Reuse for the remote source |
| Gain, rate, tuning | `esp_rtl_sdr_*` calls in the capture task near `esp_rtl_sdr_start` | Tied to `g_rtl` and `g_rtl_device_ready` |
| Wi-Fi | `wifi_service.hpp` (`connected()`, `ip()`), Hosted C6 link | Existing sockets: `web_audio_transport.cpp` (lwip), `esp_http_server` |

**The seam.** A remote source can be a second producer of `RtlIqBlock`. It takes
a slot from `rtl_free_q`, `recv()`s directly into it (one copy fewer than USB,
which memcpys out of the borrowed driver buffer) and queues the block to
`rtl_filled_q`. `rtl_dsp_task` and every decoder stay unchanged.

**Honest cost.** The capture task that tunes, starts and watches the stream is
bound to `g_rtl`. It is the part that needs a source switch, and `main.cpp` is a
23.9k-line file that should not grow. New code goes in its own modules with thin
hooks.

## Protocol record

Upstream: `osmocom/rtl-sdr`, `src/rtl_tcp.c`, latest commit touching it
`91ef34d92299a26a7af4c14ca05d921239aaed3e` (committed 2024-02-16). Licence
**GPL-2.0-or-later**. OrcSDR is AGPL-3.0, so no upstream code is copied; the
client is written from the wire format.

| Fact | Source |
|---|---|
| Greeting: 12 bytes, `"RTL0"`, tuner type (u32 big-endian), gain count (u32 big-endian) | `rtl_tcp.c` |
| Command: 5 bytes, 1 command byte + u32 big-endian parameter | `rtl_tcp.c` |
| Commands 0x01..0x0e (frequency, rate, gain mode, gain, ppm, IF gain, test mode, AGC, direct sampling, offset tuning, RTL xtal, tuner xtal, gain by index, bias tee) | `rtl_tcp.c` |
| Unknown commands are silently ignored; no command is ever acknowledged | `rtl_tcp.c` |
| Samples: raw unsigned 8-bit I then Q | `rtl_tcp.c` |
| One client at a time; default port 1234; default bind 127.0.0.1 | `rtl_tcp.c` |
| Server queues up to 500 buffers by default and drops the oldest | `rtl_tcp.c` |
| No gain table is sent, only the count; no tuner bandwidth command in this version | `rtl_tcp.c` |

### Compatibility matrix (what real clients do)

Read from source on 2026-11-04. Both clients are GPL-3.0; nothing was copied.

| Behaviour | Upstream `rtl_tcp` server | SDR++ client (`27ab5bf`, `7758c40`) | gr-osmosdr client, used by Gqrx (`14d4116`) |
|---|---|---|---|
| Greeting | Sends 12 bytes | Never reads it | Reads one 12-byte block, prints tuner and gain count |
| Commands used | 0x01..0x0e all handled | 0x01 02 03 05 08 09 0a 0d 0e | 0x01..06, 08, 09, 0a, 0e |
| Above 0x0e | Ignored silently | None sent | None sent |
| Gain | 0x04 tenths dB or 0x0d index | 0x0d index 0..28, hard-coded | 0x04 tenths dB, table hard-coded per tuner |
| Manual gain mode before gain | n/a | Yes, at start | Yes, at construction |
| Order at start | n/a | frequency, then rate | Caller decides |
| Samples | Unsigned 8-bit I, Q | minus 128 | minus 127.4 |
| Reconnect | Server returns to accept after a client leaves | None | None |

Consequences: a server must accept both gain styles; a client cannot rely on an
acknowledgement or on the greeting being consumed by the peer; the gain table
is not on the wire, only its length. This client sends the rate before the
frequency, which the server handles in either order.

### Still unverified

- Tuner type numbering (1 E4000 .. 6 R828D) comes from `rtl-sdr.h`, not fetched.
  The R820T value 5 was confirmed on a real server (`00000005`, 29 gains).
- Other servers (SDR++ server mode, Raspberry Pi packages, forks) are untested.

## What is built so far

| Piece | File | Evidence |
|---|---|---|
| Greeting parser, command encoder, I/Q pair carry, bounded backoff | `ui/rtl_tcp_protocol.{hpp,cpp}` | Host test below |
| Host test | `tests/rtl_tcp_protocol_host_test.cpp` | `RTL_TCP_PROTOCOL_HOST_TEST failures=0 pass=1` |

Covered by that test: byte-exact big-endian encoding, a greeting split at every
possible position and byte by byte, wrong magic refused on the first wrong byte,
implausible gain count, a drop part way through the greeting, I/Q order and
alignment across 50 random TCP split patterns on an odd-length stream, a carried
byte discarded on reconnect, and backoff that is bounded with no overflow.

Run (WSL Ubuntu, from `apps/orcsdr-tab5`):

```bash
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iui \
  ui/rtl_tcp_protocol.cpp tests/rtl_tcp_protocol_host_test.cpp -o /tmp/rtltcp_proto \
  && /tmp/rtltcp_proto
```

This is a **host simulation of byte streams**. It says nothing about Wi-Fi
throughput, memory or timing on the Tab5.

## Integration (built into the Tab5 firmware)

| Piece | File |
|---|---|
| Connection loop (portable BSD sockets, no RTOS) | `ui/rtl_tcp_session.{hpp,cpp}` |
| ESP-IDF worker task, saved settings, serial commands | `ui/rtl_tcp_source.{hpp,cpp}` |
| Source choice: Automatic / Local USB / Remote TCP | `ui/receiver_source_policy.{hpp,cpp}` |
| Hooks (IQ sink, readiness, start/stop/retune, event guards) | `ui/main.cpp` (thin) |

`rtl_device_ready()` now means "a receiver source is ready", so the existing
dashboards run on remote IQ without a second implementation. A dongle appearing
or leaving during a remote stream never changes that stream. Serial commands:
`RTL_TCP STATUS`, `RTL_TCP SOURCE auto|usb|remote`, `RTL_TCP HOST <ip|name> [port]`
(the last two need an authenticated session).

## Measured results

**Physical** = Tab5 on COM17 over Wi-Fi, PC `rtl_tcp` (Windows build dated 2023-09-17)
with a Blog V4L dongle on `192.168.1.15`. **Host** = WSL on the PC.

| Test | Kind | Result |
|---|---|---|
| Protocol, source policy, fake-server session (7+2 scenarios, ASan/UBSan) | Host simulation | all pass (`tools/test-rtl-tcp.sh`) |
| Client vs real server, 0.96 / 1.024 / 2.4 MS/s | Host, real server | 0.952 / 1.008 / 2.383 MS/s effective, 0 dropped (2.4 MS/s = 4.77 MB/s) |
| Tab5 remote receive, stock TCP window 5760 B, 2.4 MS/s requested | Physical | 0.92-0.99 MB/s, `link=41%` |
| Tab5, TCP window 32 KiB | Physical | 1.50-1.56 MB/s |
| Tab5, TCP window 64 KiB, 2.4 MS/s requested | Physical | 1.83-2.11 MB/s |
| Tab5 remote FM, narrowed to 960 kS/s, 64 KiB window | Physical | 1.91-1.98 MB/s (about 100% of the 1.92 MB/s needed), 0 dropped over 40 s, DSP 960,109 samples/s at 6% load, 0 pipeline drops |
| Tab5 local USB (V4 dongle) after the change | Physical | `STREAMING`, 2.394 MS/s effective, 0 overruns, 0 drops |
| Tab5 remote-only boot (mode `remote`, no commands) | Physical | streams by itself after reboot |

Findings:

- **Wi-Fi rate ceiling.** The stock lwip window (5760 B) caps one socket near 1 MB/s.
  A 64 KiB window gives about 2 MB/s, enough for 960 kS/s (1.92 MB/s) but not for
  the 2.4 MS/s dashboard bands (4.8 MB/s). The Hosted SDIO link (10 MHz, 4-bit,
  about 5 MB/s raw) makes 4.8 MB/s unrealistic. While a remote stream runs, wide
  bands therefore request 960 kS/s (`g_rtl_remote_narrow`), and the scope span is
  clamped to the real rate. ADS-B (2.048 MS/s) is not narrowed and will report a
  slow link.
- **Honest staleness.** The client compares arriving bytes with what the sample
  rate needs; under 85% the device says "remote link too slow: behind real time".
- **Server quirks (Windows `rtl_tcp`).** It needs more than 2 s after a client leaves
  before accepting another, and it exited with a libusb transfer error when the
  sample rate was changed mid-stream (960 k to 2.4 M). Start the server at the rate
  you will use.
- **The 64 KiB window is a trial** in `sdkconfig.defaults`. Existing Hosted settings
  favour bounded buffers over throughput; keeping the larger window is the owner's
  decision, and a longer stability and internal-RAM soak has not been run.

## On-device settings

Settings > Radio Defaults has two rows below the existing buttons: **Receiver source**
(CHANGE cycles Automatic, Local USB, Remote rtl_tcp) and **Remote server** (EDIT opens the
shared full-width text editor; `host` or `host:port`, port default 1234). A line under them
shows a coarse remote status (STREAMING, CONNECTING, RECONNECTING, BEHIND REAL TIME, FAILED);
it is deliberately coarse so the page repaints only when it really changes. A new source
applies from the next stream start, and the server cannot be changed while a remote stream
runs. The glue lives in `ui/settings_remote_source.{hpp,cpp}`; `main.cpp` has two one-line
hooks, and the address parser (`parse_endpoint`) is host-tested.

Verified on the Tab5 through the touch path (physical): CHANGE moved Automatic to USB to
Remote and the saved mode followed each time; EDIT opened the editor prefilled with the saved
server; deleting a character and pressing SAVE stored `192.168.1.1`, and retyping the `5`
restored `192.168.1.15`; CANCEL closed the editor without changing anything. Not done: a
dedicated drawing regression for the new rows (the repo's UI standard asks for one), and a
physical flicker check by a person; the rows use the page's existing `draw_content()` repaint
on user action and on a status change.

## IQ integrity: real recording through the network path

`tests/rtl_tcp_replay_host_test.cpp` serves a raw `cu8` recording through a fake server in
odd, irregular chunk sizes and receives it with the production client. Recording used:
8 s of broadcast FM at 96.1 MHz captured with `rtl_sdr -f 96100000 -s 960000` from the
Blog V4L on this PC (15,360,000 bytes, RMS 53.8, 0.07% clipped). Result (host simulation,
3 chunk patterns): **all bytes identical**, no discontinuity, nothing dropped, and a
quadrature FM discriminator (decimate by 4) gives identical output on the file and the
received copy (1.92 M samples, deviation sigma 0.87 rad). This shows the transport does
not alter I/Q order, sample format or count. It does not replace a decode test of an
OrcSDR decoder on the Tab5, which still needs a recording with a known message. Run it with
`RTL_TCP_REPLAY_FILE=<recording> bash tools/test-rtl-tcp.sh`; without a file it skips.

## Remote gain control

The dashboards call the driver's gain functions from about 25 places. Instead of a
second code path in each, `main.cpp` redirects three names (`set_tuner_gain_mode`,
`set_tuner_gain`, `set_rtl_agc`) to `ui/rtl_source_routing.{hpp,cpp}`. While a remote
stream runs the call becomes an `rtl_tcp` command (gain mode, gain in tenths of dB, RTL
AGC); otherwise it reaches the USB driver unchanged. `ESP_OK` on the remote branch means
"queued", because `rtl_tcp` never acknowledges. The driver's getters are not rerouted, so
the gain readout describes the USB dongle's shadow state, not the remote's.

Physical check (Tab5, remote FM through `RTL_GAIN`): auto -33.0 dBFS, then manual 0 dB
-44.0 dBFS, then manual 49.6 dB -12.0 dBFS. The last step appeared about a minute after
the command: the link was at 60-65% of real time, so the server's queue of older samples
had to drain first. **Control latency equals the backlog**, and the "link too slow"
notice is the warning that it exists. Other operations a standard `rtl_tcp` can do
(bias-T, direct sampling, IF gain, crystal trims) are not exposed to the UI.

## Server direction (OrcSDR serves its dongle)

| Piece | File |
|---|---|
| Portable server loop: bind, greeting, one owner, command framing, stall drop | `ui/rtl_tcp_server_session.{hpp,cpp}` |
| Lock-free IQ ring between the USB callback and the server task | `ui/rtl_tcp_iq_ring.hpp` |
| ESP-IDF adapter above the esp-rtl-sdr API; ownership policy; settings | `ui/rtl_tcp_server.{hpp,cpp}` |
| Hooks in `main.cpp` | IQ tee in the driver callback, a local-start gate, serial command |

Policy: off by default; enabling is saved and needs an authenticated serial session
(`RTL_TCPD ENABLE <ipv4|wifi> [port]`, `RTL_TCPD DISABLE`, `RTL_TCPD STATUS`). There
is no default bind address and `0.0.0.0` is refused. One client owns the dongle (tuning
included); a second connection is closed at once and counted. While a client is
connected the local UI cannot start a stream, and a stream that was running resumes
afterwards. A client that stops reading is dropped after 5 s. The bias-T, IF gain,
test mode, direct sampling and crystal-trim commands are never applied and are
counted as unsupported.

| Test | Kind | Result |
|---|---|---|
| Server loop vs raw-socket clients: greeting bytes, split commands, unknown id, second client refused, no stale samples after reconnect, stalled client dropped, busy backend, receiver loss, bind failures, stop latency | Host simulation | all pass |
| IQ ring, 20 MB, two threads | Host simulation | order and pairing intact |
| First client on the Tab5 | Physical | **crashed**: a 16 KB buffer on an 8 KB task stack. Fixed by moving the buffer to the heap |
| After the fix, plain socket client | Physical | greeting `RTL0`, tuner 6 (R828D), 28 gains, IQ received, state back to `listening`, local stream resumed |
| After the fix, `rtl_tcp_probe` client at 960 kS/s | Physical (client on the PC) | 0.54 MB/s received against 1.92 MB/s needed, 0 dropped by the client |

**Independent client.** SatDump 1.2.2 (`rtltcp` source) on the PC recorded from the
Tab5 server (physical):

| SatDump request | Written in 15 s | Meaning |
|---|---|---|
| 250 kS/s (`cs8`) | 5.47 MB | about 0.36 MB/s, 73% of nominal (the stream restarts when the client sets its rate); real signal: RMS 91, carrier peak 27.5 dB over median |
| 1.024 MS/s (`cs8`) | 10.78 MB | about 0.72 MB/s, 35% of nominal; the ring overflowed and the loss was counted (13.7 MB) |

SatDump configured frequency, AGC, gain, bias and PPM, and the server applied what it
supports and counted the rest as unsupported (bias-T is never applied). The same SatDump
command against the PC's real `rtl_tcp` recorded normally, so the harness is sound. In
`cu8` format SatDump wrote nothing against **either** server, which is a SatDump
behaviour and not an OrcSDR one. SDR++ and Gqrx have not been run.

**Server throughput over Wi-Fi (steady state, from the server's own counters).** The
Tab5 transmit path is the limit:

| Setting | Sent |
|---|---|
| stock (SDIO TX queue 4) | 0.3-0.5 MB/s |
| 32 KiB TCP send buffer | no change (reverted) |
| SDIO TX queue 16 (trial, **reverted**) | 0.77-0.83 MB/s |

Queue 16 was reverted: in a 15-minute soak of the remote **client** with it set, receive fell
to 1.13 MB/s (link 55-66%), against 1.33-1.41 MB/s (link 77%) with the stock queue of 4 on
the same day, and the internal DMA low-water mark dropped from 21.6 KB to 14.6 KB. The
client is the first priority, so `sdkconfig.defaults` keeps the stock value.

With the stock queue the Tab5 serves roughly 0.3-0.5 MB/s, about 150-250 kS/s. Higher
rates are accepted, but the ring overflows and the loss shows in `RTL_TCPD STATUS`
(`iq_overrun`). The driver only allows 225-300 kS/s or 900 kS/s and above, so the practical
server ceiling over Wi-Fi is 250 kS/s with gaps expected near it. A wired link, or a
board with Ethernet, is the way to serve wideband rates.

**Client soak (physical, 15 minutes, remote FM, 960 kS/s, 64 KiB window, queue 16):** 31
samples, 0 reconnects, 0 dropped, no resets, internal-RAM and DMA low-water marks flat. Receive
rate varies with Wi-Fi conditions: 1.9 MB/s on one occasion, 1.1-1.4 MB/s later the same
day, so 960 kS/s (1.92 MB/s) is the best case and the "remote link too slow" notice will
appear on a weaker link. Rates below 960 kS/s are not available to the existing wide-band
dashboards because the driver does not allow rates between 300 and 900 kS/s.

The 64 KiB TCP window is still a trial in `sdkconfig.defaults`, awaiting an owner decision.

## Not done yet

A drawing regression and physical flicker check for the new Settings rows, a server
enable control on the screen (the server stays serial-only on purpose), SDR++ and Gqrx
runs, a longer soak, and a test against a non-Windows `rtl_tcp` server.
