# S01 — Sources

> **Category:** SYSTEM · **Dependency:** P3 fixture status; P2/P4 source verification; P5 extra adapters · **Status:** planned; capabilities and integrations below are not automatically present in OrcSDR.  
> Read [mandatory UI rules](../../UI_UX_STANDARD.md), [navigation contract](../../NAVIGATION.md), [data validity](../../DATA_CONTRACTS.md), [draw/test workflow](../../RENDERING_AND_ACCEPTANCE.md).

## Function and user benefit

Provide one clear truth table for what Drone Watch can and cannot currently receive on the connected hardware. The onboard ESP32-C6's theoretical PHY support must not be mistaken for access to passive BLE or Wi-Fi Remote ID in the installed ESP-Hosted firmware.

## How to use it

1. Open SYSTEM -> SOURCES from bottom menu or tap any source badge on WATCH.
2. Read whether BLE4, BLE5 extended/coded, Wi-Fi Beacon/NAN, OrcNode, RTL-SDR, microphone or analog video paths are SUPPORTED, ACTIVE, PAUSED, UNKNOWN or UNAVAILABLE.
3. Select a source to see version, interface, reason for absence and specific qualification evidence; START only if its verified adapter can start safely.

## 1280 × 720 display layout

Reference shell: shared OrcSDR header y0–113 owned by `dashboard_audio_control`; 52–60px in-category selector y118–178; content x20–1260/y188–634; bottom five-category tabs y649–705. The following rectangles are **suggested layout zones**, not duplicate hard-coded offsets per page.

- Left x20–805: vertical capability cards for currently detected adapters; group methods by one physical source with large status labels.
- Right x825–1260: selected source details with firmware version, acquisition callback state, last actual packet, error/gap counters and a short why-not explanation.
- Bottom in-content controls: SOURCE DETAILS, START/STOP when implemented, DIAGNOSTICS; no hidden firmware update/recovery action.

Keep the selected state and any saving/error banner legible on the real 5-inch Tab5; 52px minimum touch heights and descriptive button text. OrcSDR palette: black, dark panel, cyan focus, green measured-active, amber paused/uncertain, muted unavailable. All generic system pages must still clearly say what scanning is actually doing.

## Truthful fields and service dependencies

- SourceCapabilities entries with source_id, receiver_kind, actual supported transports and untested capability status.
- Live SourceStatus: last callback, active/scanning intervals, pause reason, firmware version, compatible library and error counters.
- Permission/provenance: if C6 firmware unqualified, BLE4/Beacon/NAN shown UNKNOWN not AVAILABLE; modem cannot own two incompatible modes simultaneously without test.

No mere presence of ESP32-C6 or an RTL-SDR dongle establishes Remote ID capability. No `SCANNING` without actual verified source callback health. `TEST DATA` must remain visible in replay on every related screen. Time and source identity are always explicit; avoid unsupported '0' figures and confidential device/network data in snapshots.

## Touch actions and navigation

- Select source updates inspector, not scan state; START/STOP uses approved adapter, immediate pending/success/fail feedback.
- Tap DIAGNOSTICS -> S05 filtered source; tap ORCNODE -> S02 for authenticated external nodes.
- No C6 flash/update or USB retune action in this screen. Settings for network credentials remain in existing global Settings.

The screen is a client of lifecycle/worker interfaces; global header controls and overlays have priority. No in-page action may change C6 firmware, silently retune the RTL-SDR, reboot a device or start microphone/video recording. Every slow action paints pending feedback before work.

## No source / paused / stale / partial failure

- ESP-Hosted Wi-Fi works but BLE capture not proven -> C6 REMOTE ID UNSUPPORTED/UNVERIFIED, not SCANNING.
- No packets despite active callback -> ACTIVE / NO RID OBSERVED, last packet --, not 0 aircraft.
- Wi-Fi connection scanning causes pause -> SCANNER PAUSED / WIFI BUSY; coverage gap visible W04.
- USB RTL-SDR absent -> SOURCE UNAVAILABLE, unrelated C6 statuses unaffected.

Every source or service that is unsupported remains informative; never create phantom data to fill empty lists.

## Builder how-to

1. Source capability probe happens outside UI from actual driver/firmware API; response may explicitly be unknown.
2. Define source group/cards driven by one SourceStatus array shared with all 25 views and W04 intervals.
3. Render card static layout once, update only status pill/age/counter areas, no repeated screen clearing.

One `screens::Id::drone_watch` framebuffer owner for all 25 screens, and common category/page router. Model/worker owns service interaction and source/progress snapshots; UI draws no network/SD/USB callbacks. Paint structural chrome once and dirty rectangles for changing values; never clear the whole page on simple progress updates or idle ticks.

### Adaptive 720 × 720

720-square: one-column list with selected-source full-page DETAIL; a compact count of sources rather than eight tiny dashboard tiles. Use the same ViewSnapshot and measure actual font/hit bounds.

## Screen-specific acceptance criteria

- C6 Wi-Fi connected alone never marks BLE4 Remote ID SCANNING.
- Changing selected source updates only inspector, status transition causes correct per-source panel dirties.
- Source unplug, Hosted reset, unsupported method, reconnect and clock loss each give explicit distinct statuses and no whole-frame repaint.

Add shared 1280×720/720×720 geometry tests, invalid/absent/replay tests, global Home/Settings/volume overlay priority, 100 changing-value ticks with unchanged full-screen-clear count, no inactive display writes, and actual physical Tab5/ESP-IDF/USB regression results before declaring hardware acceptance. The document alone does not implement the screen.
