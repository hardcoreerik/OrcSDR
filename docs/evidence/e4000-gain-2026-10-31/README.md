# E4000 gain and model-label candidate on COM39

Driver source: exact local commit `3d75e5730f12613288603d8b79b3e1284758ae9f`.
All 26 managed source files were compared against that revision before flash.
App source: ae425628 plus its manifest/lock pin and a test-runner line-sync fix.
Image: 5,444,560 bytes; SHA-256
`22a2dc3219bb2e0cdce093ab16b455e9359066c9043bec1fc4fe35c849c54dc4`.
Esptool verified written data on COM39; this flash intentionally rebooted the
tablet with user approval. The isolated HAL fix and existing M5GFX patch remain.
COM17 and COM8 were unchanged. Previous HAL-candidate image is retained locally.

## Results and boundaries

- The Python serial runner passed all 14 manual settings (-1 to 42 dB) and
  tuner AUTO, with continuing IQ and no additional reported drops/overruns.
  Startup counters were already 1/1; do not describe the complete boot/run as
  having zero losses. This verifies serial controls and software state, not
  calibrated analog gain.
- Uptime advanced from 141,036 to 147,807 ms during the test. Later screenshot
  health advanced from 380,200 to 381,809 ms. These observations show no reset
  during those Python sessions. One earlier pairing attempt failed during
  startup; a blank console line and public-health synchronization resolved it.
- `RTL_GAIN STATUS` returns the exact 14 signed steps and receiver name
  `Nooelec XTR`. The verified firmware PNG shows the model in Home's header
  and footer and the active -1.0 dB control. Physical flicker acceptance was
  not independently reported.
- The attached antenna was reconnected by the user, but its model was not
  specified for this run. Serial reported stereo and RDS carrier lock. These
  flags do not independently prove correct RDS text; current-image decoded
  text/audio acceptance remains bounded by user observation.
- Antenna samples at nominal -1, 19 and 29 dB reported near-full-scale signal
  and 100% clipping. Lowering the requested gain did not resolve this. The
  live antenna is not a controlled power source: overload, IF initialization
  and clipping telemetry need checking against the tinySA fixture against the controlled result below. The cause of this antenna-only
  telemetry remains unresolved; do not infer a driver gain failure from it.

## Controlled RF follow-up, 2026-11-01

User connected tinySA RF -> 30 dB attenuator -> XTR, USB on COM39.
The completed 100.1 MHz CW run at nominal -40 dBm used a 100 MHz receiver
and 2.4 MS/s. All 14 manual gain settings and tuner AUTO met the script's
tone and rail-clipping checks. All 16 captures (76,800,000 CU8 bytes total)
were independently rehashed after retrieval and matched the device hashes.
All raw rail fractions were zero. The weakest manual tone was 43.86 dB above
the source-off search peak. AUTO retained the tone without clipping; this
single input level does not qualify AGC attack/release or overload behavior.

The manual endpoints measured -44.25 and -8.72 dBFS: a 35.53 dB change for
43 dB of nominal requested range. The 14 -> 16.5 dB step dipped 0.57 dB.
The accepted PC reference also had nonideal steps, including a 29/34 dB
plateau, but its curve differs from this ESP run. Gain adjustment is working;
absolute gain accuracy and vendor equivalence are not established. Source
and attenuation are nominal, and initialization/offset/FFT conditions differ.
Do not silently change gain codes or IF settings to fit this one curve.

Exact scripts, commands, hashes, comparison and machine-readable summaries
are in [controlled-rf](controlled-rf/README.md). Raw IQ stays local in the
ignored build-offset/gain-candidate/controlled-gain-matrix directory.
The run ended with output OFF and pause acknowledged. Both commands were
sent again and acknowledged during the morning follow-up. No new flash was
performed; COM17 and COM8 were not accessed in this follow-up.

The driver remains staged in draft [PR #64](https://github.com/hardcoreerik/esp-rtl-sdr/pull/64).
Its six source/test files match the tested local driver revision byte-for-byte.
Current checks pass: Windows/Linux host tests, ESP-IDF 5.5.4 and 6.1 compile,
truth/version hygiene and the CodeRabbit status. Review comments have not
been triaged; a green status is not a claim of review completion or readiness.
The PR is not merged. Other-dongle hardware regression gates remain open.

## Bias tee and RTL digital AGC

The USB identity is **SMArt XTR**, not SMArTee XTR. Nooelec lists no bias tee
for SMArt XTR; the SMArTee XTR has an always-on tee. Do not expose a software
bias toggle without an independently verified controllable circuit:
https://support.nooelec.com/hc/en-us/articles/360005807694-NESDR-SMArt-Series

RTL2832U digital AGC is separate from tuner AUTO. First-party PC controls
show demod page 0 register 0x19 values 0x05/0x25/0x05 for OFF/ON/OFF. The
current E4000 profile does not yet advertise RTL AGC. These writes must not
be confused with offset-NCO register 0x19 on demod page 1. Enabling and
physically qualifying that additional control remains separate work.
