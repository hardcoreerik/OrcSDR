# OrcDial and Tab5 boot reliability, 2026-10-06

Follows [the investigation handoff](orcdial-investigation-handoff-2026-10-06.md). Everything here was
measured on one Tab5, one OrcDial and one router (channel 11), with all boot items enabled
(Wi-Fi at boot, OrcDial boot connect, auto-start reception). Raw logs are under
`orcdial/.pio/wifi-investigation/c6-uart/` of the working checkout and are not committed.

## Method

`apps/orcsdr-tab5/tools/run-tab5-boot-cycles.py` sends an authenticated `RTL_RESET` (or another trigger),
then polls Wi-Fi, Hosted link, capture, OrcDial and audio until all are back or a timeout passes. It logs
the Tab5, the OrcDial and the ESP32-C6 UART side by side. Modes: `p4` (restart the P4 only), `c6pulse`
and `c6off_reset` (cut the WLAN rail; need a `-C6FaultTest` build), `c6rts` (reset the P4, then pulse the C6
EN line through the downloader about 1 s later, while the P4 is still booting) and `dial` (restart only the OrcDial). Random dwell between cycles
(`--dwell-range`, seeded) varies the phase between the Tab5 and the OrcDial channel sweep.

## Findings

### 1. The C6 downloader adapter hides C6 resets

With the adapter attached, cutting the WLAN rail for up to 4 s did not reset the C6: no ROM banner, and
Wi-Fi never dropped. With the adapter unplugged the same cut dropped Wi-Fi. The adapter's own supply keeps
the C6 powered, so every test with it attached was a test of a C6 that could not be power-cycled.
Consequence: results from adapter-attached P4-only resets say nothing about cold start.

### 2. OrcDial intermittently failed to connect at boot (fixed)

Baseline, adapter attached, 20 randomised P4 resets: OrcDial connected 18/20; the two failures stayed
failed for 30 s and recovered within 0.2 s and 3.7 s of an explicit Connect.

Mechanism, from the OrcDial's per-channel send and receive counters: the tablet is on the router channel
(11), but a strong transmitter is also heard on neighbouring channels, so the OrcDial received a valid offer
on channel 7 or 10 and locked there. Its replies went unacknowledged (66 failed sends) and it stopped
scanning, while the tablet gave up after 12 attempts (about 15 s) and never retried.

Changes (shared `secure_runtime.hpp`, new `link_policy.hpp`, tested by `policy_test`):

- A channel lock holds only while the peer acknowledges the OrcDial's unicast frames; a fresh lock gets 2.5 s.
- The OrcDial remembers the channel of its last working connection and listens there first.
- A timed-out connect is retried up to 6 times (3, 5, 10, 20, 30, 30 s). Disconnect, Forget, Cancel and
  pairing clear the retry intent.

After the change, 16 of 16 cycles with a working Hosted link connected in 13.4 to 14.6 s (previously 17 to
33 s), with all receives on channel 11.

### 3. Hosted init could fail at boot and never retry (fixed)

In the same run, cycle 17 onwards logged `RTL_WIFI_BLOCKED hosted_init_or_version` and Wi-Fi stayed down
through further P4 resets. The C6 never restarted (uptime about 7350 s) and logged
`pserial queue full`. A real C6 reset (EN pulse) followed by a P4 reset recovered everything in 14 s.
This was the adapter confound from finding 1, but the Tab5 also had no recovery for a failed Hosted
init. It is now treated like a lost link and goes through the existing bounded recovery (3 attempts),
and the failing stage and code are logged.

### 4. Boot is deterministic when the C6 really restarts

| Configuration | Cycles | Wi-Fi | OrcDial | Result |
|---|---|---|---|---|
| Adapter attached, C6 reset by EN pulse each boot | 20 | 19.3 s | 22.8 s | 20/20, no faults |
| Adapter unplugged, P4 reset (boot-time rail pulse) | 20 | 19.0 s | 23.6 s | 20/20, all receives on channel 11 |

The unplugged result is the real product configuration. The two early SDIO `0x107` timeouts per cycle
happen while the C6 is powering up after the boot-time rail pulse and are not faults; the harness counts
them separately.

## Not yet covered

- Recovery after a deliberate mid-run C6 power cut takes 25 to 35 s or more and restarts capture several
  times. It is a fault-injection case, not the boot path, and has not been optimised.
- A true cold start (battery and USB removed) and the Tab5-first / OrcDial-first start order still need a
  run with the downloader unplugged.
- One router, one channel, one OrcDial. Other channels and crowded RF environments are untested.
- The C6 firmware is unchanged (Hosted 3.0.6).
