# Drone Watch — mandatory OrcSDR UI/UX contract

> **Design authority:** `apps/orcsdr-tab5/ui/AGENTS.md`, `docs/UI_RENDERING_STANDARD.md`, `docs/superpowers/specs/2026-09-15-dashboard-header-design.md`, and the active ScreenController and shared header sources. **The application code is authoritative if coordinates or conventions change.** This file is an extension for Drone Watch, not permission to diverge.

## Product promise

One dashboard named **DRONE WATCH**, reached from the OrcSDR Home/dashboard registry. It contains **five categories**, with **25 proposed screens** within them. A beginner opens WATCH and immediately sees source availability, last legitimate observation and one obvious next action. Expert views live under the other pages; nothing needs setup to *understand* the default screen. **No synthetic activity may be represented as live reception.** All descriptions are proposed functionality, not current OrcSDR capabilities.

## Display geometry — Tab5 landscape reference, 1280 × 720

| Region | Suggested logical bounds (px) | Owner |
| --- | --- | --- |
| Canonical shared header | `y=0..113` | Existing `dashboard_audio_control`: Orc badge, Home, volume/mute, visualizer, Settings, battery; dashboard title/status occupies only free area |
| Secondary view strip | `x=20..1260, y=118..178` | Drone Watch intra-category router; 52–56 px touch height; labels/page counter where needed |
| Content safe area | `x=20..1260, y=188..634` | Active inner view only; 20px horizontal margin, 12–16px gaps, no drawing under header/strip/footer |
| Primary category bar | `y=649..705`, five evenly sized buttons | Drone Watch category router, roughly 238px target widths at 1280; leave small bottom margin |
| Modal/overlay | within active screen safe bounds, or existing approved global overlay | Single overlay owner; overlay focus blocks underlying touch handlers |

These are **design coordinates**, NOT hard-coded constants to duplicate in all 25 files. Implement one `DroneLayout` solver using actual display width/height, safe regions provided by the shared shell, font metrics and touch target constraints. Reference Dead Air's bottom tab line `kTabY=649, kTabHeight=52, kBodyY=114`, but do not copy its full-screen sprite allocation.

### 720 × 720 and unusual display sizes

- Use the **same view-model and drawing code**, not 25 independent device-specific screens. Adapt columns, typography and scroll/selection model based on measured available width and height.
- For 720×720: keep five main category buttons with abbreviated text/icons only if each remains >=52px high and touch-safe; switch secondary view strip to a selected-page label plus `PREV` and `NEXT` targets or a single selector. A right-hand inspector becomes a detail page below a selected row, not a sub-300px column.
- For smaller/round/ultrawide screens in later OrcUI work, respect clipping, safe insets and an explicit `unsupported layout` diagnostic until verified; never stretch 1280px absolute hit regions.
- Preserve chart readability, display units, wrap long IDs in bounded details, use pagination/virtual rows instead of 8-point fonts. Do not truncate an ID silently if truncation could make two transmitters indistinguishable: show `...` plus a detail control.

## OrcSDR appearance, meaning and legibility

- Reuse `dashboard_audio_control` and shared palette. Verified current reference constants: background `TFT_BLACK`, panel `0x0841`, edge `0x2945`, cyan `0x2e7f`, green `0x6fe8`, muted `0x8c71`. Additional amber/error colors must use agreed OrcSDR semantic equivalents; do not invent separate branded palettes per view.
- Use **large metrics 36–56 px**, section headers 26–32 px, primary rows 20–26 px and secondary text approximately 18–20 px where fonts permit. These are minimum **physical readability goals** subject to real M5GFX glyph measurement; no tiny 12px multi-column cockpit.
- All interactive touches >=52px high (and normally >=52px wide), with 8–12px spacing; page tabs, selected row, scroll arrows and modal close must have different hit rectangles. Color is not the sole carrier of state.
- Status is wording-first: `REMOTE ID RECEIVED`, `SCANNING BLE`, `NO RID OBSERVED`, `SCANNER PAUSED`, `SOURCE UNAVAILABLE`, `TEST DATA`. `NO DRONES`, `AIRSPACE CLEAR`, `DRONE CONFIRMED`, `PILOT LOCATED`, `THREAT` from radio-only observations are prohibited.
- The wide status area communicates one primary answer; optional measurements belong in DETAILS. Avoid decorative fake waveforms, imaginary map dots, and meaningless gauge animations.
- Time always says `last seen X s ago` from monotonic receive time; `UTC` only when clock quality is known. Frequency shows MHz/kHz and source/band coverage; RSSI must be true receiver-reported dBm, not RTL baseband dBFS relabeled. Separate hardware firmware version, receiver observation and transmitted aircraft claims.
- Information/help icon is available for unfamiliar jargon; short explanation first, exact decoder details second. Any disabled advanced feature shows why and what equipment/verification is needed.

## Interaction and navigation invariants

1. Global Home, volume, visualizer, Settings, battery and overlays have precedence over all Drone Watch touches.
2. Bottom category selection changes Drone Watch category; selected category restores its most recent inner screen when re-entered during the same session, unless a context action explicitly requests another destination. First open = WATCH / LIVE.
3. Internal top strip provides screens. It may use a compact list/selector on narrow displays; it may NOT make five categories each into separate top-level dashboard registry entries.
4. Row tap opens detail with a reliable back stack. Returning preserves filter, list scroll and selected source where possible; closed/expired record shows `NO LONGER CURRENT`.
5. Press feedback appears immediately before an NVS/SD/network change; busy, success and failure are separate statuses. Never strand the user behind a blocking call.
6. Leaving the dashboard restores prior band/session and does not retune the RTL-SDR or reset C6 firmware. Screen inactive = **zero display writes** from its renderer.
7. `test/replay` is a mutually exclusive explicit mode with an always-visible `TEST DATA` marker on every involved screen; normal live input cannot silently mix with recorded fixture events.

## Mandatory rendering invariants

Source: `docs/UI_RENDERING_STANDARD.md` and `apps/orcsdr-tab5/ui/AGENTS.md`.

- Draw chrome once on entry or true layout transition. A new model revision or timer tick **MUST NOT** call `fillScreen` or redraw the complete content area. Use widget-specific dirty rectangles; clear the old value only inside its own rectangle, then redraw the new value.
- Data/animations update changed regions only. Skip updates while another category/page owns the framebuffer. Bound redraw frequency, coalesce bursts of source packets and invalidate expired rows.
- Keep the previously completed map/video frame visible while a new image/map tile frame is prepared; label freshness separately. Never let changed content erase map attribution, controls, volume overlays or focus.
- One owner: `screens::Id::drone_watch` for the entire suite (proposed). Inside it, use `Category` + `Page`; do not register 25 new ScreenController IDs. No C6/USB/IQ/network/journal callback draws pixels.
- Prefer partial row buffers, chart buffers, existing map_service cache, or measured PSRAM sprites only after profiling. No default 1280×720 × two framebuffer allocation.
- Save measured counters for driver overruns, application IQ pipeline drops, frame updates, PSRAM/internal DMA reserves and scan gaps; a responsive UI alone does not prove radio continuity. Preserve the project's explicitly narrow accepted map-pan limitation; do not generalize that into acceptance for Drone Watch.

## Universal view states

Each screen spec must implement or explicitly explain:
`initial/loading`, `active with data`, `active with no qualifying observations`, `source absent/unsupported`, `paused with reason`, `stale/expired`, `malformed/untrusted data`, `busy or saving`, `storage/network error` where relevant, `TEST DATA`, and `recovery`.

Present `--` plus reason for unavailable numeric values, never a fabricated zero. A green RX icon indicates actual supported acquisition active—not just BLE silicon present. Any aviation coordinate from Remote ID must be labeled **broadcast-reported and unverified**.

## Acceptance required of every completed view

- Host snapshot tests for zero/one/many data, stale event, malformed fields, invalid timestamp/coordinate, unsupported source, navigation, 1280×720/720×720 geometry and overlay hit precedence.
- **Recording-display redraw test:** change values many times and assert updated pixels/text with **unchanged full-screen-clear count** after entry. Test idle timer ticks, simulated burst, background updates on an inactive view and repeated exit/re-enter.
- Native ESP-IDF consumer build + memory/binary headroom; physical 5-inch readability/touch/brightness/flicker tests separately, with exact hardware/build/driver pin/logs.
- Compare FM/RDS, ADS-B, LoRa, existing map, Wi-Fi/Hosted, SD and Dead Air behavior. Do not approve apparent smoothness when receiver loss counters regress.
- Cap events/rows and string widths; ensure a loss/error counter is exposed, not silently ignored.

**Before implementation:** read current code and update this reference if the shared header or adaptive layout system has advanced. This document does not override newer source or safety/approval gates.
