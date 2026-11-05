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

Not yet done for the server: a test against an independent third-party client
(SatDump is installed on the PC; SDR++ and Gqrx are not), so **no compatibility is
claimed beyond the probe**. The Tab5 send path is limited the same way the receive path
was (lwip TCP send buffer 5760 B); a larger send buffer is being trialled.

## Not done yet

Settings screen (serial control only), a third-party server client test, remote
gain controls in the UI, a long soak, and a test against a non-Windows `rtl_tcp`
server.
