# Cardputer Zero compact Home implementation plan

Goal: give the live spectrum/waterfall 120 of 170 vertical pixels, use 2.56 MHz RF capture, and persist P palette selection.

Preserve screenshot work and the running binary until validation. No main/remote writes or dependency additions.

1. Add focused layout/palette/settings regressions and extend the FM tone test to 2.56 MS/s; confirm failures first.
2. Reuse shared waterfall palettes. Route P through SDL/evdev and settings validation/load/save. Grow real historical rows to 72.
3. Compact chrome; derive RF scale/passband positions from actual rate. Avoid full-screen clears on ordinary updates; write only changed framebuffer runs.
4. Select 2.56 MS/s, tune hardware 640 kHz below selected station to match existing Fs/4 DSP. Keep requested/applied frequency as station frequency and report actual readback rate.
5. Run focused tests with assertions enabled, inspect preview, back up prior binary, build only GUI/related tests, and run bounded device telemetry before a live LCD/audio check.

Space selector scope is pending user clarification; no new live decoding dashboards are implied.
