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
