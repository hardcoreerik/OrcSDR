# rtl_tcp release test plan: every dashboard, gaps and opportunities

Goal: rtl_tcp (remote receiver) works on every OrcSDR dashboard as well as it does in the best SDR clients, with tests that can be
re-run. This page is the plan plus a running log; a scheduled loop (every 30 minutes while the session lives) extends it. Anything
marked "by reading" comes from the source and has not been run. Competitor statements come from general knowledge of SDR++, Gqrx,
SDR#, SDRangel and SpyServer clients, not from fresh research: treat them as hypotheses until checked.

Hands-off control: `UI_TAP x y` (authenticated, panel 1280x720) taps through the normal touch path; `tools/tab5_ft8_shot.py` screenshots.
Lab: Tab5 on COM17 (source `remote` or `usb`; a Blog V3c is plugged into its USB port), Pi 4B at 192.168.1.218:1234 running the
Blog `rtl_tcp` V1.4.0 (dongle "Blog V4L", greeting tuner 5), PC with `tools/rtl_tcp_probe.py`. Serial helpers: `UI_DOC_SHOW <id> live`,
`UI_DOC_EXIT`, `RTL_TAG_DOC ON`, `RTL_TCP STATUS`, `RTL_FM_SCOPE`, `RTL_SCREEN_STATUS`, screenshots with `tools/tab5_ft8_shot.py`.

## Status at a glance (updated 2026-11-09, iteration 18)

What has been proven on the Tab5 and a real Pi (a Pi 4B, Blog V4L dongle, Blog `rtl_tcp` V1.4.0), and what is waiting. Every fix below was built, flashed to COM17 and checked on the device; each row points to its evidence in the run log (section 5).

| Area | Status |
|---|---|
| All 19 dashboards open on a remote stream, RTL-TCP tag shown correctly | **Done** (sweep, section 5a); no crashes |
| Tapping, screenshots and crash detection are automated | **Done**: `UI_TAP`, `tools/tab5_ui_drive.py`, `tools/tab5_ft8_shot.py` |
| One-hour soak: connection, memory, Pi power and temperature | **Done**: no reconnects, memory flat, Pi clean |
| G4 reconnect back-off never gave up (744 reconnects) | **Fixed and verified** |
| G12 internal RAM leak (FT8 tasks ended with the wrong delete call; also catalog sync and the bench tasks) | **Fixed and verified**: PSRAM -28.8 KB per FT8 visit before, noise after; two full dashboard loops flat |
| G16 about 10% of samples dropped while the rate ladder never reacted | **Fixed and verified**: the ladder now decides on the mean of the last 8 readings, tested on recorded real-link data |
| G2 FT8/ACARS cannot use the remote rate | **Retracted**: not reproducible; they start at 240 kS/s |
| G5 stale samples after a retune | **Measured**: about 0.12 s at 240 kS/s, 0.07 s at 0.96-1.44 MS/s, independent of server buffers |
| G14 Pi server (slow stop, dongle that stops delivering samples) | **Fixed on the Pi**: fast stop, USB reset at every start, follows the default-route address |
| Competitor parity | **Sourced** (section 4a): retry-forever is the norm, bias tee stays server-side |
| **G13 keep retrying after the fast attempts run out** | **Fixed and verified on the Tab5** (iteration 27; owner approved) |
| **G15 say "server busy" and keep trying when the slot is held** | **Fixed and verified on the Tab5** (iterations 27 and 29) |
| **G1 gain/AGC controls on remote: serial path fixed and verified with and without a local dongle (iterations 21 and 33); popups not yet tapped on each dashboard.** G3 health cards, G6 remote HF and direct sampling, G7 RF Lab note, G8 remote profile, G10 lag display | **Designed, not built** (section 7); each changes dashboard behaviour, so each needs the owner's yes |
| G9 ADS-B at 2.048 MS/s | **Open**: the link held 99% in the sweep, decoding not proven (no aircraft in view) |
| G11 `UI_DOC_SHOW` crash for AM/CB/ADS-B/POCSAG | **Open, low**: serial test command only; the screens open normally by tapping |
| Not done | Real internet path (needs a second site), audio judged by ear, Pi on Ethernet to confirm the double Wi-Fi hop, a Linux/Pi rtl_tcp server test of the Tab5's own server |

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
| G1 | **Manual gain on Home FIXED and verified when a local dongle is also plugged in (iteration 21); remote-only (no local dongle) and the other dashboards NOT yet: see the run log.** Gain, AGC and bias controls are gated on a local dongle (`g_rtl != nullptr`, `rtl_tuner_gain_available`, `rtl_has_device_capability`), so with a remote source they show N/A and cannot be used; the gain routing hook exists but the dashboards stop before reaching it | gain handlers about lines 6836-6912, 11241-11261, 11391-11411, 11622-11651, 12896-12937, 13401-13433, 14466-14490, 14753-14762 | B | open each dashboard on remote, try to change gain, check the server log shows `set gain` |
| G2 | **RETRACTED 2026-11-08: not reproducible, no code change made** (see run log). It was: FT8, SSTV and ACARS ask the local driver whether 240 or 250 kS/s is supported (`esp_rtl_sdr_is_rate_supported`); with no local dongle they do not set their low rate (ACARS refuses to tune) | lines 14531, 14738, 14816 | B | FT8 and ACARS on remote: stream rate must be 240 or 250 kS/s |
| G3 | Health and metrics come from the local driver (`esp_rtl_sdr_get_metrics(g_rtl...)`), so RF Health, rate and overrun cards show nothing real for a remote stream; the remote counters (link %, dropped, reconnects) are not shown there | 2139, 2466, 3837, 3879, 11115, 12077, 13381 | M | FM, P25, Home RF health on remote vs the `RTL_TCP STATUS` numbers |
| G4 | **FIXED and verified 2026-11-08** (see run log). Reconnect back-off resets after any 2 s connection, so a server that greets and then sends nothing is retried every ~13 s forever (744 reconnects seen) | `ui/rtl_tcp_session.cpp` `run()` | B | stop samples on the server side (kill the dongle) and watch the back-off |
| G5 | After a retune the stream still carries old-frequency samples: **measured 0.12 s at 240 kS/s and 0.07 s at 0.96 and 1.44 MS/s (iteration 17; an earlier 0.46 s was a detector artifact), independent of the server's `-b`/`-n`**; scans, seek and hunt need a dwell of at least about 0.3 s plus the link round trip | FM seek/preset scan, Shortwave hunt, Airband and Browse scans, Dead Air | M | `probe retune`, then a scan on remote against a known list |
| G6 | Remote HF: a server tuner of type 5 (R820T) cannot tune below 24 MHz without direct sampling; no direct-sampling control exists for a remote server | Shortwave, AM, CB | M | Shortwave on remote against a Blog V4 server and a plain R820T server |
| G7 | RF Lab and the V3 HF route assume the local driver (`g_rtl`) | 7197-7430, 8883 | L | RF Lab on remote must say "local dongle only", not show zeros |
| G8 | No remote profile (LAN or Internet) or 240 kS/s default for narrow bands; 1.44 and 0.96 MS/s suit a LAN, not a WAN | `rtl_remote_wanted_rate`, Settings | M | lossy-link runs at each profile |
| G9 | ADS-B needs 2.048 MS/s (about 4.1 MB/s), above the Tab5 Wi-Fi ceiling of about 3.8 MB/s measured on a Pi stream | `rtl_default_sample_rate` | M | ADS-B on remote: link % and decoded messages |
| G10 | The server's USB buffers and queue are only partly visible: no "behind real time by N seconds" display, only a percentage | status bar, Home | L | slow-link run: displayed lag vs measured |
| G12 | **FIXED 2026-11-09 (FT8 task leak; verified on the Tab5, see the run log).** Internal RAM ratcheted down and did not recover at Home. Internal RAM (`dma_free`) falls with use: 25,967 B (69 s uptime) to 16,903 B (15 min, about 30 dashboard transitions); the Tab5's internal pool is nearly full, so a leak would be a boot-loop or crash risk | `RTL_HEALTH` | M (unconfirmed) | repeat dashboard switches and stream restarts and plot `dma_free`/`internal_free`; check it recovers at Home |
| G11 | `UI_DOC_SHOW am.radio live` (and cb, adsb, pocsag) crashes the Tab5: `Guru Meditation Error: Core 0 panic'ed (Load access fault)`, reset reason 7. Reproduced on a remote stream AND on a local V3c, so not rtl_tcp. The stack pointer is below the main task's stack bottom (SP 0x4ff2d2f0, bounds 0x4ff2d328-0x4ff314a0, about 16.7 KB): a stack overflow. Fault address resolves to `OrcConsole::printf` (`ui/main.cpp:265`, a 2048-byte `char output[]` on the stack) called from `draw_sdr_screen` (`main.cpp:7538`, the AM dashboard branch), itself reached from the serial command handler | `ui/main.cpp` 265, 7538 | L: the normal tap path does NOT crash (AM RADIO opened by `UI_TAP` 2026-11-08 and ran), so this is a bug in the serial `UI_DOC_SHOW` path only | tap AM RADIO and CB RADIO on Home; `UI_DOC_SHOW am.radio live` again after a fix |

| G13 | After its reconnect budget (8 attempts, about 60 s) the client gives up (`failed`) and stays down until the dashboard is reopened: right for a server that is wrong, wrong for an unattended remote Pi that was only briefly offline. Opportunity: after the fast attempts, keep retrying slowly (for example every 30 s) while a dashboard wants the stream | `ui/rtl_tcp_session.cpp` `run()` | M | server down for 2 minutes, then up: does the Tab5 recover on its own? |
| G14 | Server side (Pi): `rtl_tcp` ignores SIGTERM while a client is connected, so `systemctl stop` or `restart` waited the default 90 s. Fixed on the Pi with a drop-in `TimeoutStopSec=3`. The dongle can also stop delivering samples (the server then greets clients and sends nothing; each session dies after 5 s, "worker cond timeout"); a USB reset without unplugging fixes it, and the Pi unit now resets the dongle at every start. The trigger was not reproduced | Pi unit, setup guide | M | stop and restart with a client attached; busy-server and quick-reconnect tests with a clean server |

| G15 | **Busy server.** With another client holding the server's only slot, the Tab5 waits correctly, but (a) after the 10 s greeting timeout it shows "not an rtl_tcp server" instead of "server busy" (a held connection with no greeting is the busy signal, measured), and (b) its start gives up after 12 s and goes idle, so it never connects when the slot frees | `ui/rtl_tcp_session.cpp` end reasons, `start_and_wait` in `ui/main.cpp` | M | PC client holds the slot, Tab5 starts, holder leaves: the Tab5 should connect within a retry interval and say "busy" meanwhile |

| G16 | **FIXED and verified 2026-11-09 (first a 93% threshold, then a windowed mean; see the run log).** A steady 85-92% link means about 10% of samples are being dropped, and the rate ladder does nothing about it.** Soak (Tab5 FM at 1.44 MS/s from the Pi, both on Wi-Fi): 30 minutes, one connection, zero reconnects, but the Pi sent only 4.51 GB of the 5.01 GB the dongle produces (90%); the server drops its oldest blocks when the client cannot keep up. The ladder steps down only after 4 consecutive 2 s samples below 85%, and this link hovered just above 85%, so it never stepped down. Likely cause: both ends on Wi-Fi, so every packet crosses the air twice (about 21 Mbit/s usable, 1.44 MS/s needs 23) | `RateAdapter` thresholds in `ui/rtl_tcp_protocol.hpp` (`kBehindPct` 85, `kHealthyPct` 95), `ui/rtl_tcp_session.cpp` | B for audio quality | soak both ends on Wi-Fi vs Pi on Ethernet; compare sent bytes with the dongle's rate; adapter step-down at 94% |

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

## 4a. Competitor parity: what is actually documented (web search 2026-11-09; replaces the hypotheses in section 4 where it speaks)

Only what the sources state. Gaps in what I could find are listed as such; nothing here comes from running those programs.

| Client | Documented fact | Source | OrcSDR today / opportunity |
|---|---|---|---|
| gr-osmosdr (the backend Gqrx uses) | rtl_tcp device string `rtl_tcp=host:port[,psize=16384][,direct_samp=0\|1\|2][,offset_tune=0\|1][,bias=0\|1]`; gain and frequency are sent over the connection | [gr-osmosdr commit adding bias to rtl_tcp](https://gitea.osmocom.org/sdr/gr-osmosdr/commit/3c7d3f166401b1af6afd903b8d2b9729e8897189), [rtl_tcp man page](https://manpages.org/rtl_tcp/1) | OrcSDR sends rate, frequency, gain mode, gain, AGC. **Opportunity:** offer direct sampling (0/1/2) and offset tuning as owner settings on the remote settings page (G6) |
| Gqrx | For a remote rtl_tcp, bias tee is enabled on the server with `-T`; the device-string `bias=` applies to a locally attached dongle (maintainer's reply in the thread) | [Gqrx list: enabling bias tee with rtl_tcp](https://groups.google.com/g/gqrx/c/Nn9ZCiMitys) | Matches OrcSDR's policy (never switch bias tee remotely); the setup guide should say `-T` is the server owner's decision |
| SDRangel Remote TCP input | Works with rtl_tcp, rsp_tcp, Spy Server and its own sink; **on a connection error it keeps trying to reconnect continuously**; a user-set buffer trades gaps for latency and "a buffer that repeatedly runs empty points to insufficient bandwidth"; decimation only works against an SDRangel server (plain rtl_tcp is 8-bit, no decimation, and sends no device settings on connect) | [SDRangel remotetcpinput readme](https://repo.radio/VE7UWU/sdrangel/src/tag/v7.21.4/plugins/samplesource/remotetcpinput), [SDRangel remotetcpsink readme](https://repo.radio/VE7UWU/sdrangel/src/tag/v7.22.2/plugins/channelrx/remotetcpsink) | **Gap G13 confirmed as a parity gap:** OrcSDR gives up after about 60 s; a competitor retries forever. Their "buffer empty = not enough bandwidth" rule is our link-ratio rule (G16). **Opportunity:** a latency/buffer choice in the remote settings |
| SDR# | A separate "RTL-SDR / TCP" source: host and IPv4 address, port (default 1234); sample rate and gain in its Configure panel; about 35 Mbit/s of network traffic at 2.048 MS/s quoted for rtl_tcp | [SDR# user guide](https://www.rtl-sdr.com/sdrsharp-users-guide/), [rtl-sdr.com rtl_tcp tag](https://www.rtl-sdr.com/tag/rtl_tcp/) | OrcSDR automates rate (ladder) where SDR# leaves it manual. Could not find SDR#'s AGC, offset-tuning or direct-sampling options for the TCP source documented |
| SDR++ | Manual lists general source options (gain/LNA, AGC, sample rate, bias-T, offset, IQ correction, decimation) and RTL-only direct sampling, RTL AGC, tuner AGC, PPM; could not confirm which of these the RTL-TCP source module exposes | [SDR++ manual](https://www.sdrpp.org/manual.pdf) | not verifiable from the search |
| rtl_tcp servers | A community rtl_tcp with a ring buffer is reported to perform much better than the stock linked-list queue | [rtl-sdr.com: ring buffers for rtl_tcp](https://www.rtl-sdr.com/significantly-improving-rtl_tcps-performance-with-ring-buffers/) | **Opportunity (Pi side):** evaluate that server for the setup guide; stock drop-oldest queue is what loses samples on a slow link |

Conclusions for the plan: (1) retry-forever behaviour is the norm in the client the search found documentation for, so G13 is a parity gap, not a matter of taste; (2) bias tee stays server-side everywhere; (3) the remote
settings page should expose direct sampling and offset tuning as explicit owner choices, as gr-osmosdr's device string does; (4) a visible buffer/latency choice is a differentiator available at low cost.

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
- **G2 was wrong (see the iteration 5 log).** The sweep only opened FT8 and ACARS; I then called an in-transition reading (FT8 at 1.44 MS/s, ACARS "paused") a failure. Both start at their low rate on remote.
- **G9 corrected.** ADS-B at 2.048 MS/s (4.1 MB/s) ran at 99% link on the Tab5's Wi-Fi for the sampled windows, so it is not ruled out by bandwidth.
  It still needs a long run and a real decode check.
- **G7 narrowed.** RF Lab's LIVE measurements work on remote; only hardware controls are local.
- **G1** is visible on every dashboard that shows gain; not yet individually checked.

- 2026-11-08 iteration 4: **G4 fixed.** `ui/rtl_tcp_session.cpp`: a connection resets the reconnect back-off only if it also delivered at least 64 KiB
  (`last_stream_bytes`), not merely lasted 2 s. New host test `test_greeting_without_samples_keeps_backing_off` (a server that greets and then sends nothing)
  fails before the fix (the session never gives up) and passes after; all rtl_tcp host tests pass. Built, flashed to COM17 and checked on the Tab5 against a
  fake server that greets then goes silent: connection gaps 3.5, 4.1, 5.0, 7.0, 11.0, 18.0, 18.0, 18.1 s (the 3 s stall plus the 0.5 to 15 s back-off), then
  `state=failed reason=no data` after 9 connections, instead of 744 reconnects. A normal stream from the Pi still starts and runs afterwards (link 97%).

- 2026-11-08 iteration 5: **G2 retracted.** `esp_rtl_sdr_is_rate_supported()` is a pure function (rate window and quantisation, no device), so the local-dongle theory was wrong.
  Cold tests on the Pi stream (FM, then open the dashboard, log every `RTL_START`): FT8 -> `RTL_START rate=240000 display_hz=14074000` within about 7 s;
  ACARS -> `rate=240000 display_hz=131490000` (5.5 s in one run, and the same on the local V3c in 5-6.4 s); SSTV runs at 0.240 MS/s. Nine fresh-session
  switches (fm, acars, ft8 x3) started a remote stream in a median of 3.5 s, max 4.4 s, none slow and none failed. One earlier ACARS start took about 21 s after the tile tap and could not be
  reproduced (cause unknown, possibly a slow first connect); it is a watch item, not a fix. The sweep's "1.44 MS/s BEHIND 84%" FT8 reading was a transient before the rate switch.
  Lesson recorded: opening a screen is not the same as using it; reproduce on local and remote with the same actions before calling a gap.
- 2026-11-08 iteration 5, watch items: (a) **internal RAM trend**: `RTL_HEALTH` `dma_free` read 25,967 B at 69 s uptime, 19,219 B at 15 min and 16,903 B after about 30 dashboard
  transitions and many stream restarts (`internal_free` 45.5 KB at boot, 17.4 KB later); not yet known whether it recovers or leaks (G12 to measure). (b) The serial pairing
  (`authenticate`) intermittently fails with "pairing failed (device response omitted)" when several processes open the port in quick succession; the driver tool needs a fresh
  session per step as a workaround.

- 2026-11-08 iteration 6: **G12 confirmed.** Three loops over 13 dashboards (fm, am, ft8, acars, adsb, lora, p25, shortwave, airband, sstv, deadair, cb, pocsag) on the remote stream,
  `RTL_HEALTH` after each open and at Home after each loop (43 samples, Tab5 up 23 to 32 min):

  | Reading | `dma_free` | `dma_min` (lowest ever) | `internal_free` |
  |---|---|---|---|
  | start | 14,991 B | 11,275 B | 34,099 B |
  | after loop 1, at Home | 5,251 B | 2,607 B | 20,647 B |
  | after loop 2, at Home | 4,539 B | 1,895 B | 20,067 B |
  | after loop 3, at Home | 3,827 B | 1,183 B | 19,091 B |

  Two effects: a one-time step when SSTV and Dead Air are first opened (`dma_free` 13.5 KB to 5.0 KB at SSTV; `internal_free` 13.7 KB at Dead Air), and a steady loss of about 700 B of
  `dma_free` (and 600-1000 B of `internal_free`) per loop of 13 dashboard switches that is not given back at Home. Extrapolating, `dma_free` reaches zero after about five more loops, the
  kind of exhaustion that earlier boot-looped the Tab5 (+800 B of statics). `free_heap` (PSRAM) is flat at about 6.2 MB at Home, so the leak is internal RAM only.
  Not yet known which action leaks: a differential test (stream restarts only on remote, the same on local USB, dashboard taps with no restart) is running; result in the next entry.

- 2026-11-09 iteration 7: **G12 root cause found and fixed.** Differential test: 25 stream restarts on remote, 25 on local USB and 25 dashboard-grid round trips (no restart) were all flat;
  repeated single-dashboard visits showed the leak only on FT8: about 28.8 KB of PSRAM and about 800 B of internal RAM lost per visit (ACARS and ADS-B: none).
  Cause: `ft8_runtime.cpp` creates its two tasks (`ft8_native` 20,480 B and `ft8_wf` 8,192 B, stacks in PSRAM) with `xTaskCreatePinnedToCoreWithCaps` but ends them with plain
  `vTaskDelete(nullptr)`. ESP-IDF's `idf_additions.c` states that a self-deleting WithCaps task must use `vTaskDeleteWithCaps(NULL)`, because the idle task will not free the
  stack and control block ("Therefore, it will leak memory"). Fix: `vTaskDeleteWithCaps(nullptr)` at both exits. The same mistake was in the catalog-sync worker (32 KiB stack per run,
  `catalog_sync.cpp`) and in the two `RTL_TCP BENCH`/`BENCHUDP` debug tasks (6 KiB each, `rtl_tcp_source.cpp`); fixed the same way (not separately measured; same documented defect).
  Verified on the Tab5 (built, flashed to COM17), same procedure before and after on a fresh boot (warm-up visit, then 6 FT8 visits, `RTL_HEALTH` at Home each time):
  before: PSRAM -28,828 B per visit, internal -824 B per visit; after: PSRAM -40 B per visit (noise), internal no trend (45.4 KB to 44.9 KB, up and down). The 700 B per loop seen over
  13 dashboards earlier was this leak. A full two-loop confirmation over all 13 dashboards is running.

- 2026-11-09 iteration 8: **G12 confirmed fixed over all dashboards.** Two full loops over the 13 dashboards on the fixed firmware (29 `RTL_HEALTH` samples, fresh boot): at Home after loop 1
  PSRAM 7,465,220 B, `dma_free` 16,815 B, `internal_free` 33,191 B; after loop 2 PSRAM 7,465,280 B (+60), `dma_free` 16,459 B, `internal_free` 32,967 B. Loop-to-loop change is noise, against
  about 28.5 KB of PSRAM and 700 B of `dma_free` lost per loop before the fix, and `dma_min` stays at 13.8 KB (it reached 1.2 KB before). One-time first-use allocations that are kept, not
  leaks (they do not repeat in loop 2): SSTV takes about 8 KB of internal RAM (`dma_free` 25.1 KB to 16.9 KB), LoRa about 7 MB of PSRAM, Dead Air about 4 MB while open (returned on exit).
  Worth a look separately: LoRa holding 7 MB after its first visit, and SSTV holding 8 KB of the scarce internal pool.

- 2026-11-09 iteration 8, failure injection (layer L3) on the Pi stream:
  F1 **server stopped mid-stream and restored**: the Tab5 noticed at once (`no data`, `socket error`), retried at t = 2, 6, 14, 29 and 44 s (the back-off 2/4/8/15/15 s) while the Pi was down, and was
  streaming again 12.6 s after the server returned. Good: no manual step, bounded by the 15 s retry interval. The back-off budget ends the retries after about 60 s (G13).
  Found on the way: `systemctl stop rtl-tcp` with a client connected hung for the full 90 s (`stop-sigterm`, then SIGKILL): rtl_tcp catches SIGTERM but its sender thread does not exit. The Pi
  unit now has `TimeoutStopSec=3` (drop-in `10-fast-stop.conf`); stop-to-active after a restart is now a few seconds. The udev rule and NetworkManager hook that restart the service depended on this.
  F2 **busy server (another client holds the only slot)**: INCONCLUSIVE. With a PC client connected (confirmed with `ss` on the Pi), starting the Tab5 produced a repeating
  `connecting`, `streaming`, `no data` cycle every 5 s, and the PC holder was reset by the server; the server log showed "Signal caught, exiting!" on every attempt and no "listening..." for 2 min 16 s
  (00:46:46 to 00:49:02). Earlier loopback tests by the Pi session showed a second client waiting quietly, so this contradicts them and needs a clean rerun (fresh server, one client at a time,
  server log and `ss` captured at each step) before it is called a Tab5 or a server issue. Not fixed.

- 2026-11-09 iteration 9, busy server rerun and the server wedge. **Explained:** the odd F2 result was a wedged dongle on the Pi: after the forced stop the server accepted clients, sent the greeting and then no samples
  (journal: `worker cond timeout`, session dead after 5 s, next queued client takes over), which looked like one client displacing another. Proof: a client received 0 IQ bytes over 3 s, a USB reset
  (`USBDEVFS_RESET`, no unplug) restored 100% throughput at once. A plain `kill -9` and a `systemctl restart` while streaming did NOT reproduce it (both recovered to 100% immediately), so the trigger is
  unknown. Pi change: `/usr/local/bin/rtl-usb-reset.sh` run as root before every start (`20-usb-reset.conf`, `ExecStartPre=+`), chosen because `USBDEVFS_RESET` raises no udev add/remove and cannot loop the restart
  rule; checked: one start, 100% at 240 and 960 kS/s. **Clean busy-server test on the healthy server** (PC holder confirmed with `ss`): the second client waits and is not served, as designed. The Tab5 gives
  up after about 12 s, shows "not an rtl_tcp server" and goes idle (G15). The earlier claim that a second client might displace the first is withdrawn.

- 2026-11-09 iteration 11 (soak midpoint, 30 of 60 minutes): Tab5 on FM, 1.44 MS/s from the Pi (Pi on Wi-Fi, Tab5 on Wi-Fi). Stable: no reconnects (108 connections, 94 reconnects, unchanged), 0 dropped bytes on the
  Tab5, Pi power flag 0x0 throughout, temperature 55 to 64 C, no server errors, memory flat (`dma_free` 16.1-16.6 KB, `dma_min` 13.8 KB, PSRAM 7.3-7.4 MB: the leak fix holds over time). **But the link ratio averaged 88%
  (80-92%, one dip to 34% at minute 17 that recovered)**, and `bytes_sent` on the Pi was 4.51 GB over 1740 s = 2.59 MB/s against 2.88 MB/s needed, with 1.1% of the sent bytes retransmitted. Earlier tests with the PC
  on Ethernet and the Pi on Wi-Fi gave 100% at the same rate, so the double Wi-Fi hop is the likely limit (G16). The ladder never stepped down because 88% is above its 85% threshold.

- 2026-11-09 iteration 12, **one-hour soak complete** (60 samples, 59 minutes; Tab5 on FM from the Pi at 1.44 MS/s, both on Wi-Fi, source `remote`): **no reconnects** (108 connections and 94 reconnects at both start and end), 0 dropped bytes
  on the Tab5, Pi power flag 0x0 in every sample, Pi 55.0 to 63.7 C, service active throughout, 1 server error line in the hour. **Memory flat:** `dma_free` 16,063 B to 16,591 B (lowest 14,743 B), `dma_min` held at 13,835 B,
  PSRAM 7.34 to 7.38 MB, `internal_free` 17.0 to 22.2 KB: the leak fix holds over an hour of streaming. **Link:** median 89%, mean 86.7%, range 34-92%, never 95% or above; 9 of 60 samples under 85%; two brief deep dips (minute 17: 34%, minute 58: 50%) that
  recovered. The Pi sent 9.08 GB in 3540 s = 2.56 MB/s against the 2.88 MB/s needed (89%), 96.2 MB (1.06%) retransmitted. So the hour was stable but about 11% of the samples were dropped by the server (G16); the ladder never stepped down.
  Sweeps at 0.96 MS/s on the same path read 95-99%, so the usable ceiling here is about 2.6 MB/s (21 Mbit/s). Not tested: the same soak with the Pi on Ethernet (needs the cable put back) to confirm the double Wi-Fi hop, and audio quality by ear.

- 2026-11-09 iteration 13: **G16 fixed.** `RateAdapter::kBehindPct` 85 to 93 (`ui/rtl_tcp_protocol.hpp`): below 93% the server is dropping samples, so four readings in a row below it step the rate down; healthy rates read 95% and above
  (sweeps at 0.96 MS/s: 95-99%). The status text uses the same line, so "BEHIND" now appears when samples are actually being lost. Host test: the soak's own readings (91, 85, 82, 87, 90, 85) step down at the fourth; one good reading in the
  middle clears the count; healthy readings 95-101% never step down; the existing dead-band test now uses 93-95%. The new test FAILS against the old 85% threshold (3 failures, checked) and passes with 93%. Built, flashed to COM17 and verified
  on the same double-Wi-Fi link: FM started at 1.44 MS/s and logged `RTL_TCP_RATE_DOWN link=88% new_rate=960000`, then ran at 960 kS/s with link 96% (602.6 MB received in about 5.5 minutes, 6,144 B dropped). No step back up was seen in
  5.5 minutes (the probe needs 30 consecutive readings at 95% or more and this link hovers around 95-96%, so a single dip resets the count); that conservative behaviour is acceptable but worth a longer look.
  Note: right after a flash the Tab5's Wi-Fi is not up for the first seconds, and opening a dashboard in that window starts no stream; `RTL_LISTEN FM` once it is up works.

- 2026-11-09 iteration 16, **G16 reworked on data.** The 93% single-reading rule sat in the middle of the acceptable 0.96 MS/s noise. Link readings recorded on the double Wi-Fi hop: 1.44 MS/s (soak, 61 readings) median 89,
  mean 86.7, max 92; 0.96 MS/s (58 readings every 5 s) median 94, mean 93.9, range 79-97, 9 below 93, 31 below 95; 0.24 MS/s (14 readings) median 99, range 86-101. Simulating rules on those series: 4 in a row below 85 fires 0 times on the lossy
  series; below 93 fires 15 times there and 0 on the others but only because the 960 samples were 5 s apart (real ticks are 2 s); the mean of 8 readings below 91 fires 7 times on the lossy series and never on the others, with margin.
  `RateAdapter` (`ui/rtl_tcp_protocol.hpp`) now decides on the mean of the last 8 readings: step down below 91%, or at once on three readings in a row below 70%; a step up needs the mean at 97% or more for 30 readings (0.96 MS/s never earns a probe to 1.44 MS/s, so it
  settles instead of flapping); a failed probe still doubles the wait; the status label BEHIND now uses single readings below 90%. The three recorded series are in the host test as regression data (lossy 1.44 steps down, acceptable 0.96 never moves, clean 0.24 earns a step up);
  the new tests fail 13 times against the previous rule and all rtl_tcp host tests pass. Built, flashed to COM17, watched for 540 s on the Pi stream: t=16 s DOWN 1.44 to 0.96 MS/s (link 85%); the 30 readings at 0.96 MS/s (median 94, mean 93.2) caused no step;
  t=81 s DOWN to 0.24 at link 62% and t=388 s DOWN to 0.24 at link 45% (genuine collapses; the soak saw dips to 34% and 50%); UP to 0.96 at t=158 s and t=531 s. About 43% of the 540 s were spent at 0.24 MS/s after those dips: recovery takes 77-143 s.
  Opportunity: a step down caused by a brief collapse could earn an earlier probe back up than one caused by a steady shortfall.

- 2026-11-09 iteration 17, **G5 retune staleness measured properly.** `tools/rtl_tcp_probe.py retune` now discards the first `--warm` seconds after the tune, measures the carrier level, then retunes and times when the carrier vanishes (in
  stream sample time). Against the Pi (99.1 MHz carrier moved to 98.5), 3 repeats per cell, every cell produced a result: stale sample time, seconds, current `-b 4 -n 32`: 0.12 (240 kS/s), 0.02-0.07 (960 kS/s), 0.05-0.07 (1.44 MS/s);
  `-b 2 -n 8`: 0.12-0.19, 0.07-0.14, 0.07-0.11; `-b 2 -n 32`: 0.12-0.14, 0.07-0.08, 0.07-0.08; stock `-b 15 -n 500`: 0.12, 0.07-0.17, 0.07-0.09. So a retune takes effect within about 0.1-0.2 s of stream time whatever the server's buffer
  settings, a number to design scan dwell around (plus the network round trip). The earlier 0.46 s was an artifact of measuring the carrier before the previous tuning had flushed. Recommendation for the Pi guide: `-b`/`-n` need no tuning for retune
  latency; keep `-n` small only to bound how stale a stalled stream can become (about `n * block time`). Pi cleanup note: stopping a transient `rtl_tcp` unit with systemd's default `TimeoutStopSec` hangs for 90 s (as the service did before the drop-in); use `TimeoutStopSec` or `systemd-run -p TimeoutStopSec=3`.

- 2026-11-09 iteration 19, **FM seek: remote and local behave the same (no staleness symptom).** Hypothesis from G5: seek steps through frequencies fast, so stale samples after each retune could make it stop on the wrong channel. Test: on the FM Listen
  screen tap SEEK+ three times (14 s apart) on the remote stream from the Pi, then the identical taps on the local V3c. Remote: 96.1 to 96.4 MHz; local: 96.4 to 96.7 MHz; both +0.1 MHz per tap, so seek stops one channel on in both cases (the
  antennas see little there, or SEEK is a one-channel nudge on this screen; not investigated). No remote-only difference. The Pi's server log cannot give dwell times: the Blog rtl_tcp prints through a pipe and its stdout is buffered, so several
  `set freq` lines carry the same microsecond timestamp (flush time, not receipt time). The G5 effect on scanning stays a measured number (0.07-0.12 s), not an observed fault.

- 2026-11-09 iteration 20, **G1 proven on the device: Home's MANUAL gain does nothing on a remote stream.** Home, FM at 96.7 MHz from the Pi, local V3c also plugged in; level read with `RTL_SIGNAL` (`signal_dbfs_tenths`). Start (SMART): -36.1 dBFS.
  Tapped the gain tile (popup titled "RTL-TCP" opens), MANUAL: level -41.1 dBFS and the popup reads "MANUAL GAIN 0.0 dB" (so MANUAL starts at the lowest step). Six taps on "+": -42.5, -42.4, -42.4, -42.5, -42.6, -42.5 (no rise); the popup still reads 0.0 dB with
  the bar on the first segment, confirmed by a screenshot after four "+" taps. Twelve taps on "-": -43.0 to -43.1 (clamped at the bottom). So switching to MANUAL reaches the server (the level changed) but the step buttons do not change the gain. SMART works remotely
  (earlier -1 dBFS and clipping at a fixed 49.6 dB became -21 dBFS under SMART). A likely cause (not verified): the step list comes from the local driver, which has no table for a remote server (the greeting gives only a count, 29, and the tuner type 5);
  the design in section 7 would supply the standard R820T/R828D table from the tuner type. Also seen: the server's own log fills with `ll+, now N` / `ll-, now 0` lines, its linked-list queue being filled and then emptied (dropping) on this link, and its `set ...` command lines are buffered so the server log cannot be used to confirm
  commands; the signal level is the check to use. Not fixed (dashboard behaviour, needs the owner's yes).

- 2026-11-09 iteration 21, **G1 (manual gain on a remote stream) fixed for Home and verified.** Root cause: the setters were already routed to the server (macros in `main.cpp` to `orcsdr::routing::set_*`) but the readbacks the popup uses
  (`esp_rtl_sdr_get_tuner_gain`, `_gain_mode`, `_gains`, `get_rtl_agc`) still read the local dongle's shadow state, so the popup never moved and every "+" computed the same next step. Fix: matching `orcsdr::routing::get_*` that, while a remote stream
  runs, return what was last requested of the server (`rtl_tcp_source::remote_gain()`, since the protocol has no readback) and the nominal gain table for the tuner type in the greeting (`rtl_tcp::nominal_gain_steps`: R820T and R828D 29 steps, E4000 14 steps; refuses to
  guess when the reported count does not match), plus macros redirecting the four getters like the setters. Host test for the tables (strictly increasing, count mismatch gives no table, short buffer not overrun); all rtl_tcp host tests pass. Built, flashed to COM17 and
  checked with the same procedure that failed before (Home, FM from the Pi, `RTL_SIGNAL` level after each tap): SMART -36.2 dBFS, MANUAL -35.9; six "+" taps: -33.9, -33.0, -31.1, -29.0, -25.7, -22.7 (rises at every step, +13.2 dB); six "-" taps: -24.6, -29.1, -29.9, -33.4, -34.0, -36.0.
  The popup text follows: MANUAL GAIN 0.0 dB, then 8.7 dB after six "+" (the sixth step of the table), then 0.0 dB again. Before the fix the same taps left the level flat at -42.5 and the popup at 0.0 dB.
  Not done, still G1: (a) a remote-only setup (no local dongle) still shows gain as N/A and `rtl_gain_set_*` refuse when the local driver handle is null: needs the capability provider of section 7 and a test with the V3c unplugged; (b) the other dashboards' gain controls use the same getters
  so they should follow, but each is unverified; (c) bias tee and offset tuning controls still describe the local dongle.

- 2026-11-09 iteration 22 (health check): Tab5 found in state failed ("no data", 17 connections, 0% link) with the Pi service active and no client attached: the Pi dongle had wedged again (greeting, then no samples; same signature as the earlier wedge). `systemctl restart rtl-tcp` (USB reset in ExecStartPre) cleared it and FM streamed at 104% link straight away. Two observations: (1) the wedge recurs on the Pi, so the trigger is still unknown and a watchdog on the Pi (restart when a client is connected and sends no bytes for 10 s) is worth proposing; (2) the client cannot tell a wedged server from a dead one, which is the G15 wording issue: it says "no data" and gives up. No code change this iteration.

- 2026-11-09 iteration 23 (regression sweep after the G1 getter change): all 19 dashboards opened on the remote source with the Pi stream, no crash (uptime monotonic), no not-OK results. Link per dashboard 80-121% (Dead Air 80%, the lowest; Marine reconnecting at the moment of its reading, 56%); Wi-Fi analysis reads 0% because it does not use the IQ stream. Weather, Marine and Satellite taps land on Home (no screen behind those tiles, as before). Gain getter routing caused no regression.

- 2026-11-09 iteration 24 (G9, ADS-B on remote, 150 s dwell): screen stays on ADS-B, stream state streaming, link 88% at the end (99% in the earlier sampled windows), no reconnect. Decoding still NOT proven: the screenshot capture on the ADS-B screen answers `ORC_SNAP_ERROR encode_failed`, so I could not read the aircraft list, and no serial status exposes the message count. Needs either a message counter in `RTL_TCP STATUS`/an ADS-B status command or a screenshot that works on this screen. A first attempt landed on SSTV instead (the sweep started from the Settings screen); going Home first fixes it, so `sweep` should return Home before tapping. Nothing changed in the firmware.

- 2026-11-09 iteration 25 (health after about 1.9 h of mixed use, 19-dashboard sweep and ADS-B dwell included): Tab5 up 6,911 s with no reset, PSRAM free 1.69 MB (largest block 1.47 MB), internal RAM min 10.5 KB, main stack high-water 4,872 B, 34 tasks; stream streaming at 85% link, 10.39 GB received, 42 MB dropped by the server-side ratio (0.4%), 47 connections of which 18 reconnects (the Pi wedge and my restarts account for them). Pi: service active, 59.9 C, throttled 0x0. No leak or stack growth visible after the sweeps. Nothing changed.

- 2026-11-09 iteration 26 (tooling): `open_dashboard` now refuses to tap a tile unless the Home screen is confirmed (two tries), the guard for the iteration 24 wrong-screen case; smoke test ADS-B and AM both OK. Finding that corrects iteration 24: the screenshot failure is not specific to ADS-B. In the 19-dashboard sweep screenshots worked for the first nine dashboards and returned nothing for the rest, and today AM (which worked then) fails too, with the same `encode_failed` seen on ADS-B. The Tab5 had been up 2.9 h with internal RAM largest free block about 11 KB (13.8 KB earlier), so the PNG encoder most likely fails when internal RAM is fragmented. That is a diagnostic-path limit, not a streaming fault, but it blocks the G9 proof; a reboot would restore it (not done: the owner may be watching, and the stream is healthy).

- 2026-11-09 iteration 27 (owner-approved client changes, built, flashed to the Tab5 on COM17 and checked there; host tests all pass):
  - **G13 fixed.** After the 8 fast attempts the client keeps retrying every 30 s (`BackoffPolicy::slow_ms`, 0 restores the old give-up). Real link: Pi service stopped for about 2 minutes, Tab5 stayed `reconnecting` (never `failed`), Pi restarted, Tab5 streaming again by itself in about 25 s at 95-101% link.
  - **G15 wording fixed.** A connection that is accepted but sends no greeting byte now ends as `server busy or silent` (new `EndReason::no_greeting`) instead of "not an rtl_tcp server", and a stop no longer overwrites the reason that made a start fail. Real link: a PC socket held the Pi's only slot, `RTL_LISTEN FM` timed out and status showed last "server busy or silent"; slot released, next start streamed at 101%. The start-gives-up part was fixed in iteration 29.
  - **RTL_TCP settings page** (owner request): a new page in Settings holding every rtl_tcp setting: receiver source, server address, sample-rate cap (auto up to 1.44 / max 0.96 / 0.24 only), frequency correction (PPM), direct sampling (server default / off / I / Q), offset tuning, a fixed line that the bias tee is never sent to a remote server, and live connection, server and traffic lines with the last failure reason. Radio Defaults keeps one button that opens it. The page selector now scrolls (UP / DOWN) so ten pages fit. On the device: page renders, the rate cap cycles and was put back to AUTO, selector scrolls. NOT checked on the device: PPM, direct sampling and offset tuning taps and their effect on a real HF signal (host-tested only, to avoid leaving changed settings on the unit); persistence across a reboot was seen for source and server only.
  - New serial commands: `RTL_TCP RATECAP 0-2`, `RTL_TCP PPM n`, `RTL_TCP DIRECT 0-2`, `RTL_TCP OFFSET 0|1`.
  - Stock-server finding: flashing the Tab5 mid-stream (client vanishes without closing) left the Pi's `rtl_tcp` V1.4.0 logging "Signal caught, exiting!" while still holding the dead connection, so no new client got a greeting until `systemctl restart rtl-tcp`. This is the server's fault, not the client's; with G15 the Tab5 now names it ("server busy or silent") and with G13 it keeps trying. The owner has ruled out a Pi watchdog (the Pi is a development source only).
  - Cosmetic, seen while testing: the "ACCESSORIES & COMPANION" label is wider than the selector column (it was before this change).

- 2026-11-09 iteration 28 (regression sweep of all 19 dashboards on the remote stream with the iteration 27 firmware, then memory per dashboard):
  - Sweep: no crash, no reconnect failure, link 93-104% on the dashboards that read it. Five dashboards (P25, AM, CB, LoRa, Airband) read link 0% in their sweep row; a 24 s watch of AM after a fresh visit showed 99-100% from the first reading, so the 0% is the sweep sampling a freshly restarted stream before its first window, not a fault (iteration 23 sampled later). Each dashboard restarts the stream, so connections rose 3 to 19 over the sweep: expected.
  - **Finding (open, not RTL_TCP specific): after a full sweep the UI froze on RF Lab.** The sweep ended with RF Lab on screen and `RTL_SCREEN_STATUS visible_updates` stuck at 3131 for minutes; taps (BACK, Home, CLOSE, `UI_DOC_EXIT`) were accepted but nothing changed, while serial, the stream (link 100%) and `RTL_HEALTH` kept working. At that moment PSRAM free was 1.41 MB (min free heap 207 KB) against 21.85 MB on a fresh boot. A reboot (`RTL_RESET CPU`, splash gate off then back ON) cleared it. A second full pass from a fresh boot ended with 1.80 MB free and RF Lab opened and closed normally, so the freeze is NOT reproduced; the working explanation (memory exhaustion near 1.4 MB) is a hypothesis.
  - **Where the PSRAM goes (first visit after boot, then flat on repeat visits, so this is retained buffers, not a leak):** LoRa 14.7 MB (by far the largest), ADS-B 2.4 MB, FT8 1.3 MB, ACARS 1.0 MB, Shortwave 0.5 MB, Dead Air 1.3 MB transient (returned on exit), RF Lab 0.4 MB transient. FM, P25, AM, CB, POCSAG, Airband, SSTV, Wi-Fi and Settings hold nothing. Two rounds over FM, AM, ADS-B, FT8, RF Lab held flat (15.13, 15.02 MB free). A user who visits every dashboard in one session ends at about 1.8 MB free. This is a dashboard question (LoRa's 14.7 MB), so it is reported, not changed.
  - Tooling: `go_home` cannot leave a frozen RF Lab; the sweep then reports WRONG_SCREEN for the next tiles (Wi-Fi, Settings). Correct behaviour for a frozen UI, but the sweep should also record `visible_updates` so a freeze is named as one.
  - Splash gate is ON again.

- 2026-11-09 iteration 29 (owner report: FT8 shows no decodes and the waterfall is not working on the live page; **FIXED and verified on the Tab5**):
  - **Root cause (found with new log lines, not by guessing):** `ft8_native_dial_offset_hz()` asked the LOCAL driver (`esp_rtl_sdr_get_center_freq(g_rtl)`) for the tuner's centre frequency even while the stream was remote. The idle local driver sat at its last FM tune (96.1 MHz), so the FT8 tap was told the dial was 82 MHz away from the centre, could not hold that offset, and reset itself on every block. Every 15 s slot was therefore skipped as incomplete: 0 slots decoded, waterfall noise. With no local dongle plugged in the helper returns 0 and the bug is hidden, which is why the earlier cold tests passed.
  - **Same mistake in Shortwave SSB:** the SSB demodulator read the same local centre to place the carrier, clamped to +-6 kHz, so SSB on a remote stream was mistuned by up to 6 kHz when a local dongle was present. Fixed in the same way (not separately verified on the air).
  - **Fix:** both places skip the local driver while `rtl_remote_active()`; a remote server tunes to the commanded frequency and offers no readback (offset 0).
  - **Verified:** cold open of FT8 on the remote stream (240 kS/s): 4 slots in a row decoded 10, 11, 16 and 10 messages, last slot 12 decoded, 64 candidates, waterfall shows the FT8 tones, `FT8 STATUS` dial_offset_hz 0, one discontinuity at start and none after.
  - **Diagnostics kept:** a skipped FT8 slot now logs why (`why=continuity|ring_start|before_discontinuity|ring_span|rate`, time since the last discontinuity, measured sample rate), and each tap discontinuity logs its cause (`rate_change`, `reset_pending`, `dial_offset`). A skipped slot was previously indistinguishable from a quiet band.
  - The Pi's stock `rtl_tcp` again kept a dead connection after the Tab5 was reflashed and refused new clients until `systemctl restart rtl-tcp` (iteration 27 finding); with no client it never greets, and the Tab5 stays idle until a start is requested.

- 2026-11-09 iteration 29b (G15 second half, owner approved): `start_and_wait` (in `rtl_tcp_source.cpp`, no `main.cpp` change) now stretches its wait from 12 s to 120 s while the server accepts but sends no greeting, and still ends at once on a cancel; any other failure keeps the caller's timeout. Real link: a PC socket held the Pi's only slot, `RTL_LISTEN FM` waited, the holder was released at 25 s, `RTL_START ESP_OK` arrived 26 s after the request and the Tab5 streamed at 99% with the reason "server busy or silent" shown in the meantime. Not tested: a cancel during the long wait (code path unchanged and short).

- 2026-11-09 iteration 30 (audit: reads of the local driver while the stream is remote, the class of bug behind the FT8 failure; read-only, no code change):
  - Method: every `esp_rtl_sdr_get_*(g_rtl, ...)` call in `ui/` (about 70) checked for a remote guard.
  - Already routed to the server by the iteration 21 macros (`main.cpp` lines 174-181): tuner gain, gain mode, gain steps, RTL AGC. These cover the gain popups on Home, FM/AM/CB/Airband, Shortwave, P25, FT8 and SSTV.
  - Fixed in iteration 29: centre frequency for the FT8 tap and the Shortwave SSB carrier offset.
  - **Still read from the local driver on a remote stream (gaps, not changed):** RF Lab runtime page (sample rate, PPM, bias tee, bandwidth, device info, health: lines 7271-7303; shows the local dongle's values or nothing), the status readout (`RTL_DRIVER STATUS`, lines 19118-19163, reports IDLE with the last local tune), the dashboards' bias-tee state (bias is never remote by design), and `esp_rtl_sdr_is_rate_supported` gates before FT8, ACARS and SSTV low-rate requests (answered by the local driver's rate table, which is the same table for any RTL2832, so correct while no dongle is present is unverified: see below).
  - **Lesson for the matrix:** every earlier remote test ran with the owner's Blog V3c plugged into the Tab5, and the FT8 bug only existed in that configuration. The "remote only" column (no local dongle) was never run for G1 and G2 after iteration 8 and is still open: it needs the V3c unplugged by the owner. Until it is run, remote-only claims for gain, FT8, ACARS and SSTV stand on the iteration 5 cold tests only.
  - Next test candidate that needs no hardware change: SSB tuning accuracy on a remote Shortwave stream against a known carrier (a WWV tone at 10 MHz), to confirm the +-6 kHz offset fix; not run (no signal confirmed at this hour).

- 2026-11-09 iteration 31 (REMOTE-ONLY column, owner unplugged the V3c; 20 m antenna on the Pi dongle):
  - **FT8 decodes with no local dongle:** cold open, `RTL_START rate=240000 display_hz=14074000`, one tap discontinuity at start, first slot 8 decodes (owner confirmed on screen). `esp_rtl_sdr_is_rate_supported` therefore answers correctly without a local dongle for FT8.
  - **ACARS** opened at 240 kS/s (`display_hz=131490000`) and streamed at 100% link; decoding not checked (no aircraft traffic confirmed). **SSTV** on a fresh server restart opened with one connection, 240 kS/s at 3.519 MHz, 107% link.
  - **Server wedge, again, and a hypothesis I withdrew:** after FT8 -> ACARS -> SSTV in quick succession the Pi's dongle stopped delivering samples (greeting, set rate/frequency, "worker cond timeout" in the server log, no IQ), the Tab5 showed "no data" and reconnected every few seconds; from the PC every request returned 0 bytes, including 14.074 MHz that had worked minutes earlier, until `systemctl restart rtl-tcp`. I first read the server log as the Tab5 reconnecting several times per dashboard open; those lines were my own PC test client (a different source address). A clean measurement after a restart shows ONE connection for an SSTV open. So the cause of the wedge is not shown to be client churn; it follows quick switching, and from the PC a second client right after another sometimes got nothing (3 of 4 runs fine). Stock `rtl_tcp` V1.4.0 on this Pi/dongle is the weak link; the client's job is to stay patient (G13) and say what is wrong (G15), both now done.
  - Not yet run in this column: manual gain popups on Home and the dashboards (G1 remote-only), ACARS/SSTV decode proof, a longer FT8 soak.

- 2026-11-09 iteration 32 (G1 in the REMOTE-ONLY column, no local dongle: **FAILS**): on FM, AM and FT8 (Shortwave band) every `RTL_GAIN MANUAL 200`, `MANUAL 400` and `AUTO` returned `result=ESP_RTL_SDR_ERR_UNSUPPORTED`, `RTL_GAIN STATUS` reported `gain_available=0 cap_auto=0`, mode MANUAL and gain 0, and the Pi's server log showed no `set gain` line during the test. The iteration 21 fix (setters and getters routed to the server) only works while a local dongle is also present, because the capability checks in front of the routing (`rtl_has_device_capability`, `rtl_tuner_gain_available`, `g_rtl != nullptr`) still consult the local driver. So a user with only a remote server cannot change gain on any dashboard. This is the case the source-interface design (capabilities from the remote greeting: tuner type, gain steps) exists to fix; owner approved it in phases, none built yet. Also seen: `RTL_AM_GAIN STATUS` and `RTL_FM_GAIN STATUS` read gain only when `g_rtl != nullptr`, so they report 0 on a remote-only unit.

- 2026-11-09 iteration 33 (**G1 remote-only FIXED and verified**; source-interface phase 1, owner approved): new module `ui/source_capabilities.{hpp,cpp}` states what a remote rtl_tcp server can do from its greeting (stream, retune, manual gain when the greeting reports gain steps, tuner AGC, RTL AGC), as a pure function with a host test (`tests/source_capabilities_host_test.cpp`, in `tools/test-rtl-tcp.sh` and `main/CMakeLists.txt`). `rtl_device_capabilities()` in `main.cpp` returns those bits while a remote stream runs, so every existing gate (`rtl_tuner_gain_available`, `rtl_has_device_capability`) now answers for the server's dongle, and the three `rtl_gain_set_*` functions no longer insist on a local driver handle. It claims only what the protocol can carry: no readback, no bias tee, no direct sampling, no HF path. Local USB behaviour is unchanged (the bits are the driver's whenever the stream is local). Real link, V3c unplugged: `RTL_GAIN MANUAL/AUTO` on FM, AM and FT8 all `ESP_OK` (was `ERR_UNSUPPORTED`); `RTL_GAIN STATUS` shows `gain_available=1 cap_auto=1 cap_rtl_agc=1` and the 29-step table; on FT8 the measured IQ level followed the setting: gain 0 -37.3 dBFS, 200 -12.9, 400 -2.1, back to 0 -38.0, tuner AGC -6.3. NOT checked: the on-screen gain popups on each dashboard (the serial path shares the setters and the gates, the popups were not tapped), `RTL_FM_GAIN STATUS` / `RTL_AM_GAIN STATUS` (still read the gain only when a local handle exists, they report 0), and the Pi's server log (it is buffered, so it could not serve as evidence).

- 2026-11-09 iteration 34 (**IQ drops fixed**; the owner stopped the test loop after this): the Tab5 dropped about 6% of the incoming samples (28-30 KB/s measured on FT8 and on Home, 0 on ACARS), which made FT8 skip every slot as incomplete (measured tap rate 11.3 samples/ms instead of 12). Memory was not the cause (PSRAM 12 MB free, internal RAM 41 KB free, lowest 20 KB). Cause: the client waits only 40 ms for the DSP to free one of 3 slots (about 80 ms of slack at 240 kS/s) before reading the bytes into scratch and counting them lost, so any short DSP or screen stall threw data away. Fix: `SessionConfig::ring_wait_ms`, default 250 ms (TCP holds the data back meanwhile, so a stall becomes a little delay, not loss). Host test `test_short_dsp_stall_loses_nothing` (a 150 ms stall with a 600 ms wait drops 0 bytes); the old full-ring test is pinned to 40 ms. Real link after the flash: Home 0 B/s dropped at 2.95 MB/s, FT8 0 B/s at 481 KB/s, link 105-149%. Not measured: whether a longer wait adds audible lag on FM at 1.44 MS/s (not judged by ear), and FT8 decoding after the fix (the antenna on the Pi had been changed for ACARS, input level -38 dBFS, 0 decodes in the first minute).
- Remaining blockers as of this entry: gain popups on the other dashboards with no local dongle (FT8 checked); ACARS, SSTV and ADS-B decode proof; PPM, direct sampling and offset tuning on a real signal; LoRa's 14.7 MB retained PSRAM and the one-off RF Lab freeze; merge main, full release gate and the changelog entry; internet path and audio by ear (waiting on the hotspot choice).

- 2026-11-09 iteration 35 (**HTTPS at 20 MHz SDIO: PASSED** on the merged PR 240 build, COM17): the SDIO clock was lowered to 10 MHz on 2026-08-23 (`d7f43fe6`) because "sustained HTTPS receive is not stable at 20 MHz on this P4/C6 pair"; this branch raised it back to 20 MHz on 2026-11-07 and the HTTPS soak was still open. Test: `RTL_CATALOG_CHECK` (signed catalog verified in 5 s), then `RTL_CATALOG_INSTALL faa_aircraft`, the largest published pack: runtime index 35.7 MB plus source archive 73.0 MB, about 108.6 MB over HTTPS in 77 s (about 1.41 MB/s), result "Pack installed and verified", no reset (uptime monotonic), no Wi-Fi drop. The remote FT8 stream was paused by the app during the install and resumed by itself afterwards (99% link). Internal RAM low-water 19.6 KB -> 18.9 KB; largest internal DMA block 24.6 KB -> 19.5 KB after the install. The catalog worker ended and was reaped (task count back to its pre-install value), the first on-device run of that change. C6 on this unit: ESP-Hosted 3.0.6. Older C6 firmware with 20 MHz: not tested (owner: 20 MHz is documented for earlier versions; treated as supported).

- 2026-11-09 iteration 36 (**ACARS decodes on a remote stream**, owner set up the antenna and reported it): COM17 on the merged PR 240 build, source remote (Pi 4B, stock RTL-SDR Blog `rtl_tcp` 1.4.0, header "RTL-TCP LIVE 0.250 MS/s 192.168.1.219:1234"), 136.975 MHz, receiver running. One CRC-verified message on screen at 04:29:51 UTC: tail N8717M, flight WN10..., label H1 ("CRC OK", 1 of 48 recent blocks held, 0 repairs). Screenshot taken. One message is proof the remote path decodes ACARS, not a rate or sensitivity figure. `RTL_ACARS_STATUS` did not answer over serial on this build (not investigated).
  - Four minutes later: 7 CRC-verified messages, 0 repairs, from three aircraft (N77536 / UA1811 with five messages including the route "KSFO to KPDX", N58CG / XA58CG label 44 position report, N8717M / WN10..), link 96%. Screenshot kept as release evidence: [docs/evidence/rtl_tcp/2026-11-09-acars-remote-rtl-tcp.png](evidence/rtl_tcp/2026-11-09-acars-remote-rtl-tcp.png).

- 2026-11-09 iteration 37 (**SSTV decodes over rtl_tcp: replayed Robot 36**; the owner asked for the decoding path to be proven, not over-the-air reception): new test tool `tools/rtl_tcp_replay_server.py` is a fake rtl_tcp server on the PC (real 12-byte greeting, 5-byte commands in, raw 8-bit IQ out paced in real time, binds only to a named address, refuses 0.0.0.0). It replays the repo's model-transmitter audio (`tools/sstv_make_wavs.cpp`, ORC test pattern) as narrow-FM IQ at 240 kS/s, 2.5 kHz deviation, carrier amplitude 0.5, light noise (sigma 0.01), with 5 s of noise between plays. The Tab5 (COM17) was pointed at it with `RTL_TCP HOST 192.168.1.15 1235` over the home Wi-Fi, the SSTV dashboard opened and tuned to 145.5 MHz (narrow FM; the server ignores frequency). Result: VIS header read ("ROBOT 36", VIS headers OK 2, BAD 0), a complete image of 240 rows with sync locked 240 and missed 0, line error 0.3 ms, audio gaps 0, overruns 0, link 99-100%. The decoded picture shows the full test pattern with correct colours; look at [the screenshot](evidence/rtl_tcp/2026-11-09-sstv-robot36-replayed-over-rtl_tcp.png): slight horizontal wobble in the colour bars and one small green mark at the top right are visible (cause not investigated: PC-side pacing, Wi-Fi bursts or the FM demodulator). The first play was cut short when the stream restarted after the tune (1 partial image), the second played whole. This proves the decode path (SSTV audio tap and decoder fed by a remote rtl_tcp stream at 240 kS/s); it does not show over-the-air sensitivity. Settings afterwards: server back to 192.168.1.219:1234, SSTV dial back to 3.519 MHz, FT8 streaming from the Pi.

- 2026-11-09 iteration 38 (**internet hop: the shortfall was a Tab5 receive bug, not distance. Fix VERIFIED with a synthetic burst server on the LAN; the real rtl_tcp server through a relay or the cellular hop has NOT been re-measured since the fix and is still pending**). Owner requirement: a dongle across the world must stream to OrcSDR, so this is a release blocker. Setup: the Pi (V4L) moved to the owner's phone hotspot "Wifi" (open, cellular), reached over Tailscale (path via the PC's public address, 38-65 ms), relayed to the Tab5 by an SSH forward on the PC (192.168.1.15:1236 -> hotspot address). Findings, in order:
  - Verified on the real cellular hop (synthetic source): SSTV Robot 36 (a Python server on the Pi serving pre-rendered IQ) across the real cellular hop: link 98-101% for 40 s, full frame decoded, sync 240 locked / 0 missed (screenshot `docs/evidence/rtl_tcp/2026-11-09-sstv-robot36-over-internet-hop.png`). A PC client through the same relay with the Pi's stock rtl_tcp: 99% at 240 and 960 kS/s (15 Mbit/s); the Tab5's receive-only benchmark pulled 1.89 MB/s through it. So the hop itself carries the stream.
  - But the Tab5 with the Pi's REAL rtl_tcp averaged 73-82% of the 240 kS/s rate (link swinging 4-141%) through the relay. Control with the Pi back on the LAN and the same relay: 82%. A plain TCP proxy (`tools/tcp_delay_proxy.py`) at 0 / +40 / +100 ms one-way: 65 / 77 / 67%. So distance and latency are NOT the cause; any intermediary between the Tab5 and stock rtl_tcp triggered it.
  - Reproduced with no Pi and no relay: a synthetic server sending the same IQ smoothly (4 KB blocks) or in 32 KB bursts gave the Tab5 100%; in 256 KB bursts (what stock rtl_tcp's async buffers send, one every 0.55 s at 240 kS/s) 76%.
  - Cause: `CONFIG_LWIP_TCP_RECVMBOX_SIZE` was 64 while the advertised window is 262,140 bytes (MSS 1440). ESP-IDF's own help text recommends window / MSS + 2 = 184. 64 entries hold about 92 KB, so a burst larger than that overflowed the mailbox, the segments were dropped and TCP stalled to recover. Fix: 192 in `sdkconfig.defaults` (allowed up to 1024 with window scaling). After: 256 KB bursts 99% (smooth 100%). Not yet re-measured: the Pi's real rtl_tcp through a relay or the cellular hop after the fix (the Pi's dongle had no usable signal at the time, but the byte rate is measurable regardless; planned for the next loop runs).
  - Why direct LAN to the Pi hid it: not established (probably packet timing).
- 2026-11-09 iteration 39 (**remote stream started at a stale gain; fixed and verified**): after a Tab5 reset, or after another dashboard (FM smart gain leaves a low manual gain), a new remote stream sent no gain command and the stock rtl_tcp, which keeps tuner state across clients, stayed at whatever the last client left: FT8 read -43.6 dBFS (about 28 dB too low) while the screen said AUTO, and nothing decoded until a gain command was sent. Fix: `rtl_tcp_source::reset_gain_to_auto()` (tuner AGC on, digital AGC off) at start-up and whenever a remote stream starts for a different band; a same-band restart (rate step) keeps the user's gain. Verified: the Pi's server left at manual 0 dB, cold connect, no gain command: -4.0 dBFS and `RTL_GAIN STATUS mode=TUNER_AGC`.
- 2026-11-09 iteration 40 (**FT8 decodes over rtl_tcp with no radio signal**; band conditions at night cannot be relied on): new fixtures `tools/ft8-make-slot-wav.cpp` (real FT8 messages from the production codec, Gaussian CPFSK, 8 stations per 15 s slot, SNR +4 to -12 dB, noise in 2500 Hz convention) and `tools/rtl_tcp_replay_server.py` in two separate steps (`--usb` applies only to `--render`, `--align-utc` only to `--iq`): render once with `python tools/rtl_tcp_replay_server.py slots.wav --render slots_240k.cu8 --rate 240000 --usb --level 2.0 --noise 0`, then stream with `python tools/rtl_tcp_replay_server.py --bind <one LAN address> --port 1235 --iq slots_240k.cu8 --rate 240000 --gap 0 --align-utc 15` (`--gap 0` so the default gap does not shift the first slot; the file must be whole 15 s slots; it starts on a UTC slot boundary and binds one named address only). The production backend on the host decodes 8/8 in every slot of that audio. The Tab5 over rtl_tcp on 40 m (FT8 dashboard, stream at -16 dBFS): first run 2-8 per slot while the stream was restarting, then a clean run with 8/8 in 6 of 7 slots (the seventh cut by a reconnect), DT -100 to +260 ms, link 99-100%, nothing dropped. Nothing was transmitted: these are samples over TCP. Side effect to know: the Tab5 logs decodes to the SD card (`/orcsdr/ft8/log-YYYYMMDD.csv`) and its Map/Heard lists, so the eight fictitious callsigns (K1ABC, W9XYZ, JA1XYZ, DL1ABC, VK2RF, N7QQ, EA6VQ, PY2APK) are in COM17's log and lists; the FT8 code has no network upload.
  - Real-signal status: the Pi's V4L with the MLA-30+ hears almost nothing on 40 m (`rtl_power` on the Pi: FT8 window +1.7 dB over the median, top bin +5.7 dB) while the same dongle and antenna decode at once in the Tab5. Unresolved; power to the loop, placement near the Pi and cabling are the suspects, the driver is not shown to be at fault (the same stock rtl_tcp decoded FT8 on 20 m earlier the same day).
  - The owner's resets of the Tab5 twice wiped its rtl_tcp server and source settings; a clean `RTL_RESET` keeps them (reason code 7 is what a clean restart reports too). Cause of the loss not established.
- 2026-11-10 iteration 41 (**REAL FT8 decodes over rtl_tcp; two client bugs found and fixed, verified on the Tab5**). Setup: Pi 4B with the Blog V4L and the MLA-30+ on 40 m (7.074 MHz), stock Blog `rtl_tcp` 1.4.0, Tab5 (COM17) over the home LAN. Ground truth the same minute: the Pi's own capture and the production decoder on the Pi found 22 decodes in 6 slots (3.7 per slot); the same stream pulled to the PC and cut into slots decoded 2-5 per slot with tuner AGC or manual 40 dB, so the stream content was good while the Tab5 decoded 0 in 32 slots at 100% byte rate. Cause 1 (the one that produced zero): a hot retune (`FT8 BAND`, the band buttons) on a remote stream quantizes the LO to 5 kHz, but the FT8 tap and the SSB demodulator assumed the LO equalled the dial, so the passband was mis-tuned by up to 5 kHz; fix: on a remote stream the centre is the last LO sent to the server (`rtl_tcp_source::status().frequency_hz`). Cause 2 (found with a logging relay that printed the Tab5's commands): switching back to AUTO gain left the manual gain value in the desired state and every reconnect sent `gain 100` after `gain mode = auto`; librtlsdr treats a gain command as a switch to manual, so the tuner ran at a stale manual gain while the screen said AUTO; fix: no gain value is sent unless manual mode is selected, and AUTO clears it (host test `test_auto_gain_sends_no_gain_value`). Result after both fixes, same script that failed before (including the band hop): **77 decodes in 14 slots, 5.5 per slot (min 2, max 8)**, byte rate 100% then the link adapter stepped up on its own, no drops, no reconnects. Also seen: tuner AGC on this dongle and antenna overloads the front end (37% clipped samples) yet FT8 still decodes at about 4 per slot; manual 40 dB (no clipping) decodes the same. Also seen: after a 5-minute run the Tab5's Wi-Fi was found disconnected with scans failing; it recovered only after a reset (`RTL_WIFI_CONNECT_SAVED` and a C6 power command did not help); not reproduced, not yet understood. Open review findings (client and main.cpp reviewers) are listed in the follow-up notes below.

### Iteration 42 (2026-11-10): review findings, first batch

Server refuses `0.0.0.0` in `run_server` and the worker waits on a `0.0.0.0` Wi-Fi address (host test covers `0.0.0.0`, `0.0.0.00`, `00.0.0.0`). Client sends ppm 0, offset 0 and direct 0 on every connect, before the frequency (host test checks the byte order). Flashed on COM17: live FT8 over rtl_tcp, 4 min, 63 decodes in 15 slots, 100% link, no drops or reconnects. Deferred, not reproduced: the 2 KB stall-drain scratch (client finding 3). Others remain open.

### Iteration 43 (2026-11-10): connect-wait and boot recovery

A band change made while a remote stream was still connecting is no longer lost (the cancelled start restarts with the newest request). A remote stream that failed at boot is retried every 30 s with the last requested band. Flashed on COM17: Pi server stopped, Tab5 failed to connect, server restarted, stream came back by itself in about 30 s with no reboot. Live FT8 on 20 m, 4 min: 105 decodes in 15 slots (7.0 per slot), 100% link, no drops. A run started right after that recovery decoded one slot only (link adapter had stepped the rate up); not investigated further. The cancelled-start path itself is not exercised by a test.

### Iteration 44 (2026-11-10): real rtl_tcp across the cellular hop, FT8

Setup: the Pi (V4L, stock rtl_tcp 1.4.0) on the phone hotspot "Wifi" (10.110.212.7), reached over Tailscale; an SSH forward on the PC (192.168.1.15:1236 -> Pi:1234) relayed it to the Tab5 (COM17), which stayed on the home Wi-Fi. FT8 dashboard on 20 m (14.074 MHz), 8 minutes: 194 decodes in 31 slots (6.3 per slot, min 3, max 12), link 88-109% (mean 100%), 480 kB/s, 0 dropped, 0 reconnects. This closes the pending re-measurement from iteration 38: the receive-queue fix holds with the real server through a real internet path. Not covered: a server bound to a public address with no relay, and long duration. Gotchas: the Pi's server binds to its Wi-Fi address, so the forward target must be that address (not 127.0.0.1); the server takes one client, so a reconnecting Tab5 can hold the slot while another client tests. Lab restored afterwards: Pi on home Wi-Fi (192.168.1.219), Tab5 host 192.168.1.219:1234. Full setup, procedure and pass criteria: [RTL_TCP_INTERNET_HOP_TEST.md](RTL_TCP_INTERNET_HOP_TEST.md).

### Iteration 45 (2026-11-10): server restores the tuner; boot start follows an open dashboard

The rtl_tcp server (Tab5 serving its own dongle) now saves gain mode, gain, digital AGC and ppm when a client connects and puts them back when it leaves, so a remote client's commands no longer change the local app's tuner. Built and flashed on COM17, but **not exercised**: COM17 has no dongle plugged in, so no client was ever served. The boot-time remote start now uses the band a dashboard already recorded (instead of always FM) when one was opened before the stream could start. One FT8 session after a flash sat on an FM-rate stream with the FT8 screen open (tap off, 0 decodes for 3 minutes); sending the band again did nothing because it was unchanged, and a different band restarted it correctly at 240 kS/s. It did not reproduce after the boot-band change (fresh boot, FT8 opened: 72 decodes in 11 slots), but the cause is not proven. Internet-hop setup and pass record: [RTL_TCP_INTERNET_HOP_TEST.md](RTL_TCP_INTERNET_HOP_TEST.md).

### Iteration 46 (2026-11-10): pinned rate is not the link adapter's; real failure reason

The remote link adapter no longer steps a stream whose rate is pinned (FT8's 240 kS/s override): a bad moment on the link used to restart FT8 at the same rate and cost a slot, and a probe up could move it off 240 kS/s. A failed remote start now shows the reason (`remote: connect failed`, `not an rtl_tcp server`, `server busy or silent`...) instead of one fixed sentence. Flashed on COM17: live FT8 on 20 m for 6 minutes, 135 decodes in 23 slots, none skipped, 0 dropped, 0 reconnects. The new message text was **not seen on screen**: with the server stopped the stream sat in `reconnecting`, which is a different status path. Still open from the reviews: the 2 KB stall-drain scratch (not reproduced), control-surface doc mismatches, ADS-B at 2.048 MS/s against a 2 MB/s link.

### Iteration 47 (2026-11-10): ADS-B message, Wi-Fi keep-alive toggle, health log, doc corrections

- `docs/RTL_TCP_CONTROL_SURFACE.md` corrected against the code (client states and reconnect policy, the new commands, which server commands are applied versus counted as unsupported, the tuner restore, the 192-entry receive mailbox, the ADS-B bandwidth).
- ADS-B on a remote stream now says what it needs: "listening (remote; ADS-B needs ~4.1 MB/s)", and "remote link too slow for ADS-B (needs ~4.1 MB/s)" when behind. The decoder stays at 2.048 MS/s. **Not seen on screen** (ADS-B was not opened on the remote stream).
- Settings > RTL_TCP has a KEEP WI-FI ALIVE toggle (serial `RTL_TCP WIFIKEEP 0|1`), off by default, saved. Shown on COM17 ([screenshot](evidence/rtl_tcp/2026-11-10-settings-rtl-tcp-keep-wifi-alive.png)); serial and touch toggles verified. The recovery itself is **not exercised**: it needs a Wi-Fi outage of more than 5 minutes with the toggle on.
- `RTL_WIFI_HEALTH` is logged once a minute while a remote receiver may need Wi-Fi (one sample seen: connected, no failures, stream streaming, DMA free 23,819 B; the RSSI field reads 0, so it is unreliable). The give-up logic found while reading: reconnect gives up after 5 tries (about 3.7 minutes) and C6 recovery after 3; the recovery budget refills only after 5 minutes connected. A Wi-Fi outage longer than about four minutes therefore ends in "restart Wi-Fi". This fits what was seen, but is **not proven to be the wedge**: the evidence from the original event was not captured.
- Settings page rows are 48 px apart (were 54) to fit the new row; the touch test was updated.
- Flashed on COM17: live FT8 on 20 m for 3 minutes, 76 decodes in 11 slots (one slot skipped at start), 0 dropped, 0 reconnects.

### Iteration 48 (2026-11-10): Wi-Fi outage test on a phone hotspot; the wedge reproduced and fixed

Setup: the Tab5 (COM17) joined the phone hotspot (password typed on the Tab5 by the owner), remote source selected, a serial logger recording `RTL_WIFI_*`. The hotspot was switched off for about 9 minutes.

- **Run 1, keep-alive OFF: wedge reproduced.** The Tab5 lost the link at 14:22:02, retried 5 times (3 s to 120 s backoff) and logged `RTL_WIFI_RECONNECT_GIVE_UP` at 14:26:01. The hotspot came back at about 14:31 and the Tab5 stayed offline (`connected=0`, `reconnect_attempts=5`, down 614 s) with nothing left to retry. This is the "Wi-Fi never comes back until a reset" failure seen earlier: the reconnect budget is bounded and only refills after the link has been up for 5 minutes.
- **Run 2, keep-alive ON, from that stuck state:** the toggle re-armed the budget (`RTL_WIFI_KEEPALIVE rearm down_s=670`) and the Tab5 reconnected 7 seconds later. This was recovery from the given-up state with the hotspot already back, not a second 9-minute outage.
- **Second defect found: "Radio pause failed".** After a reboot with no saved network, every Wi-Fi scan and connect failed with `radio_pause_failed` (a 5-second main-loop stall each, and each failure also used up a reconnect attempt), so Wi-Fi could not be set up from the touch UI at all. Cause (from the code, reproduced only once and not with a saved network): the receiver stays marked `queued` when no source can start it (remote source, Wi-Fi down), and the pause waited for `queued` to clear, which only the driver task does. Fixed: the pause no longer waits on a `queued` stream that nothing can start, and the Wi-Fi resume only queues a stream when a source is usable. Flashed; 12 scans over 3 minutes with a saved network and an unreachable server all succeeded (they also succeeded before the fix, so this does not prove the fix; the failing case needs a reboot with no saved network and was not rerun).
- Added `RTL_RADIO_PAUSE_FAILED` (receiver state when a pause gives up).
- Not tested: the toggle through a full outage start to finish, and the stream resuming afterwards (the Pi was not on the hotspot).

## 6. Next iterations (in order)

1. Fix the retune detector and repeat at all rates and at `-b 2`; add a Tab5-side check of what FM seek dwell is.
2. DONE (see 5a): all 19 open; the earlier failure was G11, the serial `UI_DOC_SHOW` crash.
3. Prove G1 and G2 on the Tab5 (open FT8 and ACARS manually, read `RTL_START` and the Pi's `set sample rate` log).
4. With the owner's go-ahead: fix G4 (back-off), then G1/G2/G3 behind the existing source routing, one dashboard family at a time.
5. Failure injection (L3): kill the server, drop the dongle, Wi-Fi off and on, wrong host, busy server; record behaviour and recovery time.
6. Parity pass (section 4) with the real clients if available on the PC.

## 7. Design proposal (NOT implemented): active source capabilities

Problem (gaps G1, G3, G6, G7): dashboards ask the local USB driver what the receiver can do (`rtl_device_capabilities()`, `g_rtl`, `esp_rtl_sdr_get_*`), so
with a remote source and no local dongle gain, AGC and metrics read as unavailable, and with a local dongle also plugged in they describe the wrong device.

Proposal: one small module, `ui/source_capabilities.{hpp,cpp}` (not in `main.cpp`), that answers "what can the ACTIVE source do" for both source kinds.
The existing `receiver_tuning_controls.hpp` `Capabilities {rf_gain, tuner_agc, rtl_agc, offset_tuning}` and `Availability {enabled, unavailable, hidden}` stay the interface
dashboards already use; only the place the capability bits come from changes.

| Field | Local USB | Remote rtl_tcp |
|---|---|---|
| `rf_gain`, `tuner_agc`, `rtl_agc` | driver caps | true (the server accepts 0x03, 0x04, 0x08); value shown is the last one requested, marked "unconfirmed" (no readback) |
| `offset_tuning` | driver caps | false for tuner 5 and 6 (R820T, R828D); the Blog fork also toggles bias on 0x0A |
| `bias_tee` | driver caps | false always (safety policy) |
| `min_tune_hz` | by profile (24 MHz without an HF route) | tuner 6 (Blog V4): "likely HF"; tuner 5: 24 MHz unless the owner chooses direct sampling; tuner 1 (E4000): 52 MHz |
| `retune_settle_ms` | about 0 | measured about 120 ms at 240 kS/s and 70 ms at 0.96 and 1.44 MS/s (stale samples after a retune, G5), plus the link round trip; scans and seeks use it as the minimum dwell |
| health | USB overruns, drops | link %, dropped bytes, reconnects (G3) |
| hardware lab (RF Lab controls, V3 HF route) | yes | "needs a local dongle" |

Each unavailable item carries a reason string, like today's "below 24 MHz unavailable": `needs local dongle`, `server tuner has no HF`, `link too slow for this band`.
`active_source()` already exists (`rtl_choose_source`, `g_rtl_active_source`); the provider reads it, so a local dongle plugged in while the stream is remote no longer leaks its capabilities.

Implementation order (each step built, flashed and checked on the Tab5, host-tested where it is pure logic):
1. Provider plus host tests for the capability table (pure, no hardware).
2. Home: gain and AGC follow the provider (it already routes gain to the server through `rtl_source_routing`).
3. Gain on the dashboard families, one family per change: FM/AM/CB/Airband/Shortwave, then P25, FT8/SSTV.
4. Health cards (FM, P25, Home footer): remote link, drops, reconnects.
5. Scan dwell from `retune_settle_ms` (FM preset scan, Shortwave hunt, Airband).
6. Reason strings on controls and the RF Lab "local dongle only" note.

Risks: offering gain controls that cannot confirm they applied (mitigation: show requested value, label it); changing every dashboard's control state for remote users (owner approval needed per family);
the tuner type is known only after the greeting (the provider reports "unknown" until connected).
