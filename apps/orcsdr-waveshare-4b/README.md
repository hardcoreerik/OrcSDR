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

Use the separate USB-C OTG path with a powered hub until board-supplied
5V VBUS is confirmed. NEVER start bsp_usb_host_start: esp-rtl-sdr must
own USB Host. Connect a supported SDR dongle. Expected serial markers:
HOME_4B, HOME_4B TUNE, HOME_4B RF START, HOME_4B IQ.
With no SDR, the display must say NO SDR CONNECTED and show no false RF.
The GUI reports IQ-only and does NOT pretend to be an audio receiver.

Test live reception for 5 minutes at 960 kS/s, >=95% effective SPS, known
spectral peak movement on tune, bounded USB overruns and correct touch
alignment. Hardware, RF and display qualification are still pending.
See docs/hardware/WAVESHARE_4B_SCREEN_GATES.md.
