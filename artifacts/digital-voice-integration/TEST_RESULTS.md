# Test results (real runs; logs in logs/)

## G1 UI host failure
Before (log g1_before_release.log): link error
`undefined reference to orcsdr::screens::owns(orcsdr::screens::Id)` in dv_dashboard_surface_contract.
Cause: the test defined `g_owns` but never the `owns()` symbol the real Surface calls.
Fix (commit c2ed0406): test double `owns(Id id){return id==Id::digital_voice && g_owns;}`.
Ownership checks in Surface are untouched; the test still asserts they gate draw/update/touch.

## Host suites on merged tree 866a380d (WSL, run_host_suites.sh)
| Suite | Release | ASan/UBSan |
|---|---|---|
| Digital Voice UI (2 tests) | PASS | PASS |
| components/orcsdr_dmr D0 (3) | PASS | PASS |
| dmr d1_integration (2) | PASS | PASS |
| tests/dmr_protocol_conformance (7) | PASS | PASS |
| orcsdr_digital_voice_rx D-STAR/YSF (1) | PASS | PASS |
| orcsdr_nxdn (1) | PASS | PASS |
| orcsdr_radio_discovery (2) | PASS | PASS |

NOT run yet: pinned real-IQ replay gates (need fixture downloads), Python replay-acceptance tests,
existing P25 core host tests, any firmware self-check.
Sanitizer note: UI/DMR use their own sanitizer options; others use -fsanitize=address,undefined via CXX flags.

## Live-chain host suite (tests/digital_voice_live) - Release and ASan/UBSan PASS
Channelizer tone/reject, scanner state machine (refusal, dwell, hold/extend/release, manual hold, skip, pause, receiver
removal), DMR end-to-end (synthetic IQ -> 240 kS/s CU8 -> src 3024109 / TG 99 / CC 1), NXDN48/96 RAN 0 valid with no
inferred identity, noise = zero events, worker-ring path == inline path (9 = 9 events), discontinuity reset, restart.

## Real RF, PC baseline (RTL-SDR Blog V4 + LNA via bias tee, Eugene OR), rf/ directory
| Capture | Result through the production chain |
|---|---|
| 152.1575 MHz Fleetnet Terra DMR Tier III control, 30 s | 367 integrity-validated DMR events, CC 13, slot 1 (cadence-validated), no identities (CSBK) |
| 454.225 / 454.350 / 453.950 NXDN control, 30 s each | 334 / 216 / 320 FSW+LICH frames, 0 SACCH-valid (control channel carries CAC, not decoded) |
| 454.575 NXDN control (wide capture DDC) | 36 frames, 0 valid |
| 454.125 voice channel, 5 min; 454.125/.150/.025, 453.725 40 s | no NXDN frames (activity not NXDN or too sparse) |
| 444.449 MHz | 4 kHz-wide constant carrier: not DMR |
Optimizations (DMR float pre-screen + modulo-free FIR; NXDN float trig/atan2 + multi-accumulator FIR) gave identical
counts on all real captures. NXDN48 half-rate path: 361/223/310 frames on the controls, 5 of 7 synthetic events.

## Tab5 measurements (COM17, M5Stack Tab5, ESP32-P4)
Real-time cost of each decoder on its own worker core (1.0 = one full core): DMR 0.26, NXDN96 0.84, NXDN48 1.29 -> 0.46
after half-rate. All three together exceed one core, so a channel runs one decoder; an "auto" entry rotates decoders
every 400 ms. DSP task: channelizer ~20 % of its core, no resets, no lost IQ.
On-device replay of the real Terra capture: events accumulate, 0 ring drops. Live scan of a 5-channel Lane County plan:
3+ passes, 17 retunes all accepted, 0 drops, 0 IQ loss, worker 35 %, NXDN48 frame sync on real on-air control channels.
NOT yet shown on the Tab5 itself: a live DMR event or any SACCH-valid NXDN event (antenna/bias; see RISKS).
