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
restored `192.168.1.15`; CANCEL closed the editor without changing anything.

Drawing regression (`tests/settings_remote_ui_tests.cpp`, host, against the repo's display
stub as `ui/AGENTS.md` asks): the rows draw; CHANGE cycles the source with **no full-screen
clear**; EDIT opens the editor prefilled, CANCEL leaves the server alone, SAVE hands the text
over exactly once; an identical status update repaints nothing, a real status change repaints
one region and still no full-screen clear; safe mode hides the rows. Not done: a person
looking at the real panel for flicker. The rows use the page's existing `draw_content()`
repaint on a tap or a status change.

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

## No-dongle test and audio findings (2026-11-05, Tab5 on COM17)

Physical results, one Tab5 with the V4 unplugged, then replugged, against the PC's Blog V4L
through the Windows `rtl_tcp` (2023 build):

- **Remote-only start works with no USB device.** Through a logging proxy and against a fake
  server on the LAN the Tab5 started a receiver with an empty USB port: greeting read, rate and
  frequency sent, IQ streamed (36 MB through the proxy, 0 dropped bytes, no reset).
- **Bug found and fixed: 3 s greeting wait.** Direct to the real Windows `rtl_tcp` the client
  reported "not an rtl_tcp server" on every attempt. That server can take 2-5 s to greet a
  peer shortly after another client left, and the client gave up at 3 s and reconnected, which
  restarted the server's teardown. The wait is now 10 s. After the fix the Tab5 connected
  directly with 1 connection and 0 reconnects.
- **Audio.** The demodulated audio path is live during a remote FM stream (speaker running,
  0 audio drops from `RTL_AUDIO_TEST STATUS`). The earlier "real audio" reading (RMS 150-2700)
  was noise: the PC dongle (Blog V4L, a System32 `rtl_sdr`/`rtl_tcp` build, and also SDR++'s
  vanilla `rtlsdr.dll`) hears nothing from 30 MHz to 1.7 GHz in this setup, and every capture
  it made at 96.1 MHz (a strong local station) shows no 19 kHz pilot. So no real station has
  been decoded through the remote path yet, and the earlier bit-exact FM replay was a replay
  of that noise recording. What is verified: a synthetic FM signal (clean alternating 440/880
  Hz tones, 75 kHz deviation, served by a paced fake `rtl_tcp` server) played as an alternating
  tone on the Tab5 speaker, heard by the owner; the first run was choppy and inconsistent
  (link 19-54%) and after the fix below it ran at 95-98% with 0 dropped bytes.
- **Root cause of the low throughput: a broken `sdkconfig.defaults` line.** The line
  `CONFIG_LWIP_TCP_RECVMBOX_SIZE=48` had a comment glued onto it
  (`...=48# Catalog HTTPS needs...`), so the setting was ignored and the build kept the stock
  receive mailbox of 6 segments. Unthrottled receive was 0.3 MB/s, on both the FM and Home
  screens and on a fresh boot. With the line split, unthrottled receive is a steady 1.8 MB/s
  (min 1.49, max 1.93) and real-time 960 kS/s (1.92 MB/s needed) holds 95-98% on one Wi-Fi
  link. The 64 KiB window is still a trial setting; this was one physical run on one link.
- **Adaptive rate (new).** When the link stays under 85% for four 2 s samples the remote rate
  steps 960 -> 480 kS/s and the stream restarts (`RateAdapter`, host-tested). It did not
  recover the link on this run.
- **Open defect: 240 kS/s crashes the DSP.** Stepping to 240 kS/s (decimation 1) produced a
  Guru Meditation "Instruction access fault" on core 0 about a second after the restart, with
  the WBFM DSP started as `rate=240000 decim=1/5`. That decimation was never used before
  (local rates are 960 kS/s and 2.4 MS/s). The adapter is capped at 480 kS/s until the DSP
  path is reviewed. 480 kS/s ran 110 s without a reset.
- **SDR++ 1.3.0 test copy** connected to a fake server on 127.0.0.1 but reported "Could not
  connect" to every non-loopback address, including a Python server on this PC's LAN address,
  so it gave no verdict on the Tab5 server. Python clients and SatDump clients did read
  the Tab5 server (greeting RTL0, tuner 6, 28 gains, commands applied, IQ streamed).

## Real FM through the remote path (2026-11-05, Tab5 on COM17, no dongle on the Tab5)

Physical result. The PC's RTL-SDR Blog V3C ran `rtl_tcp` (960 kS/s, 96.1 MHz) and the Tab5
used it as its Remote source over Wi-Fi.

- A capture from the V3C shows a real station: 19 kHz pilot 30-36 dB over its neighbours. At
  automatic gain the V3C clips 43% of its samples on this strong local signal; a manual gain of
  10 dB gave a clean capture. (The earlier captures from the PC's Blog V4L were noise; the
  owner reports that dongle is fine and the PC-side setup is the likely cause, not established.)
- The owner heard clear FM from KZEL 96.1 on the Tab5 speaker (owner-observed).
- A screenshot of the Tab5 FM Broadcast screen shows RDS decoded: station "KZEL", radio text
  "Yes - Owner Of A Lonely Heart", STEREO lit, RUNNING, live stereo VU meters, relative level
  98%. The owner confirmed the song title and artist match what the station was playing
  (owner-observed). RDS and the stereo pilot need a clean, continuous signal, which matches the measured
  link: 97% of real time, 0 dropped bytes.
- **Remote-only boot autostart, hardware reset.** With the Tab5's own dongle unplugged, a hard
  reset (esptool, RTS) was followed by an automatic connection to the PC `rtl_tcp` and FM
  streaming: 1 connection, 0 reconnects, 0 dropped bytes, 47 MB in the first 48 s.

Known display gaps seen in these screenshots, not changed: the header and footer label a
remote stream "RTL-SDR" with gain "N/A" and "-- MSPS", and in the FM Broadcast screen's
Relative Level box the "GAIN" label is overprinted by the red "AMP 85.47%" text (not checked
on a local USB stream).

## 240 kS/s crash: root cause and fix (2026-11-06)

The adaptive rate used to stop at 480 kS/s because stepping to 240 kS/s (RF decimation 1) crashed the receiver
(Guru Meditation, instruction access fault, `rtl_tcp` task, frame pointer on `g_env`).

- **Cause:** the demodulators emit one audio sample per `kFmAudioDecim x rf_decimation` IQ pairs into a
  `kRtlAudioBufferSamples` (2048) buffer. A full 32 KiB remote read at 240 kS/s makes about 3277 samples, so the audio
  buffer overran into neighbouring globals. Decimation 2 and 4 never exceed the buffer, which is why only 240 kS/s failed.
- **Why it hid:** a synthetic server sending 8 KiB blocks never overran the buffer. A real `rtl_tcp` (V4 on the PC) sends
  full reads and crashed the Tab5 twice at 240 kS/s; the saved core dump named the `rtl_tcp` task.
- **Fix:** `rtl_tcp_sink_acquire` caps the read size to what the audio buffer holds at the active rate. The adaptive
  floor is now 240 kS/s.
- **Verified:** a real V4 stream throttled to force 960 -> 480 -> 240 kS/s stayed up at 240 kS/s (link 101%, no reset).
  Audio quality at 240 kS/s on a clean link was not judged by ear.

## Wi-Fi / SDIO throughput investigation (2026-11-06)

`RTL_TCP BENCH <seconds>` (serial, no active stream) connects to the configured server and only counts received
bytes, so the link is measured without the demodulator or the IQ ring. Measured against an unthrottled sender
(the sender is a plain 16 KiB `sendall` loop, so the cap is TCP backpressure from the Tab5 side, not the PC):

| Change (one at a time) | Receive |
|---|---|
| SDIO 10 MHz (committed default) | about 1.8 MB/s (rtl_tcp path) |
| SDIO 20 MHz | 2.6-2.7 MB/s rtl_tcp path for 3 min, 0 drops; 2.2 MB/s raw bench |
| SDIO 40 MHz | 2.0-2.5 MB/s rtl_tcp path, stable, no gain over 20 MHz |
| SDIO RX queue 2 -> 8 (20 MHz) | 2.10 MB/s raw bench, no gain; reverted |

Espressif's reference for P4 + C6 (4-bit SDIO, 40 MHz) is 30-32 Mbit/s TCP; the raw bench gives 17-18 Mbit/s. The bus
clock and the host RX queue are therefore not the limit. Untested next levers: the C6 firmware's Wi-Fi buffer profile
(RX/TX buffer counts, block-ack windows, AMPDU), the Wi-Fi link itself (RSSI, AP, 802.11ax vs 11n), and ESP-Hosted 3.1.0.
The 20 MHz clock is not committed: the earlier failure was sustained HTTPS download, which still needs a soak.

### Throughput campaign, round 2 (2026-11-07)

Raw receive (`RTL_TCP BENCH 20`, unthrottled sender on a wired PC), one change at a time, ESP-Hosted 3.0.6 on both chips:

| Change | MB/s | Mbit/s |
|---|---|---|
| SDIO 20 MHz, stock C6 image | 2.22 | 17.8 |
| + C6 Wi-Fi profile (static RX 16, dynamic RX/TX 64, AMPDU on, block-ack windows 32) in `orcdial/c6/sdkconfig.defaults` | 2.68 | 21.4 |
| + SDIO 40 MHz | 2.55 | 20.4 |
| + Wi-Fi power save off (`esp_wifi_set_ps(NONE)`, confirmed `ps=0`) | 2.41 | 19.3 |

Radio link as reported by the C6 (`RTL_WIFI_LINK`): 2.4 GHz channel 11, 20 MHz wide, internal antenna; RSSI and the
11b/g/n/ax flags come back as 0 (the Hosted reply does not fill them). A PC scan shows a busy 2.4 GHz neighbourhood.
Neither the SDIO clock, the host RX queue, the C6 buffer profile nor power save moves the ceiling beyond about
2.7 MB/s, so the limit is probably the 2.4 GHz HT20 radio path. Remaining ideas: ESP-Hosted 3.1.0 on both chips, HT40,
the external antenna, another AP channel. Updating the C6: `RTL_ORCDIAL_C6_UPDATE CONFIRM` re-flashes the embedded
image even at the same version (`RTL_WIFI_C6_UPDATE CONFIRM` refuses when the version matches).

### Throughput campaign, round 3: ESP-Hosted 3.1.0 and TCP/IP tuning (2026-11-07)

New diagnostics: `RTL_TCP BENCHUDP <seconds> [port]` receives UDP datagrams (32-bit sequence in the first four bytes) and
reports loss; `RTL_WIFI_LINK` now reports real RSSI, channel, width, PHY flags and power-save once the host is on 3.1.0
(on 3.0.6 the reply came back as zeros). The link is strong: RSSI -40 dBm, 2.4 GHz channel 11, 20 MHz wide, b/g/n/ax.

Raw receive, TCP unless noted, fresh unthrottled sender per run, Mbit/s:

| Configuration (cumulative unless noted) | Result |
|---|---|
| 3.0.6 both chips, C6 Wi-Fi profile, SDIO 20 MHz | 21.4 |
| P4 on 3.1.0 (local 3.0.6 patches skipped), C6 still 3.0.6 | 19.5 |
| P4 and C6 both 3.1.0 | 21.6 |
| + TCP/IP profile (TCP/UDP/tcpip mailboxes 64, TCP send buffer 65534, SACK) | 22.6 (about 23 on repeat) |
| UDP at 30 / 45 Mbit/s offered, UDP mailbox 6 (default) | 8.8 / 0.7 (drops inside the Tab5) |
| UDP at 30 / 45 Mbit/s offered, with the profile | 29.8 (0.5% loss) / 41.9 (7% loss) |
| + SDIO TX queue 16 | 22.7-23.1 (no gain) |
| + SDIO RX queue 8 | 23.5; UDP 29.6 / 42.5 (no gain) |
| + 11b/g/n only (no 11ax) | 22.0; UDP 29.8 / 42.9 (no gain, reverted) |
| + TCP window scaling, 128 KB window | 25.2-25.9 |
| + 256 KB window | 25.7-26.2 |
| + SDIO RX staging slots 4 | 25.6-25.9 (no gain) |

Findings: the radio and the SDIO bus carry at least 42 Mbit/s of UDP, so the earlier ceiling was the Tab5's own network
stack (the default 6-datagram UDP mailbox, a 64 KB TCP window). TCP is now about 26 Mbit/s and bounded by round-trip time
and residual loss (UDP shows 0.5% loss at 30 Mbit/s offered). The sender buffer size, SDIO clock (20 vs 40 MHz), SDIO queues
and staging slots no longer move the result. These configuration changes are not committed: the 3.1.0 trial skips the five local
3.0.6 Hosted patches (SDIO credit deadline, unresponsive-link failure event, aligned packet buffers, detached-task handle,
trampoline null-delete), which must be re-evaluated before 3.1.0 can ship; the 256 KB window and the 64 mailboxes need a
memory and soak check. 3.1.0 builds for the C6 with `orcdial/c6` against the registry package via an
`EXTRA_COMPONENT_DIRS` junction (see the trial commands in the session notes) and flashes with
`RTL_ORCDIAL_C6_UPDATE CONFIRM`.

### Throughput campaign, round 4: what moved TCP from 22 to 28-30 Mbit/s (2026-11-07)

The biggest single gain was `CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU1=y`: the lwIP task no longer shares a core with the
receiver, 26 -> 28.7-29.8 Mbit/s. `CONFIG_LWIP_IRAM_OPTIMIZATION=y` made it worse (19.7-21) and was reverted.

| Step (cumulative) | Sustained Mbit/s |
|---|---|
| 256 KB window, mailboxes 64, SDIO TX/RX queues 16/8, staging 4, SDIO 20 MHz | 25.7-26.2 |
| + lwIP task on core 1 | 28.4-29.8 |
| SDIO 40 MHz instead of 20 | 25.7-26.4 (worse) |
| SDIO 26 MHz instead of 20 | 28.2-28.8 (no gain) |
| 512 KB window instead of 256 KB | 28.2-28.3 (no gain) |
| Queues back to TX 4 / RX 2 / staging 2 | 27.1-27.8 (queues are worth about +2) |

Three 120 s runs (`RTL_TCP BENCH 120`, fresh unthrottled sender each) of the final settings:

| Host | Run 1 | Run 2 | Run 3 |
|---|---|---|---|
| ESP-Hosted 3.1.0 on both chips, local patches skipped | 29.0 | 29.5 | 30.2 |
| ESP-Hosted 3.0.6 with the five local patches, C6 3.0.6 with the Wi-Fi profile (the committed configuration) | 28.4 | 28.1 | 28.1 |

No errors, resets or drops in any run. A real V4 `rtl_tcp` FM stream on the final settings showed link 99%, 0 dropped.
DMA headroom fell from about 28.1 KB to 25.4 KB (`RTL_DRAM_BUDGET`). The 20 MHz SDIO clock was then soaked with the
earlier failure mode, a sustained HTTPS download (2026-11-07): `RTL_CATALOG_INSTALL faa_aircraft` (72.97 MB signed archive,
35.7 MB written to SD) ran twice back to back on Wi-Fi at 20 MHz SDIO; both finished with "Pack installed and verified"
(signature and hashes checked), no SDIO errors, no resets, no Hosted failure log lines. The second run took about 3 min
20 s including the SD write. The pack was removed afterwards (it was not installed before). Two runs on one link is
a pass, not a long soak; an overnight repeat would be stronger.
The 3.1.0 variant is not committed (it skips the local patches); its manifest and lock changes are kept as a patch in the
session scratchpad, and its C6 builds from the registry package with an `EXTRA_COMPONENT_DIRS` junction.

### Throughput campaign, round 5: 30+ Mbit/s sustained (2026-11-07)

The TCP bench's receive buffer grew from 16 KB to 64 KB (`rtl_tcp_source.cpp`): fewer reads per second removed per-call
overhead and was worth about +2 Mbit/s on its own. Three 120 s runs each, fresh unthrottled sender, no errors or drops:

| Host | Run 1 | Run 2 | Run 3 |
|---|---|---|---|
| ESP-Hosted 3.1.0 both chips, local patches re-based on 3.1.0, 16 KB buffer | 29.1 | 29.6 | 29.6 |
| same, 64 KB buffer | 31.3 | 31.6 | 32.2 |
| ESP-Hosted 3.0.6 with the five local patches, C6 3.0.6 Wi-Fi profile (committed), 64 KB buffer | 31.5 | 31.8 | 32.1 |

Conclusion: ESP-Hosted 3.1.0 is not needed to reach the 30-35 Mbit/s band, so the pinned 3.0.6 stays. The 3.1.0 port is kept
ready in `tools/patches/esp-hosted-3.1.0-*.patch` and `apply-esp-hosted-trampoline-fix.ps1` selects the patch set by installed
version: `detached-task-handle` is dropped (fixed upstream in 3.1.0), `trampoline-null-delete` and `sdio-unresponsive-failure`
apply unchanged, and the credit-deadline and aligned-buffer patches were re-based on the 3.1.0 SDIO source. To try 3.1.0, set
`espressif/esp_hosted` to `3.1.0` in `main/idf_component.yml`, rebuild the C6 from the registry package (an
`EXTRA_COMPONENT_DIRS` junction to `managed_components/espressif__esp_hosted`) and pass it with `-C6Firmware`.

Plateau check (same 3.0.6 configuration, 3 x 120 s each): a 128 KB bench buffer gave 31.3 / 31.5 / 31.4 Mbit/s (no gain over
64 KB); pinning the bench task to core 1, where lwIP runs, gave 24.9 / 25.5 / 25.9 (worse). The sustained ceiling on this
configuration is about 32 Mbit/s; 35 Mbit/s was not reached.

### Higher remote sample rates: scope (2026-11-07)

Correction to the earlier note here: the DSP is not limited to 960 kS/s. Locally, the wide bands (FM, AM, shortwave, WX,
CB, browse, airband) already run at 2.4 MS/s and ADS-B at 2.048 MS/s. The remote path is held at 960 kS/s on purpose by
one switch, `g_rtl_remote_narrow` in `rtl_default_sample_rate()`, written when the Wi-Fi link measured about 2 MB/s. The
link now sustains about 4 MB/s (28-32 Mbit/s), so that limit can move.

| Remote rate | Needs | Share of 32 Mbit/s | Span vs now | FM decimation |
|---|---|---|---|---|
| 960 kS/s (now) | 1.92 MB/s, 15.4 Mbit/s | 48% | 1.0x | 4 |
| 1.2 MS/s | 2.40 MB/s, 19.2 Mbit/s | 60% | 1.25x | 5 |
| 1.44 MS/s | 2.88 MB/s, 23.0 Mbit/s | 72% | 1.5x | 6 |
| 1.92 MS/s | 3.84 MB/s, 30.7 Mbit/s | 96% | 2.0x | 8 |
| 2.4 MS/s (local default) | 4.80 MB/s, 38.4 Mbit/s | 120% | 2.5x | 10 |

Recommendation: 1.44 MS/s is the practical ceiling (1.5x the span, 28% headroom for Wi-Fi dips). 1.92 MS/s has no
margin and 2.4 MS/s cannot be carried. Gain is modest, so this is optional.

Work items if approved (nothing started):
1. Make the remote wide-band default a value (1.44 MS/s) instead of the fixed 960 kS/s; keep ADS-B (2.048 MS/s, 32.8 Mbit/s)
   remote-excluded or narrowed.
2. Extend the `RateAdapter` ladder (1.44 -> 0.96 -> 0.48 -> 0.24 MS/s) and check the remote session accepts each rate.
3. Confirm the tuner and driver accept 1.44 MS/s (`esp_rtl_sdr_is_rate_supported`) and that the rtl_tcp server honours it.
4. Check the IQ ring (3 x 33280 B slots) and the FM/RDS decimation are correct at decimation 6, and the scope span cap.
5. Test: `RTL_TCP BENCH` at the new rate, a real V4 `rtl_tcp` FM run for 10 min (link %, drops, audio), then the
   dashboards. It is a visible dashboard behaviour change (wider span), so it needs the owner's go-ahead before it lands.
Risk: Wi-Fi and the DSP share core time, so higher rates can starve audio; the adapter step-down is the safety net.

### 1.44 MS/s remote wide bands with automatic fallback and recovery (2026-11-07)

Built and flashed (M5Stack Tab5, COM17). Wide dashboard bands now ask a remote server for 1.44 MS/s (`rtl_remote_wanted_rate()`
in `main.cpp`); other bands stay at 960 kS/s; ADS-B keeps 2.048 MS/s. `RateAdapter` (`rtl_tcp_protocol.hpp`) now uses an
explicit ladder 1.44 -> 0.96 -> 0.48 -> 0.24 MS/s (every rung a whole multiple of 240 kS/s) and also climbs back:

- Down: link under 85% for 4 samples (about 8 s) steps one rung and restarts the stream.
- Up: after 30 healthy samples (>= 95%, about 60 s) at a reduced rate it probes one rung up. A probe that falls behind
  again steps back down and doubles the wait before the next probe (up to about 16 min); full recovery resets the wait.
- Note: a server paced by the dongle always delivers about 100%, so the link ratio cannot show spare capacity; the probe
  is the only way to find out, which is why the backoff exists.

Measured with a real V4 `rtl_tcp` FM stream (96.1 MHz) from the PC:
| Test | Result |
|---|---|
| 1.44 MS/s, 60 s byte count | 2.88 MB/s (1.44 MS/s exactly), link 99-100%, 0 dropped |
| 1.44 MS/s, about 4.5 min | 786 MB received, 0 dropped, no reconnects, no rate change |
| Proxy paced to 1.5 MB/s | `RTL_TCP_RATE_DOWN link=52%` to 0.96 MS/s within about 8 s, then to 0.48 MS/s |
| Proxy unthrottled again | 0.48 -> 0.96 -> 1.44 MS/s on its own (about 2 min per rung after one failed probe) |

Host tests cover the ladder, the floor, off-ladder wanted rates, recovery, the 85-95% dead band and the probe backoff
(`tools/test-rtl-tcp.sh`, all pass). Not verified: audio by ear at 1.44 MS/s, the scope look at the wider span, other
bands' decoders on a remote stream, and a stream longer than about 5 min. Needs the owner's check of the wider scope span.

### Soak and status bar (2026-11-07)

Corrections first. Three earlier soak attempts were invalid and are not evidence about the Tab5: the PC `rtl_tcp` restarts
its reader on every reconnect and its V4 throws USB `-9` errors, so its delivered rate drifted (and 480 kS/s is not a valid
RTL2832 rate, the chip supports 225,001-300,000 and 900,001-3,200,000 S/s); a numpy FM generator and a constant-rate sender
either could not pace 2.88 MB/s or ignored the rate command. The valid test used `rate_server.py` (pre-made noise IQ, paced to
2 x the requested rate, honours the rate command).

Result on the Home screen, 1.44 MS/s, fresh adapter: 2.88 MB/s steady for about 24 min (1500 s test server limit), link 99-100%,
0 new dropped bytes, no rate change, no reconnect. About 12 `RTL_MAIN_STALL loop_gap` of 0.5 s per 3 min were logged (not
yet investigated; unknown whether USB mode shows the same).

Status bar: a remote stream now shows "RTL-TCP" in the Home header and footer, the header state is LIVE, BEHIND nn% or
CONNECTING, and the footer shows the stream's live rate (for example 1.440 MSPS) instead of "-- MSPS". Other dashboards' own
headers are unchanged. Screenshot verified on COM17 (M5Stack Tab5).
