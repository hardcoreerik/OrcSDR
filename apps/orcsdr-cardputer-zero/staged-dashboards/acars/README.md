# OrcSDR Cardputer Zero — ACARS (staged)

Selector ID: `acars` · Title: `ACARS` · Native Linux 320 × 170 dashboard.

This module accepts **real VHF ACARS** through the **existing OrcSDR**
`orcsdr::acars::Frontend240k` and `orcsdr::acars::Decoder` in
`apps/orcsdr-tab5/ui/acars_core.*`. A CRC-verified decode is delivered as a
bounded typed message, not captured from CLI output or a network service.
The standalone preview is explicitly **DEMO**, not an RF acceptance test.

## Receive format and antenna

- VHF airband frequency entered on a 25 kHz grid, 118.000–137.000 MHz;
  default **131.550 MHz** (an illustrative commonly used ACARS channel, not
  a nationwide scanner or promise of activity).
- RTL-SDR physical tuning center **131.490 MHz** for 131.550 MHz channel:
  desired carrier is **+60 kHz** in a **240,000 complex samples/s CU8** stream.
- Shared `Frontend240k` mixes by a four-step quarter-rate rotation, runs a
  129-tap low-pass anti-alias FIR, and decimates by 10 to **24,000 complex
  samples/s** for the canonical ACARS message decoder. Radio ownership and
  application-level queueing belong to the host. No additional receiver
  instance is opened by this module.
- VHF airband antenna and real traffic are required for over-the-air acceptance;
  an empty channel does not prove failure. The 2.56 MS/s FM overview is a
  **different radio mode/rate**, not an ACARS input format.
- No automatic retune/scanner, multiblock reassembly, network feeds or audio.
  ETB and ETX **validated blocks are shown separately**.

## Standalone build, tests and screenshot previews

Run from the actual OrcSDR repository root (C++17 + CMake; no SDR, Linux
ALSA, GUI toolkit or physical display needed for these tests):

```sh
cmake -S apps/orcsdr-cardputer-zero/staged-dashboards/acars -B build/staged-acars -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/staged-acars --parallel 2
ctest --test-dir build/staged-acars --output-on-failure
build/staged-acars/orc_zero_acars_preview --demo --screenshot build/staged-acars/acars-demo.ppm
build/staged-acars/orc_zero_acars_preview --demo --empty --screenshot build/staged-acars/acars-empty.ppm
build/staged-acars/orc_zero_acars_preview --demo --detail --screenshot build/staged-acars/acars-detail.ppm
git diff --check -- apps/orcsdr-cardputer-zero/staged-dashboards/acars/
```

For Windows or multi-config generators use the corresponding executable path
(`.exe`, and perhaps `Debug/`), but keep the same arguments. The generated
P6 PPM is **320×170** and has no physical LCD dependency.

## 320×170 UI

- Header: `OrcSDR`, `ACARS`, honest `LIVE` / `DEMO` / `CONNECT` / `NO RX` / `ERROR` state.
- Small station frequency, **actual** channel rate, gain state, separated
  validated/corrected/CRC-rejected/framing/overflow counters, and observed
  application queue loss and stream reset counters. In DEMO, simulated blocks
  are not shown as real decoder/receiver statistics.
- Five visible rows of a bounded 16-block ring, newest-first with scrolling
  selection: unread `N`, registration/tail, flight, label, age, ETX/ETB and
  first characters of sanitized text. The **full original bounded text**
  remains in the record and in detail view.
- Selected detail view: frequency, tail, flight, label, CRC/correction count,
  raw length, text length, ETB/ETX and bounded escaped payload (`<1B>` etc).
  Control bytes never execute, and payload is never used as a format string.
- Up/down: select recent blocks or scroll detail; Enter: detail; Escape: back.
  `F` frequency entry, `Space` selector, `P` shared palette, `Tab` menu,
  `G` gain, `T` step, `S/D` volume, `M` mute, `Q` exit remain with host. There
  is no decoder audio to control in this first module.

## Interface and resource bounds

See [INTEGRATION.md](INTEGRATION.md). All entry points are C++17 and the
module owns no device handles. Live ingestion is intentionally restricted to
CRC-verified callbacks from the shared decoder; `append_demo` is explicit
and only works while the dashboard is in DEMO mode.

- History: **16 records**, oldest evicted on overflow. Selection by serial
  remains on a surviving record where possible, or clamps to the oldest.
- Decoder-to-UI SPSC queue: **16 events**, drops newest at capacity and exposes
  `dropped_display_events`. No malloc in IQ callback and no per-sample logging.
- Input: one CU8 block of 2–262144 even bytes at exactly 240 kS/s with checked
  tuner center and generation; invalid input hard-fails, never masquerades as
  a successful empty receive. Host supplies sample indexes/monotonic times.
- Text: canonical `Message::text` array is 250 bytes with authoritative
  `text_length`; sanitized detail is at most 4×250 escaped characters, computed
  outside the IQ path. Main list stores the full original message.
- Screen: one RGB565 320×170 backing buffer (**108,800 bytes**) and one
  bounded previous snapshot. Only initial layout entry calls the full-buffer
  clear; dirty regions are returned for the host LCD adapter, which must flush
  regions rather than copying the whole frame for every revision.

No runtime runtime reconnect loop and no device flashing. Integration on the
real Cardputer Zero is **not complete** until the coordinator wires selector,
receiver handoff, keyboard, and partial LCD flush and tests those on-device.
