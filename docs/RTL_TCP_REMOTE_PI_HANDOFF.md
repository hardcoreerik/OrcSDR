# rtl_tcp to a remote Raspberry Pi: research handoff and plan

Planning document, written 2026-11-07. No product code is changed by this page.

## Research record (find it again from here)

- Private repo: `hardcoreerik/REA-Results`, branch `research/remote-rtl-tcp-20261107`, commit
  `b1e64b2d231495114ee8025c04aac347b17b4f8c`.
- Report: `targets/osmocom-rtl-sdr/notes/remote-rtl-tcp-2026-11-07.md` (cited below as "R §n").
- Findings: `targets/osmocom-rtl-sdr/findings.md` (cited as "F").
- Known metadata issue: `RESEARCH.md` links the older `notes/report.md`; the report above is the deliverable.
- Evidence level (R caveats): source-derived and arithmetic. No Pi, dongle, Tab5 or WAN path was used, so throughput,
  thermal and latency figures are estimates. The compression benchmark is synthetic.

## 1. What the research changes about the design

1. **The default for a remote Pi should be 240 kS/s, not our current 1.44 / 0.96 MS/s.** 240 kS/s is 3.84 Mbit/s against
   23 Mbit/s at 1.44 MS/s; the research calls it "the single most effective change" (R §7.2) and the right rate for NBFM, AM
   and HF (R §7.1). Our current wide-band default suits a LAN to a PC, and is the wrong starting point over a WAN.
2. **1.024 MS/s is not usable as the research suggests.** It is not a whole multiple of the 240 kS/s demodulation rate, and the
   current DSP chain assumes multiples (the ladder in `RateAdapter` exists for that reason). The equivalent optional rung
   for us is 0.96 MS/s (valid RTL2832 rate, 15.4 Mbit/s). Open question 2.
3. **The server queue is the real latency, and we cannot see it.** A stock server holds up to 503 blocks of 256 KiB (R §3.3);
   one block is 546 ms at 240 kS/s and 15 USB buffers are 8.19 s. A slow link makes the picture fall behind real time with no
   loss metadata on the wire. Our `link_ratio_pct` and the rate ladder are the right detector; the research confirms there is
   nothing better on the wire.
4. **The server keeps state across reconnects, so a fresh connection can start with someone else's settings** (R §3.2 `-f` is
   "not reset on reconnect"; F). Our replay only sends fields the UI has touched (section 3), so a reconnect to a Pi that was
   left in a different state is not fully defined.
5. **Safety:** rtl_tcp is plaintext and unauthenticated; use WireGuard/Tailscale or an SSH tunnel, bind to the VPN or loopback
   address, never port-forward 1234 (R §8, §9.3). This is a user-facing setup instruction, not client code.

## 2. Connection and header state machine

Matches `ui/rtl_tcp_session.cpp` today; the points marked NEW are gaps.

1. **Connect:** non-blocking `connect()` with a stop check every 100 ms, timeout 5 s (`connect_to`). Sets `TCP_NODELAY`
   (R §9.5 recommends it), receive timeout 200 ms, send timeout 1 s. `SO_KEEPALIVE` is not set (NEW, optional).
2. **Header:** loop until 12 bytes (`GreetingParser`, `rtl_tcp_protocol.hpp`); wrong magic is refused at the first byte; timeout
   10 s; a close before 12 bytes is `greeting_failed`. IQ bytes that arrive in the same read are kept (`lead`). This satisfies
   R §3.4 ("loop until 12 bytes").
   - No header within the timeout means "busy or not rtl_tcp" (R §3.3: one client at a time). Today the reason shown is
     "not an rtl_tcp server". NEW: say "no greeting: server busy or not rtl_tcp" so a Pi that already has a client is
     not mistaken for a wrong address.
3. **Header facts to record:** tuner enum (5 R820T, 6 R828D) and gain count; there is no rate, frequency or gain table (R §3.4).
4. **Replay desired state** (section 3), then enter `streaming`.
5. **Stream:** raw CU8, no midstream header after retune or rate change (R §3.4, §9.5). Commands are written between reads from a
   bounded queue (depth 16).
6. **Failure:** `closed_by_peer`, `stalled` (3 s without IQ), `socket_error` all reconnect with 0.5 s doubling to 15 s, 8
   attempts (`BackoffPolicy`). A stream that lasted 2 s resets the back-off. A reconnect calls `discontinuity()` so DSP knows the
   IQ is not contiguous.

## 3. Command writer and full-state replay order

Frames are exactly 5 bytes, opcode plus big-endian u32, no ACK, no error reply (R §3.5). `encode_command()` and `send_all()`
already do this; there is no way to confirm a command applied, so none of the UI may claim it did.

Current replay (`send_desired`, `rtl_tcp_session.cpp`): `0x02` rate, `0x01` frequency, `0x03` gain mode, `0x04` gain, `0x08` RTL
AGC, `0x05` ppm, `0x0A` offset, and each of the last five only if the UI has set it.

Proposed replay on every (re)connect, in this order:

| # | Opcode | Value | Policy |
|---|---|---|---|
| 1 | `0x02` rate | current ladder rate | always (R §9.5: server may not have applied it, wire cannot confirm) |
| 2 | `0x01` frequency | desired | always |
| 3 | `0x03` gain mode | 0 or 1 | always (NEW: today only if the UI set it) |
| 4 | `0x04` gain | tenths of dB, signed | only when manual |
| 5 | `0x08` RTL AGC | 0/1 | always (NEW) |
| 6 | `0x05` ppm | signed | only if the user set a value |
| 7 | `0x09` direct sampling | 0/1/2 | only if the user selected it (NEW, see section 5) |
| - | `0x0A` offset tuning | never on R820T/R828D | see section 5 |
| - | `0x0E` bias tee | never automatic | see section 5 |
| - | `0x0D` gain by index | not used | see section 4 |

Rate before frequency keeps the existing order. Whether the library retunes on a rate change in a way that matters here is
unmeasured (open question 5).

## 4. Gain policy

- Use absolute gain `0x04` (tenths of dB). The header carries only a gain count, so an index (`0x0D`) cannot be mapped to dB
  without a table (R §3.4, §3.5). `0x0D` only with a verified per-tuner table; not in v1.
- Gain mode `0x03` and RTL AGC `0x08` are separate controls (R §3.5). `-g 0` on the server means tuner AGC, not 0 dB (R §9.4).
- Our `ui/rtl_source_routing.hpp` already routes the three gain calls to the remote while it streams. The gap is the initial
  state (section 3 rows 3 and 5).

## 5. Rate, bias tee, offset and direct-sampling policy

**Rate** (R §7.1): valid 225,001-300,000 and 900,001-3,200,000 S/s; 225,000 and 300,001-900,000 are rejected; sample loss is
expected above 2.4 MS/s.

| Profile | Rate | Payload | Notes |
|---|---|---|---|
| Remote (WAN), default | 240 kS/s | 3.84 Mbit/s | narrowband bands; the bottom rung already exists |
| Remote, optional | 960 kS/s | 15.4 Mbit/s | our substitute for 1.024 MS/s; confirm DSP use |
| LAN (today's default) | 1.44 MS/s wide, 960 kS/s other | 23 / 15.4 Mbit/s | keep; stays the ladder top on a LAN |
| 2.4 MS/s | warned | 38.4 Mbit/s | R §1: over a 30 Mbit/s link this cannot work; ADS-B is the only band that needs 2.048 |

NEW: a link profile setting (LAN / Remote) that chooses the ladder start. The adapter itself already steps down and probes up
(`RateAdapter`).

**Bias tee `0x0E`:** never sent automatically and never switched by a remote client (also our server's rule). If it is ever
exposed for a remote Pi it needs a confirmation naming the risk, because a DC-short antenna can damage the dongle (R §8.3,
S18). Open question 1.

**Offset tuning `0x0A`:** do not send for R820T/R828D (greeting tuner 5 or 6); the Blog fork can toggle bias as a side effect
(R §3.6). No setter exists in the client today, which is correct.

**Direct sampling `0x09`:** the local path picks direct or upconverter routing from the USB dongle's profile
(`ui/main.cpp`, `!remote` branch). A remote Pi gives no profile, only the tuner enum. HF on a remote stock server needs
`0x09` chosen by the user, or a Blog server that routes HF itself (R §3.6). v1: do not auto-send; show HF as "depends on the
server's dongle". Open question 4.

## 6. Gap analysis

**Already in place:** 12-byte greeting parser with early refusal; 5-byte encoder; odd-byte IQ carry (I stays first); full-state
replay (partial); bounded command queue; non-blocking connect; `TCP_NODELAY`; send/receive timeouts; stall detection;
bounded exponential back-off; DSP handoff through a PSRAM ring with counted drops and a short wait instead of blocking
(`kRingWaitMs`); rate ladder with step-down and recovery; link ratio; Home status of RTL-TCP/LIVE/BEHIND; server is off by
default and refuses 0.0.0.0.

**Missing:** link profile (LAN/Remote) and a 240 kS/s default for narrowband remote; replay of gain mode and AGC; direct-sampling
command and its UI; a "busy server" reason; documented Pi server setup in-app or in docs.

**Wrong or doubtful:**
- Remote defaults assume a LAN (R §6.2 estimates 1.2 MS/s as the Pi Zero 2 W Wi-Fi ceiling and 2.4 MS/s wired).
- Replay leaves unset fields to the server's previous state (R §3.2, F).
- 1.024 MS/s in the research does not fit the 240 kS/s demodulation multiples.

**Out of scope for v1:** compression (R §7.3: about 1.0x on noise-like IQ), server-side decimation (SpyServer/sdr-server,
R §4), SoapyRemote and rtl_rpcd (different protocols, R §10 q11), community fork extensions `0x40+`, gain by index, bias tee
control, authentication or TLS inside the client (R §8: use a tunnel).

## 7. Tasks

| # | Task | Files | Risk | Test |
|---|---|---|---|---|
| T1 | Link profile LAN/Remote chooses ladder start; 240 kS/s default for narrowband remote | `ui/rtl_tcp_protocol.hpp` (`RateAdapter`), `ui/rtl_tcp_source.cpp`, `ui/main.cpp` (`rtl_remote_wanted_rate`, `rtl_default_sample_rate`), Settings row | medium: touches rate selection and dashboards' decimation | extend `tests/rtl_tcp_protocol_host_test.cpp`; Tab5 stream at 240 kS/s per band |
| T2 | Replay gain mode and AGC explicitly; keep gain only when manual | `ui/rtl_tcp_session.cpp` (`send_desired`), `ui/rtl_tcp_session.hpp` (`Desired`) | low | extend `tests/rtl_tcp_session_host_test.cpp`: check byte order of the replay |
| T3 | "Busy or not rtl_tcp" end reason | `ui/rtl_tcp_session.cpp/.hpp`, `ui/rtl_tcp_source.cpp` | low | session test with a server that accepts but sends nothing |
| T4 | Optional `SO_KEEPALIVE` | `ui/rtl_tcp_session.cpp` (`connect_to`) | low; lwIP option support must be checked | host loopback test; Tab5 build |
| T5 | Direct sampling `0x09` on explicit user choice | `ui/rtl_tcp_session.*`, `ui/rtl_tcp_source.*`, HF band UI | medium | encode test; real HF check needs a V3 behind a stock server |
| T6 | Pi server setup page (WireGuard/Tailscale, `-a`, `-n 32 -b 2`, `-T` warning) | `docs/` | none | review against R §9 |
| T7 | Measure the server queue and reconnect backlog on a real Pi | scripts under `tools/` | none | R §10 q1, q15 |

**Blockers:** T5 and T7 need a real Pi and dongle (none available to the research or to this lab as of today); T1 needs Erik's
decision on open question 2.

## 8. Test plan and acceptance criteria

1. Host: greeting split across 1, 3 and 11 bytes plus IQ in the same read; replay byte sequence; busy-server timeout; step
   ladder; all under `bash tools/test-rtl-tcp.sh` (existing).
2. Tab5 against `rate_server.py` (lab stand-in): remote profile starts at 240 kS/s on an AM and an NBFM band; no `dropped`
   bytes over 10 min; `RTL_TCP STATUS link` 95-105%.
3. Reconnect: kill and restart the server; the stream resumes with a `discontinuity`, the replayed commands arrive in the
   section 3 order (check on the server side).
4. A server that was left in manual gain with bias on: a new connection must not leave the UI claiming AUTO.
5. Acceptance for v1: a Pi reached through WireGuard or Tailscale streams NBFM at 240 kS/s for 1 hour with 0 dropped bytes,
   LIVE on every dashboard header; stepping the link down never leaves the picture behind real time for more than the ladder's
   8 s detection window.
6. Not testable here: Pi sustained rates, WAN latency, USB power (R §6, §10).

## 9. Open questions for Erik

1. Bias tee for a remote Pi: never expose it, or expose it behind a confirmation? Should the client ever send `0x0E 0` on
   connect to clear a stale "on" (it cannot know the state)?
2. Replace the research's optional 1.024 MS/s with 960 kS/s because of our 240 kS/s multiples?
3. Should the Remote profile be chosen automatically (host is not a private LAN address) or only by a setting?
4. HF on a remote Pi: user-selected direct sampling only, or detect a Blog/V4 server and trust its routing?
5. Is it acceptable to test on the real Pi when it is available (Frikkie's), or do we need a Pi of our own first?
6. Does a reconnect to a stock server start with a clean queue, or does it deliver the old backlog (R §10 q15)? Matters for
   whether reconnect-on-step-down really flushes.

## 10. What I did not verify

I read the report and findings. I did not re-derive its arithmetic, run any server, or test the client against a stock
`rtl_tcp` on a Pi. Repo statements above come from reading the files on this branch today.

## 11. Prompt claims checked against the report (2026-11-07)

The handoff prompt listed "non-negotiable protocol facts". Each was checked against the report at commit `b1e64b2d`. Status is
what the report supports, not independent confirmation.

| Prompt claim | Report support |
|---|---|
| 12-byte greeting `RTL0` + BE tuner + BE gain count | Confirmed, R §3.4 (source `rtl_tcp.c:79-83,618-639`) |
| Client must loop to 12 bytes | Supported: the server does one `send()` and ignores short writes, so fragmentation is possible, R §3.4 |
| Commands are 5 bytes, opcode + BE u32; no ACK, ID, sequence or timestamp | Confirmed, R §3.5 |
| No tuner-bandwidth or decimation command | True for upstream, Blog and Debian. **Not true for the community fork**, which adds `0x40` tuner bandwidth (R §3.5). Irrelevant for a stock Pi, relevant if the Pi runs the fork. |
| No midstream header after retune or rate change | Stated in R §9.5 and F as source-derived (C), not measured |
| Server keeps state across reconnects | R §3.2 (`-f` not reset on reconnect). Rate, gain and bias persistence on the server is implied by the library, not tested |
| One client at a time; no header means "busy" | One client: confirmed, R §3.3 (`listen(...,1)`). **"No header means busy" is not established**: R §3.3 says OS behaviour for a second client is unverified (**U**) and R §10 q15 lists it as open |
| Slow client: queue growth, oldest-block drops, no loss metadata | Confirmed, R §3.3 (503-node derivation) |
| 546 ms at 240 kS/s, 8.19 s for 15 buffers, `-l 32` = 16 KiB about 34 ms | Confirmed, R §3.3 table. The report notes the community source comment is off by a factor of two |
| Valid rates 225,001-300,000 and 900,001-3,200,000; loss above 2.4 MS/s | Confirmed, R §7.1; the loss claim cites an external warning (S23), not a measurement |
| Plaintext and unauthenticated; tunnel, never port-forward 1234 | Confirmed, R §8.1, §8.3 |
| Do not use `0x0A` on R820T/R828D | Supported: it returns an error, and the Blog fork toggles bias as a side effect, R §3.6 |
| 2.4 MS/s is 38.4 Mbit/s | Arithmetic, R §1 and §7 |

Net: the prompt's facts hold, with two corrections to carry forward: the community fork does have a bandwidth command, and
a missing header is only a hypothesis for "server busy".

## 12. First measurements from the Pi session (2026-11-07, loopback only)

Reported by the Pi provisioning session before the Pi had a dongle or SSH access. A real RTL-SDR Blog V4 (R828D) on the Windows
PC served a real `rtl_tcp` on 127.0.0.1 with the planned flags. These are server behaviour numbers, **not link measurements**:
no Ethernet, no Pi, no WAN. Raw files are with that session, not in this repo. Status of each is as that session reported it.

| Check | Result | Answers |
|---|---|---|
| Greeting | `52544c30 00000006 0000001d`: tuner 6 (R828D), 29 gains, byte-for-byte the R §3.4 prediction | confirms R §3.4 |
| Throughput | 240 k, 960 k, 1.024 M, 1.44 M and 2.4 MS/s all 99.9-100.5% of expected; 2.4 MS/s was 4,799,868 B/s (38.40 Mbit/s), no USB overruns | the dongle and server deliver 1.024 MS/s fine, so any 1.024 limit is in our DSP |
| Backlog after a 30 s stall | `-n 32 -b 4`: 20.07 s of backlog; stock defaults: 29.49 s | R §10 q1 (queue depth, loopback) |
| Reconnect after the stall | 0.00 s of stale data delivered, in both configurations | **R §10 q15 and our question 6: a reconnect flushes** |
| Second client | TCP connect completes, no header, waits; gets a correct header as soon as the first client leaves | "busy, not refused": a missing header does mean busy on this server |
| Persistence | manual 40.2 dB survived a reconnect (RMS 27.13 to 28.22); the 0 dB run was inconclusive at the quantisation floor | R §3.2 partly confirmed for gain |

Effects on this plan:

1. Reconnect-on-step-down flushes the server backlog (measured on this server build; a different build is untested). The
   Tab5's step-down by reconnect is therefore sound, and open question 6 is answered for this server.
2. A connected socket with no header is a reliable "server busy" signal here, so task T3 (a "busy" end reason) is justified.
   The greeting timeout (10 s) should not be shortened below the time a waiting client may legitimately wait.
3. **`-n` and `-b` are easy to read backwards.** From the binary's own usage text: `-b` is the number of USB transfer buffers
   (default 15), `-n` is the maximum network queue (default 500). With `-n 32 -b 4` the network queue is about 19-20 s and the USB
   buffering about 2.18 s. The queue drops at `-n` plus 3 nodes (R §3.3), so `-n 32` is 35 nodes, 19.1 s by arithmetic, against
   17.5 s in R §9.4; the measured backlog was 20.07 s. This belongs in the T6 setup page: to reduce latency, lower `-n`
   (for example `-n 8`), not only `-b`.
4. Any stream the Tab5 can see is at most the queue depth behind real time after a stall, so the ladder's 8 s detection window
   does not by itself bound the delay. A small `-n` does.
5. The Pi in the lab is reachable by the PC over Tailscale (a 100.x address). The Tab5 is not known to be on that network. A Tab5
   test needs the Pi's LAN address and `rtl_tcp` bound to it (not 127.0.0.1); neither is configured yet.

## 13. Tests run against the real Pi 4B (2026-11-07, by the OrcSDR session)

Pi: Raspberry Pi 4 Model B on Debian 13, `rtl-sdr` 2.0.2-2, dongle "Blog V4L" reporting tuner 5 (R820T), service
`rtl_tcp -a 192.168.1.218 -p 1234 -s 240000 -n 32 -b 4` as user `rtl`. Client: this Windows PC (Python), and the Tab5.
Raw numbers are in the session scratchpad (`pi_raw/`), not in this repo. Everything below is measured unless marked.

**Pi state during all runs.** `get_throttled=0x50005` throughout: under-voltage and throttling are *live* (not stale), and the
kernel logged "Undervoltage detected" 13 s after boot, after the power supply was replaced. The Pi also runs other things
(`indi-allsky` camera app, a Docker `ground-station`, `ollama`, `indiserver`; load average 3.4-4.2). None were stopped. Despite
this, no run lost data over Ethernet or Wi-Fi. Long-run stability under the power fault is NOT shown (runs were 8-25 s).

**Throughput, no impairment** (12-15 s per rate, bytes/s against 2 bytes per sample):

| Rate | Ethernet | Wi-Fi (`wlan0`, policy-routed) |
|---|---|---|
| 240 kS/s | 100.7% | 100.0% |
| 960 kS/s | 100.2% | 99.2% |
| 1.024 MS/s | 100.1% | not run |
| 1.44 MS/s | 100.0% | 100.0% |
| 2.048 MS/s | 100.1% | 100.0% |
| 2.4 MS/s | 100.1% (38.43 Mbit/s) | 100.0% (38.41 Mbit/s) |

The Wi-Fi runs were confirmed to use `wlan0`: its transmit counter rose 7.4, 29.8, 44.7, 63.5 and 74.4 MB across the five runs.
So the Pi, the dongle and `rtl_tcp` all deliver 1.024 MS/s; any limit on 1.024 MS/s is in OrcSDR's DSP.

**Behaviour** (Ethernet):
- Greeting `52544c30 00000005 0000001d` (R820T, 29 gains). The server needs about 1.65 s after a client closes before it
  serves the next: header latency was 26 ms (first), 1148 ms (0.5 s gap), 624 ms (1.0 s), 138 ms (1.5 s), 25 ms (2.5 s).
- Second client: the TCP connect completes, no header for 4 s, then a header 349 ms after the first client closed.
- Gain persistence: manual 49.6 dB survived a reconnect with no commands sent (RMS 30.8 before, 31.67 after, against 0.5 at 0 dB).
- An unknown opcode (`0x7f`) was ignored and the stream kept flowing (493 kB/s). A request for the invalid rate 480000 was silently
  ignored: the stream stayed at about 485 kB/s, the 240 kS/s rate. There is no reply either way.
- 25 s stall then resume: 20.09 s of backlog (`-n 32`); a reconnect after the stall delivered 0.00 s of stale data.

**Simulated internet path.** The path was Tab5 or PC client to a PC-side forwarder, an SSH tunnel over Tailscale to the Pi, then
`rtl_tcp`. Tailscale reported a *direct LAN* path, so this was not a real internet traversal; the internet-like conditions were
imposed with `tc netem` on the Pi's `tailscale0` egress and removed afterwards. Receiver = PC client:

| Profile (netem) | 240 kS/s | 960 kS/s | 1.44 MS/s |
|---|---|---|---|
| 40 +-10 ms, 0.3% loss, 30 Mbit/s (BBR) | 100.6% | 51.6% | 31.3% |
| 80 +-20 ms, 1% loss, 10 Mbit/s (BBR) | 52.4% | 8.9% | 6.4% |
| 60 +-15 ms, 0.5% loss, 3 Mbit/s (BBR) | 29.6% | 10.6% | 5.7% |
| 40 +-10 ms, 0.3% loss, 30 Mbit/s (CUBIC) | 100.5% | 35.6% | not run |
| 80 +-20 ms, 1% loss, 10 Mbit/s (CUBIC) | 52.8% | 10.7% | not run |

The Pi was already using BBR; CUBIC was set temporarily and BBR restored. Packet loss, not the bandwidth cap, is what limits TCP here:
with 0.3% loss and 40 ms the 30 Mbit/s profile still delivered only about 7 Mbit/s at the higher rates. BBR helped somewhat at
0.3% loss and not at 1%.

**Tab5 on the lossy path** (40 ms, 0.3% loss, 30 Mbit/s; FM at 96.1 MHz; 250 s): started at 1.44 MS/s; `RTL_TCP_RATE_DOWN link=79%`
to 960 kS/s; `RTL_TCP_RATE_DOWN link=64%` to 240 kS/s, all within about 16 s of starting; it then ran at 240 kS/s with `link=89%` and
`state=streaming`. The step-up probe did not fire because 89% is under the 95% healthy threshold. `dropped=3,851,392` bytes were
counted locally during that run; I did not investigate whether they fell in the high-rate phase. Audio was not judged.

**What this means.**
1. A remote profile that starts at 240 kS/s is right: only that rate held up under 0.3% loss.
2. At 1% loss and 80 ms even 240 kS/s (3.84 Mbit/s) fell to about half. The RTL2832 cannot go below 225,001 S/s, so on a link that
   bad only server-side decimation (a different server) or a tunnel that avoids loss helps; this is outside v1.
3. The ladder worked on real hardware against a real server: down in two steps within about 16 s.

**Not done / limits.** Runs were seconds to 4 minutes, not the 1 hour planned; no real internet path (a real one needs a second
site; Tailscale picked the direct LAN path); no antenna check; no sustained Wi-Fi run beyond 15 s per rate; direct sampling and
bias tee were not touched; the under-voltage was not cleared, so a clean-power repeat is still owed.

**Power fixed (later the same day).** After a power supply swap the Pi still flagged under-voltage (live bits set in 8 of 32
idle samples, 5 kernel events in 64 s, not caused by streaming). After a new USB-C cable: `get_throttled=0x0`, 0 live samples in 32
idle readings, 0 kernel under-voltage events, and the same over 32 readings while streaming 2.4 MS/s for 60 s (100.0%, 38.4 Mbit/s).
The measurements above that were taken under the fault are unchanged in character (no data loss either way), but a clean-power
repeat of the long runs is still owed.

**Clean-power 10-minute run, Ethernet** (PC client to the Pi at 192.168.1.218, 1.44 MS/s, 600 s, 1.73 GB): average 99.99% of the
required byte rate; worst 10-second window 99.4%, best 100.2%; 59 windows, none under 95%; no 3 s read stall; `get_throttled=0x0`
in all 122 samples taken every 5 s; 0 kernel under-voltage events; Pi temperature 50.1-56.4 C; Pi load stayed under 0.6.
The matching Wi-Fi run was NOT done: after the last reboot the Pi's saved Wi-Fi profile was gone and `wlan0` was down
("Not connected"), and joining a network needs the Wi-Fi password, which this session does not enter. The earlier 12-15 s Wi-Fi
runs (100% at every rate, under the power fault) stand; a 10-minute Wi-Fi run is still owed.
