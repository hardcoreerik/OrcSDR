# Developer reference

The help pipeline is manifest-driven:

```powershell
.\tools\build-help-media.ps1 -Port COM17 -Release <tag> -All
```

The first full run creates a Kokoro `am_michael` pronunciation sample and stops for approval. Rerun with `-ApproveVoice` after listening. Firmware capture commands require the existing HMAC-authenticated serial session. Captures are exact 1280×720 BMP frames, retrieved through the existing SHA-256-verified SD protocol, converted to PNG, annotated, and used for guide and video output.

The production firmware remains a native ESP-IDF project. Do not use PlatformIO.
For the current ESP-IDF 5.5.4 / ESP-Hosted 3.0.6 Tab5 dependency set, pins,
and non-negotiable hardware acceptance boundary, see
[`Tab5 ESP-Hosted 3.0.6 migration`](../tab5-esp-hosted-3-migration.md).

## Tab5 real-time UI rule

The Tab5 display, DSP, and speaker share the P4. Keep display work on the UI
owner; never move drawing, allocation, SD I/O, or diagnostics into IQ or audio
callbacks.

- A view that redraws a plot or the whole screen uses the DSI page-flip path:
  draw the complete frame into the back framebuffer, then present it once.
  This prevents the panel from showing an erased or partly rebuilt plot.
- A view that changes only a small, bounded region may update the visible
  framebuffer directly. Waterfall and audio spectrogram use this incremental
  path; do not convert them to full-frame copies without a measured need.
- Do not add a PSRAM sprite plus a full-frame memcpy for high-rate rendering.
  It competes with the radio/audio pipeline and regresses frame pacing.
- M5GFX's Tab5 double-framebuffer support is a pinned local patch. Native and
  M5Burner builds apply it through `tools/apply-m5gfx-tab5-pageflip.ps1`.
  Update the tracked patch with any M5GFX component upgrade; never edit only
  the ignored `managed_components` copy.

For any visual change, prove all three separately: native build; direct device
test with live audio/DSP; and, for a release, the private M5Burner install.
Use `RTL_UI_REGRESSION RUN` for the dashboard handoff baseline, then manually
exercise the changed view long enough to check for tearing, smooth motion, and
audio stutter.
