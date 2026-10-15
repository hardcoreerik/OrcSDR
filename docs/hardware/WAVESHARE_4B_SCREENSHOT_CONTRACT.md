# Waveshare 4B — OrcSDR screenshot and UI reference contract

**Created: 2026-10-14. Status: source-reference audit; screenshot files located and checked against source modules, NOT a completed pixel-level review of every image or a rendered/physical visual acceptance.**

The user rejected the first 720-square Waveshare Home UI because it discarded OrcSDR's actual visual identity. **Treat project images and the working Tab5 firmware as the only visual source of truth; do not substitute generic SDR screenshots, web image results, or an invented color palette.**

## Reference provenance

- Public GitHub Wiki: https://github.com/hardcoreerik/OrcSDR/wiki — the Home page, actual 1280×720 Home screenshot and Screenshot Catalog are now accessible by following the Wiki link. The [Wiki catalog](https://github.com/hardcoreerik/OrcSDR/wiki/Screenshot-Catalog) inventories **94 captures, 78 public**, beyond the 24 repo-tracked dashboard image files. Screen-by-screen pixel comparison is still pending.
- **Confirmed, tracked screenshot library:** `docs/images/dashboards/` on `main`, 24 PNG captures covering five groupings below. The exact assets are embedded below as GitHub-renderable image references for human/Codex comparison.
- Additional larger project screenshots: `docs/images/orcsdr-tab5-live.jpeg`, `docs/images/orcsdr-fm-dashboard.jpg`, `docs/images/orcsdr-adsb-dashboard.jpg`, `docs/images/orcsdr-rf-lab-dashboard.jpg`, `docs/images/orcsdr-visualization-modes.jpg`.
- Public user guide: `docs/user-guide/` with FM, P25, ADS-B, LoRa, FT8, other-band guides; the automatic `reference/screen-catalog.md` is currently only a placeholder, not a complete screenshot catalog.
- Active Tab5 source, rather than dated concept art, resolves ambiguities between a screenshot and current controls. Original images may be from a previous firmware revision; record the screenshot commit/date when known.

## Screenshot gallery — compare every screen, do not blanket-scale

### Home

**HOME** — [original PNG](../images/dashboards/home.png)

![OrcSDR original Home: HOME](../images/dashboards/home.png)

### FM RADIO

**LISTEN** — [original PNG](../images/dashboards/fm-listen.png)

![OrcSDR original FM RADIO: LISTEN](../images/dashboards/fm-listen.png)

**SPECTRUM** — [original PNG](../images/dashboards/fm-spectrum.png)

![OrcSDR original FM RADIO: SPECTRUM](../images/dashboards/fm-spectrum.png)

**STATION / RDS** — [original PNG](../images/dashboards/fm-station-rds.png)

![OrcSDR original FM RADIO: STATION / RDS](../images/dashboards/fm-station-rds.png)

**RF HEALTH** — [original PNG](../images/dashboards/fm-rf-health.png)

![OrcSDR original FM RADIO: RF HEALTH](../images/dashboards/fm-rf-health.png)

**SETTINGS** — [original PNG](../images/dashboards/fm-settings.png)

![OrcSDR original FM RADIO: SETTINGS](../images/dashboards/fm-settings.png)

### P25 RADIO

**MONITOR** — [original PNG](../images/dashboards/p25-monitor.png)

![OrcSDR original P25 RADIO: MONITOR](../images/dashboards/p25-monitor.png)

**SPECTRUM** — [original PNG](../images/dashboards/p25-spectrum.png)

![OrcSDR original P25 RADIO: SPECTRUM](../images/dashboards/p25-spectrum.png)

**TALKGROUPS** — [original PNG](../images/dashboards/p25-talkgroups.png)

![OrcSDR original P25 RADIO: TALKGROUPS](../images/dashboards/p25-talkgroups.png)

**SYSTEMS / PROGRAM** — [original PNG](../images/dashboards/p25-program.png)

![OrcSDR original P25 RADIO: SYSTEMS / PROGRAM](../images/dashboards/p25-program.png)

**RF HEALTH** — [original PNG](../images/dashboards/p25-rf-health.png)

![OrcSDR original P25 RADIO: RF HEALTH](../images/dashboards/p25-rf-health.png)

### LORA / MESH

**OVERVIEW** — [original PNG](../images/dashboards/lora-overview.png)

![OrcSDR original LORA / MESH: OVERVIEW](../images/dashboards/lora-overview.png)

**NODES** — [original PNG](../images/dashboards/lora-nodes.png)

![OrcSDR original LORA / MESH: NODES](../images/dashboards/lora-nodes.png)

**TRAFFIC** — [original PNG](../images/dashboards/lora-traffic.png)

![OrcSDR original LORA / MESH: TRAFFIC](../images/dashboards/lora-traffic.png)

**MAP** — [original PNG](../images/dashboards/lora-map.png)

![OrcSDR original LORA / MESH: MAP](../images/dashboards/lora-map.png)

**RF HEALTH** — [original PNG](../images/dashboards/lora-rf-health.png)

![OrcSDR original LORA / MESH: RF HEALTH](../images/dashboards/lora-rf-health.png)

### GLOBAL SETTINGS

**CONNECTIVITY** — [original PNG](../images/dashboards/settings-connectivity.png)

![OrcSDR original GLOBAL SETTINGS: CONNECTIVITY](../images/dashboards/settings-connectivity.png)

**DISPLAY / AUDIO** — [original PNG](../images/dashboards/settings-display-audio.png)

![OrcSDR original GLOBAL SETTINGS: DISPLAY / AUDIO](../images/dashboards/settings-display-audio.png)

**RADIO DEFAULTS** — [original PNG](../images/dashboards/settings-radio-defaults.png)

![OrcSDR original GLOBAL SETTINGS: RADIO DEFAULTS](../images/dashboards/settings-radio-defaults.png)

**LOCATION / ADS-B** — [original PNG](../images/dashboards/settings-location-adsb.png)

![OrcSDR original GLOBAL SETTINGS: LOCATION / ADS-B](../images/dashboards/settings-location-adsb.png)

**DATA / MAPS** — [original PNG](../images/dashboards/settings-data-maps.png)

![OrcSDR original GLOBAL SETTINGS: DATA / MAPS](../images/dashboards/settings-data-maps.png)

**STORAGE** — [original PNG](../images/dashboards/settings-storage.png)

![OrcSDR original GLOBAL SETTINGS: STORAGE](../images/dashboards/settings-storage.png)

**COMPANION** — [original PNG](../images/dashboards/settings-companion.png)

![OrcSDR original GLOBAL SETTINGS: COMPANION](../images/dashboards/settings-companion.png)

**SYSTEM** — [original PNG](../images/dashboards/settings-system.png)

![OrcSDR original GLOBAL SETTINGS: SYSTEM](../images/dashboards/settings-system.png)

**Tracked gallery: 24 images (1 Home + 5 FM + 5 P25 + 5 LoRa + 8 Settings).**

## Additional full-frame / feature screenshots

- [Actual Tab5 live radio](../images/orcsdr-tab5-live.jpeg)
- [FM dashboard](../images/orcsdr-fm-dashboard.jpg)
- [ADS-B dashboard](../images/orcsdr-adsb-dashboard.jpg)
- [RF Lab dashboard](../images/orcsdr-rf-lab-dashboard.jpg)
- [Visualization modes](../images/orcsdr-visualization-modes.jpg)
- [Primary OrcSDR image](../images/OrcSDR-Main.png)

## Concrete source-of-truth mapping

| Surface | Actual source | Porting requirement |
|---|---|---|
| Shared header / Orc badge / navigation buttons / status treatment | `apps/orcsdr-tab5/ui/dashboard_audio_control.cpp`; `apps/orcsdr-tab5/main/orc_badge_104.rgb565` | Reuse original badge, brand wordmark, status semantic groups and visual accent. Do not brand the app "WAVESHARE 4B". |
| Home | `apps/orcsdr-tab5/ui/home_dashboard.cpp` | Recent dashboard navigation/browse, tune/keypad/mode/filter/gain, frequency readout, real spectrum/waterfall and settings/header. A prototype subset may be explicitly marked unsupported, never called parity. |
| FM five-panel dashboard | `apps/orcsdr-tab5/ui/fm_dashboard.cpp` | LISTEN/SPECTRUM/STATION-RDS/RF HEALTH/SETTINGS are **five distinct views**. The original UI positions its tabs at the bottom of the Tab5; the 4B may wrap/scroll while retaining their order, labels, identity and behavior. |
| P25 five-panel dashboard | `apps/orcsdr-tab5/ui/p25_dashboard.cpp` | MONITOR/SPECTRUM/TALKGROUPS/SYSTEMS/RF HEALTH; never conflate monitored system state with tuner-only RF. |
| LoRa five-panel dashboard | `apps/orcsdr-tab5/ui/lora_dashboard.cpp` | OVERVIEW/NODES/TRAFFIC/MAP/RF HEALTH; preserve topology/data honesty and per-screen palette controls. |
| Settings | Tab5 settings source under `apps/orcsdr-tab5/ui/`; `docs/user-guide/settings.md` | Eight documented sections listed above, consistent header and navigate-back semantics; don't present unimplemented P4 peripherals as READY. |
| Waterfall styles | `apps/orcsdr-tab5/ui/waterfall_style.cpp`, `.hpp` | **Reuse exact seven palette implementations** and speed semantics, with Home default CLASSIC; no improvised pseudo-heat palette. |
| Screen ownership | `apps/orcsdr-tab5/ui/screen_controller*`, `architecture.md` | One active UI owner; DSP / audio/USB state not owned by screen. Port renders snapshots without competing for USB/SDR ownership. |

## Original color and frame contract from current source

- Background: `TFT_BLACK`.
- Common panel surface: RGB565 `0x0841`.
- Common cyan: `0x2E7F`; active green: `0x6FE8`; muted: `0x8C71`; grid: `0x2945` in `dashboard_audio_control.cpp` and FM/P25/LoRa.
- Home uses slightly different existing panel/frame accent values: `kPanel=0x0021`, `kCyan=0x05FF`, `kGreen=0x6FE0`, `kDim=0x4228`. **Do not flatten these differences into one guessed theme.**
- Brand `OrcSDR`, source Orc badge, dashboard title, Wi-Fi/RTL-SDR/clock status, right-side controls, cyan outlined rounded panels, bright green operational values.
- Original status bar dimensions `x420..866, y12..66` are **Tab5-specific geometry**; preserve the meaning, grouping, emphasis and availability states, not the numeric coordinates. On 4B, no false battery/Wi-Fi/USB/audio readiness.

## Per-screen qualification BEFORE production flashing

1. Select one of the original screenshots **and** its present-day source module. Make a visible inventory of header, navigation, primary data, actions, status fields and overlays. Record all actions and which are not yet ported.
2. Draw/propose a real 720×720 arrangement using the same brand assets, actual colors, signal palettes and native-size touch targets. Never just scale 1280×720 or drop controls without an explicit alternate screen/action path.
3. Generate a host-rendered screenshot that the user can inspect, with deterministic **labeled simulated visual data only** (never fake receiver/RF values on the device). Record dimensions, contrast and text legibility checks.
4. Obtain specific user visual approval on **that single screen** before treating the theme as accepted. Build may proceed for qualification, but do not tell the user the port is visually complete while rejected.
5. Native-compile, flash only with specific board/rev approval, photograph the actual display, check every hitbox, compare original-reference vs rendered-image vs device photo, and log acceptance.
6. Confirm readouts and RF against measured signals separately. Home must pass before an additional dashboard is treated as accepted. Port other screens sequentially.

## Current 4B findings

- ESP32-P4 v1.3, display, PSRAM and partial GT911 interaction ran on physical board (see `WAVESHARE_4B_SCREEN_GATES.md`).
- Existing 4B `main.cpp` is a functional IQ-focused prototype and **failed OrcSDR UI parity**. Do not reuse its invented palette/header/button design as the base template.
- Radio/USB reception remains blocked by unqualified USB-OTG power/attachment path; see `WAVESHARE_4B_USB_HOST_POWER.md`.
- This document creates a reproducible reference set. It **does not** assert every source screenshot was pixel-audited, that the Wiki's 94-item catalog was exhaustively reviewed or that the Waveshare port passed a screenshot comparison.

## Final principle

**Same OrcSDR product, different viewport. The board is hardware; it is not a new app.**
