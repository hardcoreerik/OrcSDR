# A06 — ADS-B Context

> **Category:** AIRCRAFT · **Phase dependency:** P4 optional reuse of existing ADS-B source when active · **Status:** proposed and unimplemented.  
> Shared [UI/UX rules](../../UI_UX_STANDARD.md), [routing](../../NAVIGATION.md), [data contracts](../../DATA_CONTRACTS.md), [rendering/tests](../../RENDERING_AND_ACCEPTANCE.md).

## Purpose and plain-language UX

Let an observer compare independent nearby ADS-B aircraft with Remote ID observations without asserting that a commercial flight and a particular Remote ID message refer to the same aircraft.

## User how-to

1. Open ADS-B CONTEXT from AIRCRAFT after a valid RID observation or as an independent list.
2. Read ADS-B source state separately from Remote ID receive state; inspect what ADS-B actually decoded.
3. Tap OPEN ADS-B to enter the existing OrcSDR ADS-B dashboard, or return to Drone Watch without falsely merging tracks.

## Tab5 1280 × 720 layout specification

Reference shell is reserved: common OrcSDR header y0–113, 52–60px inner-screen selector y118–178, content-safe rectangle x20–1260/y188–634, five global categories in y649–705. All controls go through the shared layout/hit system, not 25 independent coordinate owners.

- Left x20–835: two clearly separated source panels: REMOTE ID CLAIMS and ADS-B RECEIVED; show age/source for each.
- Right x855–1260: context explanation, source operating state and selected actual ADS-B fields if active.
- One large OPEN ADS-B button with clear effect on receiver ownership; no split map that duplicates ADS-B renderer.

Typography defaults to large 26–32px headings, 20–26px list rows and 36–56px headline metrics subject to M5GFX font measurement. Use current OrcSDR background/panel/cyan/green/muted palette. Green means actual supported reception, amber means caution/invalid/paused, not threat level. Each active tap region >=52px.

## Field requirements and provenance

- Only existing ADS-B application's actual tracked aircraft snapshot if a safe read-only adapter exists; don't start ADS-B reception from a passive context page.
- Independent ADS-B ICAO/callsign/position and clock, and separate Remote ID reported fields; do not auto-match IDs.
- Radio ownership: single RTL-SDR can be on ADS-B 1090 or another band—not simultaneously unless explicitly measured architecture supports that.

Every reported location, ID, timestamp, altitude or speed remains a *broadcast claim* or a separately identified receiver measurement. Use true valid/absent/invalid states; never synthesize positions or interpret unknown ID as confirmed anonymous aircraft. TEST DATA remains clearly visible in every replay-derived view.

## Tap controls and screen navigation

- OPEN ADS-B -> normal OrcSDR dashboard transition using existing receiver control/return-state semantics; no silent frequency takeover.
- SELECT RID -> A02 Details; SELECT ADS-B -> existing ADS-B Details only through approved adapter.
- HELP explains the systems are separate and simultaneous reception is not guaranteed.

Global Home/volume/Visualizer/Settings take precedence. Back preserves source/selected observation and scroll when record remains available. The whole suite has one ScreenController owner.

## No-data / unsupported / stale / failure presentation

- No ADS-B stream while RTL-SDR elsewhere -> ADS-B NOT CURRENTLY RECEIVING rather than NONE NEARBY.
- No access to ADS-B snapshot -> UNAVAILABLE and OPEN ADS-B option, not a mirrored demo list.
- No established identity association -> CONTEXT ONLY, NOT MATCHED.
- Replay source -> TEST DATA, keep separation even with coincident coordinates.

A source cannot show SCANNING merely because chipset and UI exist. Do not silently turn on an RTL tuner or alter C6 firmware. Time since reception uses monotonic clock; UTC only if verified.

## Builder how-to (work units)

1. Use a read-only adapter to existing ADS-B state, avoiding duplicate decoder/cache/aircraft tracker; keep lifetimes and threading correct.
2. Keep context screen lightweight: no extra USB capture, no second drawing task, no new map tile cache.
3. Route to ADS-B as a normal external ScreenController transition with explicit mode transfer; restore prior session on return as supported.

View draws from immutable `DroneWatchSnapshot` on UI task only. Keep received packet parsing, network I/O, storage and map tiles in proper model/adapters/workers. Draw chrome on structural entry only, then invalidate bounded rows/cards and skip unchanged widgets. Avoid a per-screen full-frame framebuffer.

### 720 × 720 adaptive behavior

720×720: two vertically stacked summaries and one OPEN ADS-B button; no simultaneous side-by-side source columns. Derive from a common layout solver with touch-safe secondary navigation, not cloned screens.

## Acceptance cases unique to this screen

- Tune RTL-SDR away from 1090 and verify no fabricated active ADS-B source status.
- Matching coordinates and timestamps do not create an automatic same-aircraft identifier.
- Transition OPEN ADS-B and return with correct receiver state and no inactive Drone Watch draws.

Also run shared visual regression: 100 value/timer ticks update correct pixels without increasing the full-screen-clear count, off-page state publishes without screen writes, overlay clipping, 1280/720 geometry, and separately recorded physical Tab5 readability and RF continuity. Treat implementation, host build and hardware/RF evidence as distinct outcomes.
