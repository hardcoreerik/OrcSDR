# OrcSDR SSTV — native Tab5 receiver (experimental)

**Branch:** `codex/sstv-native-dashboard`  
**Hardware:** M5Stack Tab5 (ESP32-P4), RTL-SDR Blog V4 or compatible receiver  
**UI:** Native M5GFX, 1280×720, receive-only, no web runtime required.

## Implemented dashboard views

| View | What it shows |
| --- | --- |
| Receive | Progressive image preview, VIS/mode/line progress, tone and peak, abort and save |
| Images | Four completed in-memory frames, selector, per-image mode/frequency, BMP save, clear |
| Signal | Tone estimate, PCM sample/overrun counters, VIS diagnostics |
| Frequencies | 14.230 MHz USB, 145.800 MHz FM (event only), 145.290 MHz FM Oregon net, 437.550 MHz FM ARISS (event only) |
| Setup | Mode selector (Auto / Robot 36 / Martin M1 / Scottie S1), SD auto-save control, receiver status |

Modes attempted in this first experimental core: Robot 36 (VIS 8), Martin M1 (VIS 44), Scottie S1 (VIS 60). **PD120 / PD180 are not yet decoded.** These are essential for many ISS SSTV events and are next on the decoder roadmap. This is not a production-ready or validated SSTV receiver yet.

## Architecture

- The existing `esp_rtl_sdr` driver supplies IQ. No new USB backend.
- Normal OrcSDR demodulation produces **48 kHz PCM**. The SSTV tap receives these samples before speaker-mute policy, decimates 4:1 into a bounded SPSC queue, and never blocks the DSP task.
- SSTV owns only its display through `screen_controller`; the main/UI task drains, decodes, and paints, not the USB callback.
- A 240 kS/s RTL acquisition rate is requested **only if supported by the connected driver**. If unsupported, the standard band rate is used.
- HF 14.230 MHz selects USB; VHF/UHF selects NFM. On a Blog V4, HF tuning relies on its native HF conversion and the compatible V4 driver, not direct sampling.
- Completed images are kept in RAM (four entries); when SD is mounted, auto-save or the Save button writes 24-bit BMP to `/sd/orcsdr/sstv/`. SD must be mounted by OrcSDR.
- No images, image metadata, or signal-lock claims are simulated in live mode.

## How to build and test

1. Check out this branch, preferably in a clean worktree. Do not build from main when testing SSTV changes.
2. Follow `ENVIRONMENT.md` and `apps/orcsdr-tab5/README.md` for the supported **ESP-IDF 5.5.4** Tab5 build. Do not use the historical PlatformIO configurations.
3. On a Linux host, run the stand-alone algorithm smoke test:

   ```sh
   g++ -std=c++17 -O2 -Wall -Wextra -Iapps/orcsdr-tab5/ui \
       apps/orcsdr-tab5/tests/sstv_receiver_smoke.cpp \
       apps/orcsdr-tab5/ui/sstv_receiver.cpp -o /tmp/sstv-smoke
   /tmp/sstv-smoke
   ```

   The GitHub workflow `sstv-receiver-tests.yml` performs the same synthetic Robot36 test. This validates a limited ideal-signal path, not RF performance.

4. Use the project's normal `tools/build-tab5-idf.ps1` on the supported Windows/ESP-IDF workstation to build and flash the M5Stack Tab5. Capture the build output and device console serial log.
5. Plug in the RTL-SDR V4. Select **SSTV** from the Home dashboard catalogue. Check that the receiver shows the actual connection state. Open Frequencies, choose a preset, and return to Receive.
6. Use a known legal *received* SSTV transmission or a local audio/RF test generator into a shielded receiver path. Do not transmit on amateur bands without appropriate license/authorization. Feed standard Robot36/Martin/Scottie reference captures, compare pixel output against QSSTV, Robot36, or the SDR++ SSTV module.
7. During acquisition, monitor `VIS OK`, `VIS ERR`, ring overruns, row progress, and whether a completed image enters Images. Check automatic and manual saving with an SD card. On the card verify `/orcsdr/sstv/*.bmp` exists and opens in an image viewer.
8. Repeat for no receiver, SD missing, noise-only input, interrupted transmission, frequency retune during a frame, manual abort, and Home navigation. Check that unrecognized VIS codes do not generate fake images or an unwarranted success banner.
9. Verify no regressions to FT8, FM, shortwave, other dashboards, audio recording, or UI frame ownership. Run the repo's other host suites and normal release build as appropriate.

## Known gaps and engineering risks

- **No actual Tab5 firmware build or RF playback validation has been completed in this branch.**
- Frequency detection currently uses zero-crossing period estimation, not a robust band-limited quadrature FM tone estimator. Weak/noisy reception will need improvement.
- Line reconstruction uses ideal fixed timings; it still needs sync-edge tracking, slant correction, frequency-offset tracking, and signal-loss recovery before release.
- Satellite PD modes, including **PD120**, are unsupported.
- SD writes run in the UI task; a queued storage worker and atomic file-write/rename are needed for unattended operation.
- Only four completed images are kept in RAM, and the current frequency list is fixed in firmware.
- The 145.290 MHz Oregon net and ISS frequencies are listening presets, **not proof of any current transmission**; verify schedules and current ARISS downlink configurations separately.
- The visual signal screen currently plots a current tone estimate, not a historical spectrum or calibrated SNR.

## Research references

- [RTL-SDR Blog V4 user's guide](https://www.rtl-sdr.com/v4/) — V4 driver and HF reception requirements.
- [SDR++ SSTV decoder](https://github.com/F4JTV/sstv_decoder) — auto VIS, slant correction, multiple modes, auto-save and weak-signal operation.
- [Robot36](https://github.com/xdsopl/robot36) — lightweight audio SSTV decoding and practical gallery workflow.
- [PD120/Robot36 timings](https://sstv.org.uk/docs/modes/detail/pd-pd120/) — timings and VIS codes.
- [ARISS contact and current SSTV information](https://www.ariss.org/contact-the-iss.html) — verify active downlink/event before tuning.

Design rule: **Never claim a decoded image when there was no validated VIS and pixel reception.** Any synthetic tests must remain labeled as test data.
