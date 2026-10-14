# Waveshare 4B bring-up probe (developer-only)

Target: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B, NOT the different
86 Panel ETH-2RO. This native ESP-IDF application links the same pinned
esp_rtl_sdr v0.9.3 component as Tab5, starts the published Waveshare 3.0.1
BSP and renders a 720x720 LVGL screen with a live touch position label.

It does not start USB Host or stream IQ; no audio, Wi-Fi, SD or C6 flash.
Successful compilation does not establish hardware operation.

## Build (ESP-IDF 5.5.4)

Export your ESP-IDF 5.5.4 environment, then:

    cd apps/orcsdr-waveshare-4b
    idf.py -B build-4b -D SDKCONFIG=build-4b/sdkconfig set-target esp32p4
    idf.py -B build-4b -D SDKCONFIG=build-4b/sdkconfig build

Do not flash until the actual P4 silicon revision, PSRAM clock, flash size,
board identity, power and USB port roles have been independently checked.
Only after explicit local hardware approval:

    idf.py -B build-4b -D SDKCONFIG=build-4b/sdkconfig -p <PORT> flash monitor

Acceptance: BOARD_PROBE log, stable display, accurate repeated touches.
Next stages: USB OTG VBUS validation, radio dongle and 5-minute stream soak,
ES8311 audio, SDMMC, matched C6/ESP-Hosted, UI parity.

References: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B
and https://github.com/hardcoreerik/esp-rtl-sdr
