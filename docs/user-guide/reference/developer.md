# Developer reference

The help pipeline is manifest-driven:

```powershell
.\tools\build-help-media.ps1 -Port COM17 -Release <tag> -All
```

The first full run creates a Kokoro `am_michael` pronunciation sample and stops for approval. Rerun with `-ApproveVoice` after listening. Firmware capture commands require the existing HMAC-authenticated serial session. Captures are exact 1280×720 BMP frames, retrieved through the existing SHA-256-verified SD protocol, converted to PNG, annotated, and used for guide and video output.

The production firmware remains a native ESP-IDF project. Do not use PlatformIO.
For the current ESP-IDF 5.5.4 / ESP-Hosted 3.0.6 Tab5 dependency set, pins,
and non-negotiable hardware acceptance boundary, see
[`../../TAB5_ESP_HOSTED_3_MIGRATION.md`](../../TAB5_ESP_HOSTED_3_MIGRATION.md).
