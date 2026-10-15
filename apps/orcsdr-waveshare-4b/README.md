# OrcSDR Waveshare ESP32-P4-WIFI6-Touch-LCD-4B

**Home v3 source integrated on the Waveshare development branch. Not yet
visually accepted on physical hardware. Do not treat the prior IQ-only Home
v1 photograph as approval of this UI.**

The v3 Home keeps the actual OrcSDR Tab5 RGB565 Orc badge, cyan/green/black
theme, real Home waterfall palette, and badge-toggled LAST USED navigation.
The drawer opens over the full-width spectrum/waterfall; no plots are resized
while it is open. Tune +/-, STEP and PALETTE are available. Other dashboard
routes and unported controls are visibly disabled. No fake RF, Wi-Fi, audio,
or decoder functionality is presented as live.

Local preflight corrected STEP label overlap and preserves six fractional
MHz digits for exact 12.5 kHz tuning readouts. `HOME_4B STEP` and
`HOME_4B PALETTE` logs support the physical touch checks. Host geometry,
tuning/FFT, drawer state and log-validator tests pass; these do not establish
physical visual acceptance. See the v3 design and screen-gate evidence ledgers.

Local ESP-IDF 5.5.4 rev1.x build also passed (857,376-byte app, 18% free).
Original badge and shared palette symbols are present in the linked ELF.
This image has not yet been physically qualified; retain the separate build,
flash, photo, drawer and complete RF-log acceptance gates.

Owner-approved Home v3 flash/boot now passed on COM40/P4 v1.3. The complete
boot-to-IQ log passed the unmodified strict checker: 310.11 seconds, minimum
post-warmup 959,604 SPS, zero added drops, profile `blog_v4l_r828s`. Photos,
owner visual acceptance, 50 drawer interactions and button/palette testing
remain pending; no touch events occurred during that stationary RF capture.

Home v3 lives in main/home_v3_ui.cpp. The USB/IQ task and standalone
esp-rtl-sdr v0.9.3 dependency are unchanged. The new renderer reuses the
original apps/orcsdr-tab5/ui/waterfall_style.cpp directly in the
board-specific native build and embeds the original Orc badge unchanged.

### Local 4B hardware evidence

The owner's exact physical 4B has ESP32-P4 silicon v1.3, 32MB flash, 32MB
PSRAM at 200MHz. It has successfully run live RTL-SDR Blog V4 IQ through
an independently powered passthrough adapter: 309.18 seconds of serial
metrics measured about 960,052 samples/second with 0 reported IQ drops.
The reconnect-only serial capture did not include the RF START marker,
so strict complete-boot capture qualification is still open.

### Build for owner's verified silicon v1.3

Use ESP-IDF 5.5.4. From this app directory in an exported IDF shell:

    idf.py -B build-4b-home-v3 -D IDF_TARGET=esp32p4 \
      -D SDKCONFIG=build-4b-home-v3/sdkconfig \
      -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev1_x" build

For PowerShell run the same command on one line or with PowerShell
backtick continuation. Do NOT flash CI's default rev3.x artifact to v1.3
silicon. Verify build-4b-home-v3/sdkconfig's revision/PSRAM settings and
the generated flasher_args.json. Before flashing, compare actual board,
COM port and revision with the recorded preflight; obtain final owner
approval. Do not erase chip, overwrite C6 or affect Tab5.

### Device acceptance required

1. Photograph booted Home. Compare real badge colors, styling, font size,
   header Wi-Fi/SDR/uptime groups, full-width spectrum and waterfall against
   the original OrcSDR Wiki screenshot. Test no-SDR state separately.
2. Touch badge repeatedly: opens drawer; badge/outside tap closes. No
   underlying tuning action should fire from a drawer-dismissal tap.
3. Test every enabled button (TUNE +/-, STEP and PALETTE) and confirm
   serial log plus screen status agree. Disabled actions must not claim
   navigation to unimplemented dashboards.
4. With the proven properly powered RTL-SDR V4/USB-C passthrough adapter,
   verify Home's actual live plot and sustained RF metrics. Capture the
   complete RF START followed by 5+ minutes of IQ telemetry.
5. Run the strict test from this app directory, saving the monitor log here:

    python ../../tools/hardware/check_4b_home_log.py --log home-v3-com40.log

   Use local COM40 only after re-confirming the port; COM39 is a different
   board in previous logs. Keep flash and monitor logs beside commands
   run, with source SHA, hashes, revision, chip ID, and any exceptions.

No FM or other dashboard is accepted until Home's physical acceptance gates
pass. Additional architecture/evidence: ../../docs/hardware/
WAVESHARE_4B_HOME_V3_DESIGN.md,
WAVESHARE_4B_SCREEN_GATES.md,
WAVESHARE_4B_USB_HOST_POWER.md.

---

## Historical Home v1 probe notes (retained for context)

# OrcSDR Waveshare ESP32-P4-WIFI6-Touch-LCD-4B — Home v1

First real OrcSDR Home screen on the 720x720 Waveshare. This is an IQ-only
development build, not a complete Tab5 port. It shows real relative FFT
spectrum and rolling waterfall, SDR status, dropped IQ counters, touch tune
plus/minus, selectable tuning step and four preset frequency buttons.
There is no demodulated audio, no settings, no other screens, no C6 writes.

## Build and test (ESP-IDF 5.5.4)

Run the host geometry, frequency and known-tone FFT test:

    g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
      apps/orcsdr-waveshare-4b/tests/home_host.cpp -o /tmp/home4b
    /tmp/home4b

From an exported ESP-IDF 5.5.4 shell:

    cd apps/orcsdr-waveshare-4b
    idf.py -B build-4b -D SDKCONFIG=build-4b/sdkconfig set-target esp32p4
    idf.py -B build-4b -D SDKCONFIG=build-4b/sdkconfig build

Do NOT flash until checking the physical ESP32-P4 silicon revision and
32 MB flash/PSRAM config: factory Waveshare image defaults to rev3.x with
250 MHz PSRAM and must not be flashed on rev1.x silicon.

For a verified rev1.x chip, use the separate 200 MHz PSRAM profile in a
fresh build directory. Add the following argument to **both** commands above:

    -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev1_x"

This profile selects silicon revisions 1.00–1.99. It does not establish PCB
identity or prove the installed PSRAM works. Never reuse a rev3.x build for it.

After physical board and revision verification, flash only if explicitly
approved by the local device owner:

    idf.py -B build-4b -D SDKCONFIG=build-4b/sdkconfig -p <PORT> flash monitor

IMPORTANT: On the published 4B schematic the USB-C OTG socket has fixed
5.1k Type-C SINK CC terminations. It may NOT source 5 V to a USB dongle
or complete standard USB-C source attachment. A generic powered hub is
NOT guaranteed to solve the CC and upstream VBUS role mismatch. Before
attaching an SDR, use a verified, independently powered host-data/SDR
fixture with correct Type-C roles, VBUS isolation and USB HS support;
never backfeed 5 V into USB-C, bridge input rails or attempt unreviewed
PCB modifications. See docs/hardware/WAVESHARE_4B_USB_HOST_POWER.md.
NEVER start bsp_usb_host_start: esp-rtl-sdr must own USB Host.
Connect a supported SDR dongle only after power/role tests.
Expected serial markers:
HOME_4B, HOME_4B TUNE, HOME_4B RF START, HOME_4B IQ.
External USB hub support is enabled in the build defaults. Existing build
directories with a saved sdkconfig must enable `CONFIG_USB_HOST_HUBS_SUPPORTED`
explicitly or be replaced with a fresh build directory to pick up this default.
With no SDR, the display must say NO SDR CONNECTED and show no false RF.
The GUI reports IQ-only and does NOT pretend to be an audio receiver.

Test live reception for 5 minutes at 960 kS/s, >=95% effective SPS, known
spectral peak movement on tune, bounded USB overruns and correct touch
alignment. Hardware, RF and display qualification are still pending.
See docs/hardware/WAVESHARE_4B_SCREEN_GATES.md.
