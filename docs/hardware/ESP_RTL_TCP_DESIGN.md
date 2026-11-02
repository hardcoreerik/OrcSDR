# esp-rtl-tcp: our own rtl_tcp for OrcSDR boards (revision 2)

Status: design only, 2026-10-30. No code. Owner decision: break down the original rtl_tcp and write our own `esp-rtl-tcp`; it is application-layer, never part of the
`esp-rtl-sdr` driver (the driver stays independent: no HTTP, WebSocket, Ethernet, Wi-Fi or rtl_tcp in it).

**Revision 2** replaces revision 1 after an independent research session (Codex, run in the RSA Sandbox and published to REA-Results) checked revision 1 against the
original rtl_tcp, two forks, three client programs and our driver's public API. Two of revision 1's rules were wrong and are removed; five more were incomplete. The
owner-approved decisions (section 8) are unchanged.

Research material (point-in-time, 2026-10-30, pinned commits inside):
`F:\Ai\RSA-Sandbox\targets\osmocom-rtl-sdr\notes\` (`rtl_tcp-breakdown.md`, `rtl_tcp-conformance-checklist.md`, `rtl_tcp-design-api-audit.md`, `rtl_tcp-clients.md`,
`rtl_tcp-fork-extensions.md`) and the same files in the private `REA-Results` repository (local commit `91be749`, not pushed). That analysis is offline only: no sockets,
real clients, hardware or RF were involved. I re-checked the facts the two blockers rest on directly against the pinned sources.

## 1. Provenance

The protocol and behaviour were read from the original rtl_tcp (osmocom/librtlsdr, GPL-2.0-or-later) and compared with two forks. They were read for what they do, not to copy:
`esp-rtl-tcp` is written from this behaviour description, the way `esp-rtl-sdr` is a clean-room driver. No third-party application code is used or merged.

## 2. The compatibility target (verified against the pinned original)

- **Header**, sent **once**, immediately after accept and before the sample stream starts: 12 bytes, magic `RTL0`, tuner type (u32 big-endian), tuner gain count (u32 big-endian).
  There is no rate, frequency or version field. A failed send is only logged in the original.
- **Commands** from the client: 5 bytes (command u8 + parameter u32 big-endian); no replies; unknown codes ignored. Baseline codes 0x01 to 0x0E are in the original and both forks.
- **Samples:** unframed unsigned 8-bit interleaved I/Q (CU8), exactly what the driver and `radio_service` produce. One client at a time.
- **Original's buffering:** a linked list of block copies with a count limit (default 500, the queue reaches about 503); the oldest data is dropped silently.
- **Forks differ.** The community fork adds 0x40 to 0x56 (bandwidth, tuner I2C, GPIO, extended frequency word, and others), declares 0x41 without handling it, and uses 0x42 to end the session;
  a disabled historical branch reuses 0x0E for bandwidth while all active builds use 0x0E for bias tee. There is no dialect or version marker, so a client's expectations cannot be inferred from the wire.

## 3. Rules (revision 2)

1. **The header is never resent.** Revision 1 proposed refreshing it on rate or frequency changes. That is impossible: the header carries neither value, and any extra `RTL0` bytes after
   streaming starts would arrive at the client as fake samples. Nothing besides raw CU8 goes on a legacy connection after the single header: no sequence numbers, timestamps or
   sink metadata (`IqBlock` fields stay internal). Settings, counters and the reason a session ended are reported only on the authenticated status channel
   (`RTL_TCP STATUS`, `/api/v1/rtltcp`).
2. **Sample rate: honour it or end the session, never substitute.** The console's 960 kS/s / 2.4 MS/s pair is application policy, not the driver's limit: the driver accepts
   225,001 to 300,000 and 900,001 to 3,200,000 S/s after hardware quantization (225,000 and 900,000 are rejected), which includes 2.048 MS/s. A legacy client has no way to learn
   that its rate was changed. So: a requested rate the driver can run and the board's link budget (section 6) can carry is applied; any other request **closes the session** and records
   the reason (`rate_unsupported` or `rate_exceeds_link`) in the status. Hardware quantization (the driver rounds to the exact rate it programs) is normal; replacing 2.048 MS/s with 2.4 MS/s is not.
   A rate change is one serialized transaction: take the receiver's control owner, stop, wait for clean stop, set the exact rate, restart at the intended centre and rate, bump the queue generation
   and discard queued blocks of the old generation before sending again. If stop fails, end the session; do not stream mislabelled data.
3. **Counted loss, bounded memory.** The sample path is a queued sink (IQ_SINKS_DESIGN.md) with a byte budget in PSRAM (default 1 MB, about 0.2 s at 2.4 MS/s; the unit and what counts toward
   it (queued and in-flight payload) are defined in the implementation). A slow client costs only its own blocks. Drops are whole blocks, counted (`blocks_sent`, `blocks_dropped`,
   `bytes_sent`, `max_queue_bytes`, `commands_ignored`, `commands_unsupported`, `clients_total`, `session_end_reason`), and never signalled in the byte stream. The sink callback receives a borrowed pointer
   that is invalid after it returns, so the queue stores its own copy of each block with its length; enqueue is all-or-nothing, and a partial socket write keeps an offset into the same block
   until it is sent or the session ends.
4. **Safe by default.** Off by default; enabled only from the authenticated serial console (later the signed web session); one client; optional client allow-list; every command that
   would change the receiver is logged. The original is unauthenticated and local-only by default, so on a LAN-facing board this is the main exposure.
5. **Listen-only is a fixed monitor endpoint.** Owner-approved default stays. Its contract: the session captures the receiver's centre, rate and gain mode at connect. Tuning commands are ignored and counted,
   which means an unmodified client will keep assuming they took effect, so the endpoint must be configured with matching client settings. If any other owner (a screen, the web UI, another
   control path) changes the centre frequency or rate during a session, **the session is ended** with `receiver_changed` rather than streaming data the client now misinterprets. Gain, AGC and bias changes
   do not change the interpretation of samples and do not end it. An explicitly enabled control mode accepts tuning commands under the arbitration in rule 6.
6. **Control arbitration (control mode only).** Commands pass through the receiver's existing control owner (the network task never touches USB itself). The most recent commander wins; the
   status shows who holds control. Frequency and rate commands are validated first (profile RF range, supported-rate and link checks), so a rejected request does not leave the receiver half-changed.
7. **Command mapping is explicit about what exists.** Unsupported commands are counted, never reported as applied:

| Code | Wire meaning | Behaviour |
|---|---|---|
| 0x01 | centre frequency (Hz) | `set_center_freq`; validate against the attached profile's range; the 0x56 high-word extension is not implemented (a nonzero staged high word is rejected) |
| 0x02 | sample rate (S/s) | rule 2 |
| 0x03 | gain mode (0 auto, nonzero manual) | normalised to AUTO/MANUAL; checked against capabilities |
| 0x04 | tuner gain (signed tenth dB) | nearest ladder step; forces manual; signed semantics after the endian conversion |
| 0x05 | ppm (signed) | accepted -200 to +200; outside is rejected, not wrapped or clamped |
| 0x06 | IF stage gain | no driver equivalent: unsupported |
| 0x07 | test mode | unsupported (no synthetic stream labelled as RF) |
| 0x08 | RTL2832 AGC | `set_rtl_agc` |
| 0x09 | direct sampling 0/1/2 | no arbitrary selector in the driver (low-band routing is automatic per profile): unsupported |
| 0x0A | offset tuning | unsupported |
| 0x0B, 0x0C | crystal frequencies | unsupported; never changed from the network |
| 0x0D | gain by index | index into the **connection-stable compatibility gain list** (rule 8), validated |
| 0x0E | bias tee | `set_bias_tee`, only if the attached profile has the capability and control mode is on |
| 0x0F, 0x40 to 0x56 | historical/community extensions | not in v1 (0x40 bandwidth is the first optional addition, valid only while streaming at 2.4 MS/s within profile limits); 0x42 is treated as unknown and does not uninstall the receiver |
| other | unknown | ignored and counted |

8. **Header values are defined, not borrowed.** The tuner type written in the header is mapped from the attached dongle profile to rtl_tcp's enum (the driver's enum values are different from rtl_tcp's:
   unknown 0, E4000 1, FC0012 2, FC0013 3, FC2580 4, R820T 5, R828D 6), and the gain count and the 0x0D index use one compatibility ladder built at connect and held for the session. The V4's measured gain
   ladder has a different length from the desktop R82x ladder, so an unchecked index into the live `radio::gain().steps` is not a compatible contract. If the attached device changes during a
   session, end it and require a reconnect.
9. **Failure behaviour.** A short or failed header send ends the session (the original just prints and carries on). End-of-stream, socket errors, no-data and slow-peer timeouts are explicit session ends with a
   recorded reason; waits are bounded. Reading a command is non-blocking with respect to the sender. Exact deadlines are an open decision (section 8).

## 4. Architecture

- `components/esp_rtl_tcp` (inside OrcSDR for now, own repository later), no dependency on OrcSDR. It owns the listener, the single header, the 5-byte command parser, the byte-budgeted
  send queue and counters, and a **control interface** the host application implements: set centre, set rate (honouring rule 2), gain mode, gain, ppm, AGC, bias tee, compatibility gain list, tuner type, current
  rate and centre, "receiver changed" notification. IQ arrives through `push(block)`.
- OrcSDR glue: an adapter over `radio_service` implementing the control interface, and `push` registered as a **queued sink**. Enable/disable and status are commands in the shared table
  (`RTL_TCP ON|OFF|STATUS`, `/api/v1/rtltcp`), so serial and the web UI see one state.
- Dependency direction as for the driver: `esp-rtl-tcp` never depends on OrcSDR; OrcSDR adapts.

## 5. Both directions (later)

The client half (a "virtual remote receiver" fed from another board's server) is not in the first version. When added, metadata the legacy wire cannot carry (rate changes, generations, sequence) needs a
**separate, versioned, non-legacy transport**, not extra bytes on the rtl_tcp connection. `radio_service` also needs a receiver backend that is not USB first.

## 6. Throughput (arithmetic, model assumptions in the research; not measured)

CU8 payload is `rate * 16 / 1e6` Mbit/s. Using IPv4+TCP over Ethernet framing as an explicit model input (`1538/1460` forward; a shared-link proxy adds ACK slots):

| Rate S/s | Payload Mbit/s | Ethernet forward (model) | 40 Mbit/s shared proxy (model) |
|---:|---:|---:|---:|
| 250,000 | 4.00 | 4.21 | 4.33 |
| 960,000 | 15.36 | 16.18 | 16.62 |
| 1,024,000 | 16.38 | 17.26 | 17.73 |
| 2,048,000 | 32.77 | 34.52 | 35.46 |
| 2,400,000 | 38.40 | 40.45 | 41.56 |
| 2,560,000 | 40.96 | 43.15 | 44.33 |

- 100M Ethernet (COM29, Waveshare ESP32-P4-ETH; no Ethernet driver in the console yet): two dongles at 2.4 MS/s need about 81 Mbit/s forward, so two fit and three do not. Gigabit is not available on these boards.
- Wi-Fi via the C5 on COM31 (Waveshare ESP32-P4-WIFI6-DB) sits behind a 10 MHz 4-bit SDIO bus (40 Mbit/s raw, the assumption stated; the OrcNode builds ran 40 MHz). 2.4 MS/s already exceeds it
  at the payload; 2.048 MS/s is under it only before real-world overhead; 960 kS/s is the first rate to test. These are planning numbers, not a prediction of the SDIO or Wi-Fi path, and rule 2 means a
  request above the budget ends the session instead of being rounded.
- A queue cannot cure a sustained link deficit: it trades RAM for latency before loss.

## 7. Tests (none run)

The full conformance checklist and fixtures (28 acceptance steps, 9 header fixtures, 34 command fixtures) are in the research notes above and are the starting test plan. Additional tests this design needs: the
header is sent exactly once and nothing else precedes the first sample; a rate change under each outcome (honoured, unsupported, over link budget) leaves the stream coherent or ends the session with the right
reason; a change by another owner ends a listen-only session; a slow reader produces counted drops and no receiver drops; partial writes preserve block integrity; reconnect; second client waits; unsupported
commands are counted and never reported applied; memory with a client attached; interoperability with at least gr-osmosdr and SDR++ against a real dongle; throughput per board with measured drops.

## 8. Decisions

**Approved by the owner (2026-10-30), unchanged:**
1. `esp-rtl-tcp` lives inside OrcSDR until it settles, then its own repository; written with no dependency on OrcSDR.
2. Listen-only by default; tuning commands are ignored (and counted) unless control is explicitly enabled.
3. First version is the server half only; the client half comes later.
4. Licence: AGPL-3.0, like the driver.
5. Credit goes in the commit and merge message only; no code or docs name anyone; no third-party code is used or merged.

**Proposed by revision 2, approved by the owner 2026-10-30 ("yes agreed"):**
6. Rate policy: honour supported requests, otherwise end the session with a recorded reason (rule 2).
7. Listen-only coordination: end the session when another owner changes centre or rate (rule 5).
8. Compatibility gain list and tuner-type mapping held for the session (rule 8).
9. Session deadlines (idle, slow-peer, no-data): approved in principle; the actual values are still to be chosen from measurements on a board.

**Owner direction (2026-10-30):** an ESP32-optimised rtl_tcp is a high-value feature for OrcSDR. Code comes after the sink fix (done on `claude/p4-board-support`,
commit `636991a6`, build-verified only, not yet run on a board) because the server is a queued sink.

## 9. Implementation status (2026-11-02): server half written and built, not flashed, not run against a client

Code: `components/esp_rtl_tcp` (server, protocol core, host test), `main/rtltcp_app.{hpp,cpp}` (OrcSDR adapter), table commands in `main/commands.cpp`. Builds for the Waveshare ESP32-P4-ETH (`build-p4eth-web`, 3.6 MB, 50 % of the app slot free)
and the Waveshare ESP32-P4-WIFI6-DB (`build-web-ui`). Run so far: `tests/proto_test.cpp` on a host (header bytes, command reassembly at every split point, classification, 4 checks groups): all passed.
Nothing has run on a board and no rtl_tcp client has connected.

How it follows the rules:
- **Rule 1 (header once):** the header is sent right after accept; `push()` sends nothing but the block bytes; counters live only in `RTL_TCP STATUS` and `/api/v1/rtltcp`.
- **Rule 3 (bounded, counted loss):** the feed is a *queued sink* of the receiver service with a 1 MB PSRAM budget; a slow client makes the service drop whole blocks and count them (`blocks_dropped`). `push()` runs in the sink's own task, so a stalled socket never touches the radio task.
- **Rule 4 (safe by default):** off unless `RTL_TCP ON` (authenticated serial, admin tier, no HTTP path); listen-only unless `RTL_TCP CONTROL ON`; one client, the second waits in the listen backlog; an optional single allowed client address exists in `Config` but has no command yet.
- **Rule 5 (listen-only is a fixed endpoint):** the first block of a session defines the rate and centre the client was told; a later block with another value ends the session (`receiver_changed`). Receiver-changing commands are counted as `commands_ignored`.
- **Rules 7/8 (mapping, header values):** codes 0x06 0x07 0x09 0x0A 0x0B 0x0C are `commands_unsupported`; 0x0F and 0x40 up are `commands_unknown`; the tuner type comes from the dongle profile (V4 and V4L: R828D, V3 and NESDR v5: R820T, unknown: 0), and one gain ladder is snapshotted per session for 0x04 (nearest step) and 0x0D (index).
- **Rule 2 (rate):** control mode accepts 225,001 to 300,000 and 900,001 to 3,200,000 S/s through the receiver service's exact-rate override; anything else ends the session (`rate_unsupported`), as does a rate above the link budget (`rate_exceeds_link`).
- **Open / provisional:** the link budgets (85 Mbit/s wired, 30 Mbit/s Wi-Fi) are guesses; the send deadline is 2 s (design decision 9 still wants a value from measurement); the override is receiver 0 only; control mode has never been run; the serving receiver is fixed at 0;
  the Wi-Fi path through the SDIO coprocessor is expected to need 960 kS/s. The receiver service gained `Info::profile` for the tuner mapping.
- **Test to run first:** `RTL_TCP ON` over serial, then `python tools/p4_rtltcp_test.py <ip>` (header, rate, CU8 sanity, counting of ignored/unsupported/unknown commands, a split command, second client waits, end reason). Then SDR++ and gr-osmosdr against a real dongle.

## 10. First runs on boards (2026-11-02, owner asleep, COM29 and COM8 cleared for testing)

Client: `tools/p4_rtltcp_test.py` (Windows PC, Python), a dongle streaming at 2.4 MS/s. Build overlay `sdkconfig.net-throughput` (TCP send buffer and window 32 KB, 16 sockets) is on for both boards below; the default 5,760-byte window is what lwip ships.

**COM29, Waveshare ESP32-P4-ETH, wired 100 Mbit, NESDR SMArt v5 (tuner type 5, gain count 29).** Listen-only. 14 of 14 acceptance checks pass:
header exactly once, no `RTL0` in the stream, CU8 samples, tuning command counted as ignored, unsupported and unknown commands counted, a command split across two TCP segments reassembled, a second client waits and is served after the first leaves.
Throughput: 60 s at 2,399,391 S/s, no new dropped blocks. 240 s with a 1 MB queue: 2.26 MS/s average, 5 % of blocks dropped (counted, receiver drops 0); with a 4 MB queue: 1.15 GB sent, 104 blocks dropped (0.15 %).
With the default 5,760-byte window the same link lost about 3.5 % in 10 s and ended a session as `slow_peer`. Internal RAM free fell from 140.9 KB (web only) to 127.1 KB with the larger windows and the server. The web page and login test (25 checks) pass on the same image.
Open: a few `status` requests over HTTP made while a client streamed were reset by the server (the test now retries and says so); curl against the same endpoint during a stream always answered in about 55 ms. Cause not found; the browser pane polling the same board is one suspect.

**COM8, Waveshare ESP32-P4-Module-DEV-KIT, Wi-Fi through the ESP32-C6 (ESP-Hosted), Blog V4L (tuner type 6).** 13 of the 14 acceptance checks pass; the sample-rate check fails (below). The 25 web login checks pass over Wi-Fi.
- Streaming 2.4 MS/s over Wi-Fi: about 137 kS/s with the default windows, 197 to 214 kS/s with 32 KB windows, with or without Wi-Fi power save (turned off for the session; round trip while idle 35 to 150 ms).
  So the stream is not window-bound or power-save-bound: it gets about 3 to 4 Mbit/s. Burst downloads of a 393 KB file from the same board over Wi-Fi ran at 11 to 16 Mbit/s (wired COM29: 78 Mbit/s). Why the live stream gets a fraction of the burst rate is unexplained (CPU, SDIO contention with the USB receiver and the screen are candidates).
- Control mode (`RTL_TCP CONTROL ON`): a client's `set sample rate 250000` was applied (the session rate became 250,000 and about 180 to 250 kS/s arrived); 2,400,000 ended the session as `rate_exceeds_link`; 500,000 as `rate_unsupported`; a connect during the receiver's restart after a rate change was refused (`host_refused`);
  a mixed run applied 7 commands and rejected 2 (ppm 500 and gain index 99); the receiver returned to 2.4 MS/s when the session ended. A rate of 960 kS/s passed the (then guessed) 30 Mbit/s budget and could not be carried: the budget for Wi-Fi was lowered to 12 Mbit/s, which allows only the 225 to 300 kS/s range.
- **Consequence:** over this Wi-Fi path the useful rtl_tcp rate is about 250 kS/s (fine for narrowband work, not for wide spectrum). Wired Ethernet carries 2.4 MS/s.

## 11. Why the Wi-Fi stream is slow (COM8, 2026-11-02, second look)

The gap is the Wi-Fi link itself, not rtl_tcp: a plain HTTP download of a 393 KB file in a loop from COM8 over Wi-Fi, with no rtl_tcp and no client stream, sustains only 4.9 to 5.3 Mbit/s (bursts of one file reach 11 to 16). Idle ping averages 53 ms with a minimum of 4 ms and a maximum of 113 ms.
During the rtl_tcp stream the rtl_tcp task used 0.8 % of a core, the SDIO tasks 0.6 to 2.4 % each, and the radio task 80 % of core 1, so the send path is waiting on the network, not on CPU. The home screen task used 75 % of core 0.
Not determined: whether the ceiling is the SDIO link, the C6 firmware (stock ESP-Hosted slave 3.0.6), the 2.4 GHz air link to this router (signal strength is not reported, `rssi=0`), or the PC side. A wired board reaches 78 Mbit/s with the same code.
The Wi-Fi budget in the code is therefore 5 Mbit/s (it was 30, then 12), which admits only the driver's 225 to 300 kS/s range. Ideas to try later: read the real RSSI and PHY rate from the C6, test from a second Wi-Fi client, compare a Tab5 (same C6 path, not touched here).

## 12. Soak on COM29 (Waveshare ESP32-P4-ETH, wired), 2026-11-02, 40 minutes

One rtl_tcp client (listen-only) streaming the NESDR SMArt v5 at 2.4 MS/s, plus two web requests every half second, the whole time. Memory logged every minute.
- Streamed 11.5 GB in one unbroken session (sessions = 1), 3,588 web requests, 0 failed, receiver drops 0 throughout.
- Internal RAM free stayed at 121,507 bytes at every sample (one dip of 324 bytes and one of 260 bytes that recovered); PSRAM free moved by tens of KB around 27.69 MB while the 4 MB sink queue was in use. No leak visible in 40 minutes.
- Blocks dropped for the client: 901 of about 700,000 (0.13 %), all counted, arriving in a few bursts (for example 48 to 519 within one minute at about 20 minutes). The client was a Python script on a Windows PC, so a stall on that side is as likely as one on the board; not separated.
- After the soak and `RTL_TCP OFF`, internal free read 133,323 bytes (server idle) and the receiver streamed normally.

## 13. Counters no longer wait behind a send (2026-11-02)

The server's counters and the end-of-session flag moved off the lock that `push()` holds during a blocking send (a spinlock for the counters, an atomic for the end reason and the "first block seen" flag), so commands are handled and counted promptly even when a slow link has a send in flight.
Checked: on COM8 (Waveshare ESP32-P4-Module-DEV-KIT, Wi-Fi) the acceptance test now passes every check except the sample-rate one (410 kS/s arrived of 2.4 MS/s, about 6.6 Mbit/s with the stream task at priority 5, so that link carried more than the 4 Mbit/s measured before); previously four command-counting checks failed there.
On COM29 (Waveshare ESP32-P4-ETH, wired): 20 s at 2,397,683 S/s, 0 dropped blocks, 14 of 14 checks (0 failed). rtl_tcp is off on both boards again.
