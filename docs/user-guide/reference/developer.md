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
  M5Burner builds apply it through
  `apps/orcsdr-tab5/tools/apply-m5gfx-tab5-pageflip.ps1`.
  Update the tracked patch with any M5GFX component upgrade; never edit only
  the ignored `managed_components` copy.

For any visual change, prove all three separately: native build; direct device
test with live audio/DSP; and, for a release, the private M5Burner install.
Use `RTL_UI_REGRESSION RUN` for the dashboard handoff baseline, then manually
exercise the changed view long enough to check for tearing, smooth motion, and
audio stutter.

## Receiver driver regression

Run the capability-based gate after normal startup and active IQ streaming:

```powershell
.\apps\orcsdr-tab5\tools\run-tab5-ui-regression.ps1 -Port COM17 -PairingKeyPath <local-key-path> -DriverRegression -LogPath <log-path>
```

The version is recorded for provenance, not used as a pass threshold. The gate checks the recognized profile, capabilities, exact tuning, HF route, live IQ and getter health, gain/AGC transitions and restoration. Nooelec V5 uses its driver's 100 kHz–1.75 GHz limits; out-of-range probes must be rejected without interrupting reception. The old `-Driver080Rc2` and `-Driver080Rc3` names remain aliases.

Bias-tee testing is off by default. Do not pass `-TestBiasTee` with the externally powered MLA-30+ setup. Close other serial monitors before the runner owns COM17.

For this mode, `-ResetDevice` preserves the splash gate rather than disabling startup staging. Prefer a manual power reboot followed by the normal SD, Wi-Fi, receiver and post-splash ESP-NOW sequence when validating startup. Other unattended UI modes retain their existing splash bypass. A driver API pass is separate from visual, startup, RF and package acceptance; this gate is not a waterfall test. Low-level test retunes temporarily move away from the dashboard's selected band.
