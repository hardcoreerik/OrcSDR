# Cardputer Zero / OrcSDR native FM UI, audio and controls

Status: Linux CI **passed**; **device GUI/audio not yet physically accepted**.
Development branch: `codex/cardputer-zero-ui-audio-controls`.
Base: `codex/cardputer-zero-linux-bringup-20261102` at
`d6023d7c012ee5ffed166e46187b29cb13d3e636`.
No modifications to `main`, ESP32 firmware or original Cardputer bring-up branch.

## Implementation and source lineage

This is a genuine native 320 x 170 instrument, not a scaled Tab5 screen, separate Web
dashboard or Python mockup. It lives alongside the established CLI:

- `src/compact/{control_model.hpp,compact_renderer.hpp}`: semantic actions,
  pending/applied state, deterministic RGB565 320x170 frame composition.
- `src/gui_main.cpp`: SDL2 simulator, direct Linux fbdev frame sink, evdev
  keyboard and explicit demo/live modes. Same renderer and layout in both.
- `src/fm_session.cpp`: one `RtlReceiver` owner, dedicated IQ producer,
  8-block bounded CU8 queue, original FM DSP/audio consumer, spectrum worker
  in the consumer, generation checks, stopped/stale waterfall handling.
- `src/rtl_receiver.cpp`: existing `librtlsdr` Linux driver, with narrowly
  added tuned-frequency and gain readback controls.
- `src/linux_audio.cpp`: ALSA S16_LE 48k output to negotiated 1/2 channel
  layout; mono duplication if stereo hardware, exact software mute/0% silence,
  bounded output, short write/underrun recovery and null diagnostic sink.
- `src/settings.cpp`: validated version-1 per-user XDG settings; temp+fsync+
  atomic rename; no credentials or transient errors.
- `core/portable/cardputer_radio/fm_dsp.c/.h` and `scan_dsp.c/.h`: unchanged,
  explicitly pinned imports of the **existing** OrcSDR-Headless FM and FFT
  processing modules at `67e535e3a8562709d190194363f7d81ce77fe2f3`.
  The FM algorithm is not a rewritten separate decoder. It handles 300 kS/s
  center-tuned CU8 input, existing phase discriminator, mono lowpass,
  48 kS/s resampling, 75 us de-emphasis and audio DC removal.
- `apps/orcsdr-tab5/ui/waterfall_style.cpp`: compiled **directly**, original
  OrcSDR FM palette. Existing ACARS/ADS-B decoder sources and tests untouched.

Only WFM mono is enabled. AM, airband, shortwave, stereo, RDS, full
Radio Service Web operations and finished bandwidth controls are **not
implemented** in this UI. The existing loopback-only Web asset preview is
unchanged and is not an operational remote-control server.

## Visual reconnaissance and exact OrcSDR tokens

Sources reviewed: `apps/orcsdr-tab5/ui/fm_dashboard.cpp`,
`am_dashboard.cpp`, `dashboard_audio_control.cpp`,
`home_dashboard.cpp`, `fm_config.cpp`,
`waterfall_style.cpp/.hpp` and original OrcSDR Web implementation
`platform/web/app/app.js` (Headless).

FM dashboard RGB565 theme: black `0x0000`, panel `0x0841`,
cyan `0x2e7f`, green `0x6fe8`, yellow `0xff24`,
muted `0x8c71`, grid `0x2945`. Selected row uses original
`0x1264` fill, green outline/accent. Waterfall calls
`orcsdr::waterfall_style::color565(Screen::fm,...)`: no guessed heatmap.

The compact renderer uses a built-in uppercase 5x7 pixel glyph set at 1x/2x/3x,
with fixed-width 21px high frequency digits. This avoids external font licensing,
but the pixel font is **not** a byte-identical Tab5 M5GFX font. Where space
permits the status and menu labels are 14px high; dense bottom hints/long detail
remain 7px bitmap glyphs and require physical legibility acceptance.

Final native pixel rectangles (inclusive start, exclusive end):

| Region | Rectangle (x,y,w,h) | Content |
| --- | --- | --- |
| Header | 0,0,320,16 | OrcSDR, FM broadcast, applied receiver/demo state |
| Frequency | 0,16,320,38 | fixed-width MHz, WFM, pending tune state |
| Status | 0,54,320,18 | tuning step, volume/mute, auto/manual RF gain |
| Spectrum | 2,72,316,39 | latest windowed source-derived FFT, centered tune marker |
| Waterfall | 2,111,316,41 | up to 36 real rows, original OrcSDR palette |
| Footer | 0,152,320,18 | important keyboard hints |
| Focused overlay | 27,30,266,113 | frequency, menu, gain, audio, receiver, help/error |

RGB565 output buffer is 320 * 170 * 2 = **108800 bytes**.
Waterfall history is 36 * 256 = **9216 bytes**.
Bounded live IQ queue: 8 * 8192 complex CU8 = **131072 bytes** excluding
metadata/allocator overhead. These are allocation budgets, not measured RSS.
The Linux process RSS, device GUI cadence, live underruns, heat and
end-to-end USB/audio latency remain to be measured on target.

The spectrum uses the original 1024-bin Hann-windowed PSD, rebinned to 256
display columns over the **300 kHz RF input span**, updated at most once
every 90ms. Frequency marker is centered. Values are **relative
uncalibrated FFT-bin levels**, not calibrated dBm or an S-meter. The
waterfall rows use the same 256-bin horizontal mapping, are not fabricated
in live mode, and are invalidated on stream generation changes.

## Keyboard controls

These keys are supported by both the desktop SDL and Linux evdev code.
Hardware mapping is provisional until inspected on the actual 46-key Zero:

| Keys | Action |
| --- | --- |
| Z / C, or Left / Right where available | tune by current step |
| F, Enter from receive | frequency dialog, MHz entry |
| digits, period, Backspace, Enter | edit/accept frequency |
| Esc | cancel dialog or go back without applying partial input |
| T | cycle 10/50/100/200/1000 kHz steps |
| S / D (or - / =) | volume down/up |
| M | immediate mute/unmute |
| Tab, Up/Down, Enter | open/navigate/choose menu |
| G, Enter, Left/Right | gain auto/manual and supported receiver steps |
| H | keymap/help |
| R | reconnect receiver/audio after error |
| Q, SDL window close, SIGINT/SIGTERM | orderly release and exit |

The driver enumerates actual supported manual gains. If a capability/driver
change fails, a brief error is reported; the GUI never manufactures a
successful RF readback. Key repeats coalesce on a requested tune frequency,
rather than opening multiple device handles. Real Zero keyboard event codes,
FN layers and launcher ownership require local confirmation.

## Linux toolchain and vendor references

Vendor sources inspected, not silently imported:
- https://docs.m5stack.com/en/CardputerZero
- https://docs.m5stack.com/en/compute/cardputerzero/usage
- https://github.com/CardputerZero/Template pinned at
  `477fa6d217b683e3084c361a757f03dc8ca9f753`.
- https://github.com/lvgl/lvgl pinned at v9.5.0,
  `85aa60d18b3d5e5588d7b247abf90198f07c8a63`.
- https://github.com/CardputerZero/AppBuilder pinned at
  `18996b694ae305306fc5d457d982cfba0b0e7cc5`.
- https://github.com/CardputerZero/M5CardputerZero-UserDemo
- https://github.com/m5stack/M5Stack_Linux_Libs

`tools/bootstrap_gui_audio_dev.sh` installs system SDL2, ALSA,
libevdev, libdrm, FreeType, FFTW, `librtlsdr`, CMake, Ninja and build tools;
`--cross` additionally installs the AArch64 compiler.
`tools/bootstrap_vendor_sdk.sh` retrieves exact pinned upstream Template,
LVGL and AppBuilder repositories to an ignored/outside work directory.
**The runtime deliberately uses a small native RGB565 fbdev implementation
instead of linking the large vendor LVGL GUI**; the SDK is available for
side-by-side verification and future migration. This choice must be
revisited if direct fbdev ownership conflicts with APPLaunch.

The chat sandbox lacks outbound apt/GitHub DNS, so the developers installed
and tested the dependencies on network-enabled GitHub CI. Do not report
the local chat container as having obtained SDL/ALSA development headers.

## Build and run (physical Debian 13 Cardputer Zero)

The user previously verified on HardputerZero (user-run, **before this
feature branch**) Debian 13 AArch64, 352 MiB visible, approximately
134 MiB available, 351 MiB swap. Their original Cardputer branch passed
all 10 CTest cases, discovered Realtek RTL2838/R820T with 29 gain steps,
and captured 32768 complex samples at 100.1MHz/1.024MS/s. The live
ADS-B transport test did not decode aircraft. **None of this is evidence
that this new GUI, 300k WFM DSP, speaker or LCD worked physically.**

From the **actual** OrcSDR working tree:

```bash
cd /home/hardputer/OrcSDR-cardputer-zero
git status --short
# STOP and preserve/stash/commit local changes before branch switching.
git fetch origin codex/cardputer-zero-ui-audio-controls
git switch --track origin/codex/cardputer-zero-ui-audio-controls
# If the local branch already exists, use git switch codex/cardputer-zero-ui-audio-controls.
bash apps/orcsdr-cardputer-zero/tools/bootstrap_gui_audio_dev.sh 2>&1 | tee zero-dependencies.log
cmake -S apps/orcsdr-cardputer-zero -B build/cardputer-zero \
  -DCMAKE_BUILD_TYPE=Release -DORC_ENABLE_GUI=ON \
  -DORC_ENABLE_ALSA=ON -DORC_ENABLE_SDL=ON -DORC_ENABLE_RTLSDR=ON \
  2>&1 | tee zero-configure.log
cmake --build build/cardputer-zero --parallel 1 2>&1 | tee zero-build.log
ctest --test-dir build/cardputer-zero --output-on-failure --verbose \
  2>&1 | tee zero-ctest.log
./build/cardputer-zero/orcsdr_cardputer_zero_gui --demo --screenshot zero-demo.ppm \
  2>&1 | tee zero-render.log
```

Inspect vendor audio/display/input devices **without changing codec registers**:
```bash
uname -a
cat /proc/asound/cards
aplay -l
ls -l /dev/fb* /dev/input/event* 2>&1
./build/cardputer-zero/orcsdr_cardputer_zero --list
```

Run live local UI:
```bash
./build/cardputer-zero/orcsdr_cardputer_zero_gui \
  --display fb --device 0 --freq 96100000 --audio default \
  2>&1 | tee zero-live-fm.log
```

For a known ALSA PCM device, pass `--audio plughw:CARD,DEV`
with actual identifiers observed on that OS. Never assume card index or
speaker/headphone switching. Do not require root by default; if access
fails, investigate vendor groups/udev policy for USB SDR, framebuffer,
input and ALSA. Do not use blanket chmod 777.

Desktop simulator (the same renderer):
```bash
./build/cardputer-zero/orcsdr_cardputer_zero_gui --demo --display sdl --frames 120
./build/cardputer-zero/orcsdr_cardputer_zero_gui --demo --view gain \
  --screenshot gain.ppm
```
The screenshot flag writes raw 320x170 PPM from the actual compositor.
Use ImageMagick or Pillow for 3x/4x nearest-neighbor inspection if wanted.
CI produces 11 PNG captures plus a contact sheet, with no radio attached.

Optional `cmake --install build/cardputer-zero` installs a binary,
APPLaunch-compatible desktop entry and existing OrcSDR badge, but **do
not install/enable an autostart service or reflash the OS**. On
uninstall, remove only the installed package/file paths; user settings
remain under `$XDG_CONFIG_HOME/orcsdr/cardputer-zero.conf` (or
`~/.config/orcsdr/cardputer-zero.conf`) until deliberately removed.

## Tests and evidence

Successful cloud CI run:
https://github.com/hardcoreerik/OrcSDR/actions/runs/44028855612

All **15 CTest** checks passed, including the existing Linux/ACARS/ADS-B
regressions; new checks include control state and frequency cancellation,
actual compositor output, source-pinned FM DSP at 300k -> 48k,
1kHz tone recovered at 1000 Hz with RMS ~3571 PCM, identical results for
4096- and 16384-byte IQ chunks; versioned settings/corruption;
and null audio exact mute and bounded software volume.
SDL dummy video mode successfully rendered 11 states to native
320x170 images with CI artifact logs. Debian ARM64/x86-64 Linux,
`librtlsdr` ON/OFF builds preserved in the same workflow.

Known gaps: no field-verified GUI/evdev keymap/fb format; no
audible speaker/headphones evidence; no ALSA hardware underrun stress
evidence; no real on-air FM audio acceptance; no USB hot-unplug stop
latency evidence; no measured target RSS; no complete Web-service
remote control; no AM, stereo, RDS, digital voice, TX or Pluto.
RTL-SDR synchronous USB reads can still block on pathological driver
failure; only controlled shutdown in functional tests is established.

## Required local acceptance

1. Prove Q, Escape and SIGINT exit and return shell/launcher control;
   verify SDR kernel driver reattaches where appropriate.
2. Inspect real evdev keycodes. Validate tune, step, numeric entry,
   cancellation, gain list/auto/manual, volume, mute and errors.
3. Listen at conservatively low volume on speaker **and** headphones;
   test output-device selection and null/silent error behavior.
4. Test antenna/FM station reception against known independent receiver,
   not just synthetic IQ; record real applied frequency and rate.
5. Tune rapidly, disconnect/reconnect dongle and audio, confirm no
   stale waterfall/audio silently persists.
6. Measure `/usr/bin/time -v`, `/proc/<pid>/status`, sustained
   queue drops, CPU, thermal and shutdown times on device.
7. Photograph or capture LCD outputs, inspect overlays, 14px text,
   tone/readability and framebuffer pixel format. Validate APPLaunch
   integration before packaging as a consumer-ready application.
