# rtl_tcp release test plan: every dashboard, gaps and opportunities

Goal: rtl_tcp (remote receiver) works on every OrcSDR dashboard as well as it does in the best SDR clients, with tests that can be
re-run. This page is the plan plus a running log; a scheduled loop (every 30 minutes while the session lives) extends it. Anything
marked "by reading" comes from the source and has not been run. Competitor statements come from general knowledge of SDR++, Gqrx,
SDR#, SDRangel and SpyServer clients, not from fresh research: treat them as hypotheses until checked.

Hands-off control: `UI_TAP x y` (authenticated, panel 1280x720) taps through the normal touch path; `tools/tab5_ft8_shot.py` screenshots.
Lab: Tab5 on COM17 (source `remote` or `usb`; a Blog V3c is plugged into its USB port), Pi 4B at 192.168.1.218:1234 running the
Blog `rtl_tcp` V1.4.0 (dongle "Blog V4L", greeting tuner 5), PC with `tools/rtl_tcp_probe.py`. Serial helpers: `UI_DOC_SHOW <id> live`,
`UI_DOC_EXIT`, `RTL_TAG_DOC ON`, `RTL_TCP STATUS`, `RTL_FM_SCOPE`, `RTL_SCREEN_STATUS`, screenshots with `tools/tab5_ft8_shot.py`.

## 1. Test layers

| Layer | What | How | Automated now |
|---|---|---|---|
| L0 Protocol | greeting, 5-byte commands, odd-byte carry, unknown opcodes, state after reconnect | `bash tools/test-rtl-tcp.sh`, `tools/rtl_tcp_probe.py header|second|badcmd|stall` | yes |
| L1 Link and rate | throughput per rate, step-down and recovery, loss tolerance | `probe rates|soak`, netem on the Pi, Tab5 `RTL_TCP_RATE_*` events | yes |
| L2 Per dashboard | opens, starts a remote stream, shows the RTL-TCP tag, controls work, decoder runs | `UI_DOC_SHOW` for the screens it can open, taps for the rest | partly |
| L3 Failure injection | server killed, dongle drops, Wi-Fi blip, wrong host, busy server, slow link | Pi `systemctl`, netem, `RTL_TCP HOST` | partly |
| L4 Soak and parity | hours of streaming, counters, comparison with local USB and with other clients | `probe soak`, Tab5 counters | partly |

## 2. Findings so far (audit by reading `ui/main.cpp`, 2026-11-08)

Severity: B = blocks release for remote users, M = clearly worse than a competitor, L = polish.

| # | Gap | Where | Sev | Test that would catch it |
|---|---|---|---|---|
| G1 | Gain, AGC and bias controls are gated on a local dongle (`g_rtl != nullptr`, `rtl_tuner_gain_available`, `rtl_has_device_capability`), so with a remote source they show N/A and cannot be used; the gain routing hook exists but the dashboards stop before reaching it | gain handlers about lines 6836-6912, 11241-11261, 11391-11411, 11622-11651, 12896-12937, 13401-13433, 14466-14490, 14753-14762 | B | open each dashboard on remote, try to change gain, check the server log shows `set gain` |
| G2 | FT8, SSTV and ACARS ask the local driver whether 240 or 250 kS/s is supported (`esp_rtl_sdr_is_rate_supported`); with no local dongle they do not set their low rate (ACARS refuses to tune) | lines 14531, 14738, 14816 | B | FT8 and ACARS on remote: stream rate must be 240 or 250 kS/s |
| G3 | Health and metrics come from the local driver (`esp_rtl_sdr_get_metrics(g_rtl...)`), so RF Health, rate and overrun cards show nothing real for a remote stream; the remote counters (link %, dropped, reconnects) are not shown there | 2139, 2466, 3837, 3879, 11115, 12077, 13381 | M | FM, P25, Home RF health on remote vs the `RTL_TCP STATUS` numbers |
| G4 | **FIXED and verified 2026-11-08** (see run log). Reconnect back-off resets after any 2 s connection, so a server that greets and then sends nothing is retried every ~13 s forever (744 reconnects seen) | `ui/rtl_tcp_session.cpp` `run()` | B | stop samples on the server side (kill the dongle) and watch the back-off |
| G5 | After a retune the stream still carries old-frequency samples: 0.461 s at 240 kS/s measured; scanners, seek and hunt that dwell less read the wrong frequency | FM seek/preset scan, Shortwave hunt, Airband and Browse scans, Dead Air | M | `probe retune`, then a scan on remote against a known list |
| G6 | Remote HF: a server tuner of type 5 (R820T) cannot tune below 24 MHz without direct sampling; no direct-sampling control exists for a remote server | Shortwave, AM, CB | M | Shortwave on remote against a Blog V4 server and a plain R820T server |
| G7 | RF Lab and the V3 HF route assume the local driver (`g_rtl`) | 7197-7430, 8883 | L | RF Lab on remote must say "local dongle only", not show zeros |
| G8 | No remote profile (LAN or Internet) or 240 kS/s default for narrow bands; 1.44 and 0.96 MS/s suit a LAN, not a WAN | `rtl_remote_wanted_rate`, Settings | M | lossy-link runs at each profile |
| G9 | ADS-B needs 2.048 MS/s (about 4.1 MB/s), above the Tab5 Wi-Fi ceiling of about 3.8 MB/s measured on a Pi stream | `rtl_default_sample_rate` | M | ADS-B on remote: link % and decoded messages |
| G10 | The server's USB buffers and queue are only partly visible: no "behind real time by N seconds" display, only a percentage | status bar, Home | L | slow-link run: displayed lag vs measured |
| G11 | `UI_DOC_SHOW am.radio live` (and cb, adsb, pocsag) crashes the Tab5: `Guru Meditation Error: Core 0 panic'ed (Load access fault)`, reset reason 7. Reproduced on a remote stream AND on a local V3c, so not rtl_tcp. The stack pointer is below the main task's stack bottom (SP 0x4ff2d2f0, bounds 0x4ff2d328-0x4ff314a0, about 16.7 KB): a stack overflow. Fault address resolves to `OrcConsole::printf` (`ui/main.cpp:265`, a 2048-byte `char output[]` on the stack) called from `draw_sdr_screen` (`main.cpp:7538`, the AM dashboard branch), itself reached from the serial command handler | `ui/main.cpp` 265, 7538 | L: the normal tap path does NOT crash (AM RADIO opened by `UI_TAP` 2026-11-08 and ran), so this is a bug in the serial `UI_DOC_SHOW` path only | tap AM RADIO and CB RADIO on Home; `UI_DOC_SHOW am.radio live` again after a fix |

## 3. Dashboard matrix

IQ need = what the dashboard asks the stream for locally; "remote" = what a remote stream gives today (by reading).

| Dashboard | IQ need | Remote today (by reading) | Tests to run | Automation |
|---|---|---|---|---|
| Home (spectrum, FM) | 2.4 MS/s local | 1.44 MS/s, header and tag fixed, gain N/A (G1) | stream, span cap, tag, retune, gain | serial + screenshot |
| FM Broadcast | 2.4 MS/s local | 1.44 MS/s, tag verified, scope 8 fps, seek and preset scan affected by G5 | listen, spectrum, RDS, RF health, preset scan | `UI_DOC_SHOW fm.*` |
| AM / WX / CB / Browse | 2.4 MS/s wide bands | 1.44 MS/s; HF affected by G6 | each tab, tune across the band | `UI_DOC_SHOW` (am/cb did not open in remote mode: investigate) |
| Shortwave | 2.4 MS/s | start allowed since the HF fix; needs a server that handles HF (G6) | start, tune 7.1 MHz, hunt | manual tap |
| Airband | 2.4 MS/s | 1.44 MS/s, scope, scanning (G5) | scope, channel scan | manual tap |
| P25 | 2.4 MS/s? | 1.44 MS/s, decode quality unknown | monitor, talkgroups, RF health | `UI_DOC_SHOW p25.*` |
| ADS-B | 2.048 MS/s | off-ladder rate, exceeds link (G9) | message rate, link % | manual |
| LoRa | 0.5 MS/s | ladder rung 0.96 MS/s (by reading) | overview, scan band, channels | `UI_DOC_SHOW lora.*` |
| POCSAG | narrow | not examined | live, ids | `UI_DOC_SHOW pocsag.*` (did not open: investigate) |
| FT8 | 240 kS/s | rate request fails without a local dongle (G2) | band tune, decode a slot, clock offset | manual + `FT8 SNAP` |
| SSTV | 240 kS/s | same as FT8 (G2) | receive an image | manual |
| ACARS / VDL2 | 240 / 250 kS/s | tune refused without a local dongle (G2) | message decode | manual |
| Dead Air | wide | not examined | band map, scrub | manual |
| RF Lab / Visualizer | local driver | local only (G7) | message shown | manual |
| Settings | n/a | remote source rows exist | host, port, source, profile | host test + screenshot |

## 4. Parity with other clients (hypotheses to verify)

| Capability | Typical in other clients | OrcSDR remote today |
|---|---|---|
| Set sample rate | yes, manual | automatic ladder; no manual pick (G8) |
| Tuner gain, AGC, RTL AGC | yes | unavailable on dashboards (G1) |
| Bias tee, direct sampling, offset tuning | yes (SDR++, Gqrx expose them) | no, by policy for bias tee; no UI for the others |
| Frequency correction (ppm) | yes | client sends it only if set, no UI |
| Reconnect, status | basic | automatic reconnect and status tag; back-off bug (G4) |
| Latency or buffer indication | rare | link % only (G10) |
| Server buffer stats | no | the Pi session can read `ss` and the journal |

## 5. Run log (the loop appends here)

- 2026-11-08 iteration 1: audit (section 2), plan, new `retune` test in `tools/rtl_tcp_probe.py`. Retune stale time: 0.461 s at 240 kS/s
  (3 of 3 runs), 0.145 s at 960 kS/s (1 of 3 runs registered), 1.44 MS/s: detector did not fire in 3 runs, so no result. The detector
  needs a steadier reference (the first blocks may still carry the previous tuning) before these numbers are trusted.
  Server was `-b 4 -n 32`.

- 2026-11-08 (evening): Tab5 back on the Pi over Wi-Fi only (the Pi's Ethernet removed; server follows the default-route address, bound to
  192.168.1.219:1234): FM 96.1 MHz at 1.44 MS/s, `link` above 100% at start, no new dropped bytes, Pi power flag 0x0. Home showed
  `GAIN SMART 3` and `BIAS OFF` and a clean -21 dBFS signal (the unattended Pi had been at -1 dBFS with a fixed high gain), so gain
  routing to a remote server does work **when a local dongle is also plugged in**: the capability bits then come from the local V3c.
  That is the G1 mismatch seen from the other side: the controls (including the bias-tee switch) describe the local dongle, not the
  server. With no local dongle the same controls read N/A. This supports a single "active source capabilities" provider
  (see the design question of 2026-11-08).

- 2026-11-08 iteration 2: the earlier "am, cb, adsb, pocsag do not open in remote mode" is a crash (G11). Reproduced twice on a remote
  stream and once on the local V3c. Core dump is saved in the Tab5's flash (partition `coredump`). The addresses were mapped with
  `riscv32-esp-elf-addr2line` against `build-native-hosted3/orcsdr_tab5.elf` (the same build as the flashed one). Update the same evening: a new authenticated serial command `UI_TAP x y` (firmware, built and flashed to COM17) injects a touch through the existing virtual-tap path; tapping AM RADIO from Home opened the AM dashboard with no crash (uptime kept counting, `RTL_SCREEN_STATUS active=am`). So G11 affects only `UI_DOC_SHOW`. Not fixed.

## 5a. Sweep of all 19 dashboards on a remote stream (2026-11-08, iteration 3, hands-off)

Method: `python tools/tab5_ui_drive.py --port COM17 sweep --out DIR --source remote --dwell 10` (taps through Home, ALL DASHBOARDS and each tile with
`UI_TAP`, reads `RTL_SCREEN_STATUS` and `RTL_TCP STATUS`, takes a screenshot, flags a crash if uptime goes backwards). Source: Pi 4B on Wi-Fi
(192.168.1.219:1234, Blog `rtl_tcp` V1.4.0, `-s 240000 -n 32 -b 4`), Tab5 on Wi-Fi. Link is the Tab5's received-over-needed byte ratio.

| Dashboard | Opens | Stream rate shown | Link | Tag | Notes |
|---|---|---|---|---|---|
| FM | yes | 1.44 MS/s | 90% | bar | first sweep read idle only because the local V3c stream was still running; fixed in the tool |
| P25 | yes | 0.96 | 95% | bar | |
| ADS-B | yes | **2.048** | 99% | full | streams; 0 aircraft in view, so decode not proven (antenna and location unknown) |
| Shortwave | yes | 1.44 | 95% | bar | starts on remote since the HF fix |
| AM | yes | 1.44 | 89% | bar | |
| CB | yes | 0.96/1.44 | 95% | bar | |
| LoRa | yes | 0.96 | 99% | strip | |
| POCSAG | yes | 0.96 | 98% | strip | |
| FT8 | yes | **1.44** (should be 0.24) | **84%, BEHIND** | strip | "CLOCK SYNCING", "LISTENING", 0 decoded |
| ACARS | yes | 0.96 | 92% | full | **"RECEIVER PAUSED", "TUNE REQUEST PENDING", "NO RECEIVER FEED: the native decoder is not attached to a receiver yet"** |
| Airband | yes | 0.96/1.44 | 96% | bar | |
| Weather, Marine, Satellite | yes (Home with that band) | 0.96 | 91-96% | Home status | |
| SSTV | yes | **0.240** | 100% | strip | uses its own low rate correctly |
| Dead Air | yes | 0.96 | 95% | strip | |
| RF Lab | yes | n/a | 87-89% | none | LIVE tab works on remote: peak frequency, SNR, DC offset, IQ balance read correctly (peak 96.106 MHz, SNR 39.8 dB); CONTROLS tab not yet tried |
| 2.4 GHz Wi-Fi | yes | n/a | 91% | none | |
| Settings | yes | n/a | 85-88% | own | |

Results: 19 of 19 open on a remote stream, **no crashes**, the RTL-TCP tag is correct and unobstructed on every screen that has one. The `UI_DOC_SHOW` crash (G11)
is not reachable by tapping.

Corrections and confirmations to the gap list:
- **G2 confirmed with evidence.** FT8 runs at 1.44 MS/s and falls behind (84%) instead of 240 kS/s; ACARS never attaches a receiver. SSTV is fine.
- **G9 corrected.** ADS-B at 2.048 MS/s (4.1 MB/s) ran at 99% link on the Tab5's Wi-Fi for the sampled windows, so it is not ruled out by bandwidth.
  It still needs a long run and a real decode check.
- **G7 narrowed.** RF Lab's LIVE measurements work on remote; only hardware controls are local.
- **G1** is visible on every dashboard that shows gain; not yet individually checked.

- 2026-11-08 iteration 4: **G4 fixed.** `ui/rtl_tcp_session.cpp`: a connection resets the reconnect back-off only if it also delivered at least 64 KiB
  (`last_stream_bytes`), not merely lasted 2 s. New host test `test_greeting_without_samples_keeps_backing_off` (a server that greets and then sends nothing)
  fails before the fix (the session never gives up) and passes after; all rtl_tcp host tests pass. Built, flashed to COM17 and checked on the Tab5 against a
  fake server that greets then goes silent: connection gaps 3.5, 4.1, 5.0, 7.0, 11.0, 18.0, 18.0, 18.1 s (the 3 s stall plus the 0.5 to 15 s back-off), then
  `state=failed reason=no data` after 9 connections, instead of 744 reconnects. A normal stream from the Pi still starts and runs afterwards (link 97%).

## 6. Next iterations (in order)

1. Fix the retune detector and repeat at all rates and at `-b 2`; add a Tab5-side check of what FM seek dwell is.
2. DONE (see 5a): all 19 open; the earlier failure was G11, the serial `UI_DOC_SHOW` crash.
3. Prove G1 and G2 on the Tab5 (open FT8 and ACARS manually, read `RTL_START` and the Pi's `set sample rate` log).
4. With the owner's go-ahead: fix G4 (back-off), then G1/G2/G3 behind the existing source routing, one dashboard family at a time.
5. Failure injection (L3): kill the server, drop the dongle, Wi-Fi off and on, wrong host, busy server; record behaviour and recovery time.
6. Parity pass (section 4) with the real clients if available on the PC.
