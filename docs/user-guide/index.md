# OrcSDR User Guide

OrcSDR turns an M5Stack Tab5 and a fully supported SDR receiver (RTL-SDR Blog V4, V3c, V4 Lite, or Nooelec NESDR SMArt V5) into a portable, touch-first radio workspace. This public guide starts with the physical setup, then explains each dashboard, common workflow, limitation, and diagnostic.

The labels used throughout the guide are deliberate:

- **Available / Implemented** — the integrated feature is present in current source; the published firmware package may lag.
- **RF-validated / Hardware-tested** — a specific physical test is documented, not a promise for all RF conditions.
- **Limited / In development** — important functionality is missing or integration remains underway.
- **Demo** — deterministic sample data, always marked `DEMO` on the device.
- **Unavailable / Planned** — not offered as working functionality.

See the [feature availability matrix](feature-status.md) and [status terminology](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/STATUS_TERMINOLOGY.md) before interpreting dated RF validation records.

Start with [Getting started](getting-started.md), then use the searchable [screen catalog](reference/screen-catalog.md) for a control-by-control view.

Want to test the latest merged code? Follow [Build from source](build-from-source.md).
It explains the P4 build, application-only flash, and why C6 updates are
optional for normal OrcSDR use.

For digital reception, see [FT8 reception](dashboards/ft8.md), [LoRa / Meshtastic](dashboards/lora.md), and [Dead Air's measurement-versus-interpretation guide](dashboards/dead-air.md).

For the maps integration development builds, see [Offline maps](offline-maps.md)
for in-app downloading and manual SD-card installation. The currently published
global-map catalog is signed for production and has no scheduled expiry.
