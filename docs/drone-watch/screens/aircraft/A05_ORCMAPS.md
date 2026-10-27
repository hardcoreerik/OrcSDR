# A05 — OrcMaps

> **Category:** AIRCRAFT · **Phase dependency:** P4 existing shared map service + verified positions · **Status:** proposed and unimplemented.  
> Shared [UI/UX rules](../../UI_UX_STANDARD.md), [routing](../../NAVIGATION.md), [data contracts](../../DATA_CONTRACTS.md), [rendering/tests](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and plain-language UX

Offer a familiar map of **aircraft-broadcast reported locations**, using the same OrcMaps engine, map packs, attribution, camera and Labels On/Off preference as other OrcSDR consumers. Do not create a new basemap renderer.

## User how-to

1. Tap MAP from Aircraft Details or open AIRCRAFT -> ORCMAPS.
2. See a position pin only for a valid report, with LAST REPORTED age and UNVERIFIED BROADCAST LOCATION.
3. Tap a pin to inspect related broadcast and source; toggle labels and pan/zoom using existing map controls.

## Tab5 1280 × 720 layout specification

Reference shell is reserved: common OrcSDR header y0–113, 52–60px inner-screen selector y118–178, content-safe rectangle x20–1260/y188–634, five global categories in y649–705. All controls go through the shared layout/hit system, not 25 independent coordinate owners.

- x20–886: shared map viewport with existing required map attribution and labels controls; no map geometry invented in this feature.
- x906–1260: selected aircraft claim, last reported age, reported coordinate validity, receiver origin and MAP PACK status.
- Map side controls reserve touch >=52px; overlays and labels must remain clipped to viewport, never obscure global header or footer.

Typography defaults to large 26–32px headings, 20–26px list rows and 36–56px headline metrics subject to M5GFX font measurement. Use current OrcSDR background/panel/cyan/green/muted palette. Green means actual supported reception, amber means caution/invalid/paused, not threat level. Each active tap region >=52px.

## Field requirements and provenance

- Valid paired broadcast-reported lat/lon with monotonic age, source_id, claim ID and track_key; no geolocation derived from RSSI.
- Map adapter projection/camera metadata, map state/loading/reused last frame, installed pack and attribution.
- Observer's location kept separately; distance/bearing omitted unless observer location explicitly set, valid and users choose to reveal it.

Every reported location, ID, timestamp, altitude or speed remains a *broadcast claim* or a separately identified receiver measurement. Use true valid/absent/invalid states; never synthesize positions or interpret unknown ID as confirmed anonymous aircraft. TEST DATA remains clearly visible in every replay-derived view.

## Tap controls and screen navigation

- Pan/zoom requests shared asynchronous map service; labels ON/OFF uses existing geographic label preference.
- Tap pin -> A02 Details; source -> S01; FIT VALID REPORTS acts only on currently valid plotted markers.
- No map pack -> existing offline pack picker or map availability explanation, not silent download.

Global Home/volume/Visualizer/Settings take precedence. Back preserves source/selected observation and scroll when record remains available. The whole suite has one ScreenController owner.

## No-data / unsupported / stale / failure presentation

- Reported coordinate invalid/stale -> pin removed or marked historical with exact age under policy; never left live.
- Map rendering pending -> retain prior completed map frame until replacement ready and show small loading status.
- Map service unavailable -> textual coordinate list remains, no invented world silhouette.
- No positions -> NO VALID REPORTED LOCATIONS; TEST DATA pins always marked demo.

A source cannot show SCANNING merely because chipset and UI exist. Do not silently turn on an RTL tuner or alter C6 firmware. Time since reception uses monotonic clock; UTC only if verified.

## Builder how-to (work units)

1. Reuse OrcMaps `map_service` / `map_view` and existing projection and labels preference; no forked tile store or synchronous SD tile reads in draw.
2. Keep immutable pin overlay separate from shared basemap; overlay source IDs and age labels in bounded sprite/dirty regions.
3. When pan/fill completes, UI owner composites resulting frame; inspect map attribution and redraw while volume tray is overlaid.

View draws from immutable `DroneWatchSnapshot` on UI task only. Keep received packet parsing, network I/O, storage and map tiles in proper model/adapters/workers. Draw chrome on structural entry only, then invalidate bounded rows/cards and skip unchanged widgets. Avoid a per-screen full-frame framebuffer.

### 720 × 720 adaptive behavior

720×720: nearly full-width map panel plus a bottom-selected-pin summary overlay within content bounds; switch list/details via a prominent touch button. Derive from a common layout solver with touch-safe secondary navigation, not cloned screens.

## Acceptance cases unique to this screen

- Two known valid coordinates project consistently with other consumers at same camera; invalid coordinate never plots.
- Map pack absent/offline -> no invented position, no auto-download; labels preference honored.
- Rapid pan while active RF: measure drops and screen progress, preserve last image until new frame; no full-screen clear for time updates.

Also run shared visual regression: 100 value/timer ticks update correct pixels without increasing the full-screen-clear count, off-page state publishes without screen writes, overlay clipping, 1280/720 geometry, and separately recorded physical Tab5 readability and RF continuity. Treat implementation, host build and hardware/RF evidence as distinct outcomes.
