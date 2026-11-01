# E4000 hardware offset acceptance on COM39

Candidate based on OrcSDR origin/main `0b60a166`, driver exact local commit
`da22090752660b6689b706716771d8cd8d35f665`. The manifest uses a local Git path;
this is a lab candidate, not a portable published dependency or merged release.
All 26 managed driver source files were checked against the pinned commit.
Image: 5,443,360 bytes, SHA-256
`fdf56237dcd31d1f275b115642511a9affb3fdf2b1ab03a3179cc56d2b212c7e`.
Esptool verified written data on COM39. The previous application partition is
retained privately, and its image hash matches the prior known 068efab build.

The published M5GFX 0.2.27 lacks panel/framebuffer access used by current
OrcSDR. With user approval, the working lab patch was reproduced; both changed
files match the prior working build. The reproducible patch is at
`../../patches/m5gfx-0.2.27-dsi-framebuffers.patch`. It uses two display buffers
(one extra 1,843,200-byte RGB565 buffer). This is a separate dependency condition.
Shared ESP-IDF was not modified; its USB HAL assertion remains an open issue.

## Controlled comparison

XTR serial 82115932, tinySA RF → 30 dB attenuator → XTR, tinySA controlled by
PC USB. Antenna and Ham It Up removed. Nominal source -40 dBm at 100.1 MHz;
receiver requested center 100 MHz, 2.4 MS/s. Absolute power is not calibrated.
E4000 gain remains unavailable in this candidate: do not compare its absolute
amplitude against the PC's manual 19 dB gain result.

Six one-second CU8 captures: offset OFF/ON/OFF, source OFF/ON at each state.
All six contain 4,800,000 bytes with matching firmware/local SHA-256. Every
source-on tone exceeds its paired source-off measurement by over 15 dB.
No rail clipping, reported overruns or consumer drops occurred in these captures.

- Source-off center peak reduction: **20.14 dB**.
- Tone amplitude change: **-1.17 dB** versus surrounding OFF captures.
- Tone frequency: **+99,938.96 Hz in all three source-on captures**.
- Restored-OFF tone level differs from initial OFF by -0.35 dB.
- Requested/effective center remains 100 MHz; ON physical LO is 97.96 MHz,
  hardware offset 2.04 MHz, additional offset mixer zero.

Analysis: average 16 non-overlapping 65,536-point Hann spectra for tone;
average 2048-point Hann spectra after per-window mean removal for center peak
within ±5 kHz. Reduction includes the filter/noise-floor change and must not
be described as calibrated sensitivity gain. `rf-results.json` contains exact
measurements; per-capture metadata retains raw hashes and physical LO/center.
Raw IQ remains in the ignored build folder. Firmware PNG screenshots show
the visible before/after center feature, source OFF in both.

## Other checks and remaining gates

Rate changes applied 816 kHz offset at 960 kS/s and 2.72 MHz at 3.2 MS/s.
Restoring 2.4 MS/s returned 2.04 MHz. These are status/streaming checks, not
matched RF sensitivity or IQ-quality qualification at those rates. A command
issued while RF LAB was closed queued without applying; it applied after
opening RF LAB. The active-page result is recorded separately.
OFFSET OFF while streaming returned BUSY and preserved requested ON.
The capture-tool self-check passed; the scoped application review found no
high-signal findings. Driver host checks: profiles 2,155/0; runtime/API trace
9,012/0; policy CTest passed. Host checks do not establish physical recovery.

Hotplug, broadcast FM/RDS/AM on this firmware, other-dongle hardware regression,
gain implementation and the separate USB HAL crash fix remain open. COM17 and
COM8 were not flashed. TinySA output was acknowledged OFF and paused after the
matrix. This evidence validates the center correction in the tested fixture;
it is not release-wide acceptance or superiority over the PC reference.

## Restart/reconnect follow-up

The user reconnected the XTR and accidentally pressed restart. This qualifies
restart/reconnect, not uninterrupted hotplug. After boot the driver identified
E4000 while idle with requested offset cleared, as specified. Starting reception
reapplied the application's default ON request. Settled status reports 100 MHz
IQ center, 97.96 MHz physical LO, 2.04 MHz hardware correction at 2.4 MS/s.
Two streaming samples advanced bytes from 38,371,328 to 48,824,320 with zero
reported overruns/drops. No RF tone was generated during this check. Pure
hotplug remains unclosed. The tinySA remains OFF and paused.

## Uninterrupted hotplug follow-up

The user unplugged and reconnected only XTR USB without restarting COM39.
Passive serial evidence captured disconnect at device uptime 172676 ms,
new-device/open at 177970�177971 ms (same enrolled serial 82115932), queued
resume, and successful start at 178336 ms. Offset was reapplied automatically.
One bulk endpoint recovery was logged at startup; subsequent streaming was
stable in the observed interval. Uptime advanced to 231538 ms without reboot.
Settled counters advanced from 257,261,568 to 267,583,488 bytes, effective rate
about 2.399 MS/s, zero reported overruns/drops. Applied offset 2.04 MHz,
physical LO 97.96 MHz, effective IQ center 100 MHz. This closes one physical
hotplug cycle for this fixture; it is not a long-run or multi-dongle guarantee.
Source remained OFF. Broadcast, legacy hardware and USB HAL crash gates remain.
