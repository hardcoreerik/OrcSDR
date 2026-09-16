# OrcSDR versioning

OrcSDR uses Semantic Versioning: `MAJOR.MINOR.PATCH[-stageN]`.

- **alphaN** — incomplete experimental work; APIs and behavior may change.
- **betaN** — the intended product is usable, while behavior and UX continue
  to evolve through validation.
- **rcN** — the exact stable release is frozen; only release-blocking fixes,
  evidence, and release documentation should change.
- **No suffix** — stable release.

The driver and application version independently: `esp_rtl_sdr` is `0.8.0-rc3`
for this release, while OrcSDR is `0.2.0-beta7`. A private test build is
identified by its full source commits, artifact SHA-256, and build number, not
by inventing a mixed beta/RC suffix.
