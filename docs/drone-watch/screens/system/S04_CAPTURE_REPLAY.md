# S04 — Capture Replay

> **Category:** SYSTEM · **Dependency:** P1 host packet fixtures / P3 protected UI replay / P5 saved session replay · **Status:** planned; capabilities and integrations below are not automatically present in OrcSDR.  
> Read [mandatory UI rules](../../UI_UX_STANDARD.md), [navigation contract](../../NAVIGATION.md), [data validity](../../DATA_CONTRACTS.md), [draw/test workflow](../../RENDERING_AND_ACCEPTANCE.md).

## Function and user benefit

Make deterministic tests repeatable in-device while *never* confusing them with live RF. This is primarily a development/diagnostic facility that can later replay an owner's saved session safely.

## How to use it

1. Open SYSTEM -> CAPTURE REPLAY; default LIVE mode is untouched and fixture tools require an explicit DEVELOPMENT/REPLAY entry.
2. Select a bundled known fixture or permitted saved session; preview source, schema, time range and whether raw payloads were retained.
3. Tap START REPLAY to enter a separate source mode with a persistent TEST DATA banner across WATCH/MAP/DETAILS, STEP or PAUSE, and RETURN TO LIVE.

## 1280 × 720 display layout

Reference shell: shared OrcSDR header y0–113 owned by `dashboard_audio_control`; 52–60px in-category selector y118–178; content x20–1260/y188–634; bottom five-category tabs y649–705. The following rectangles are **suggested layout zones**, not duplicate hard-coded offsets per page.

- x20–810: fixture/session list with capture origin, protocol/version, event count and validity warnings.
- x830–1260: selection inspector and large state machine panel IDLE / TEST DATA PLAYING / PAUSED / ENDED; SPEED/STEP only if safely supported.
- Persistent TEST DATA band in navigation/status whenever replay route feeds views; playback scrub only for actually retained frames.

Keep the selected state and any saving/error banner legible on the real 5-inch Tab5; 52px minimum touch heights and descriptive button text. OrcSDR palette: black, dark panel, cyan focus, green measured-active, amber paused/uncertain, muted unavailable. All generic system pages must still clearly say what scanning is actually doing.

## Truthful fields and service dependencies

- Immutable fixture key, schema/revision, event count, source provenance, monotonically ordered capture times, payload permission, clock quality.
- Playback state, current test event index, replay synthetic clock vs actual local device monotonic clock, loop/seek status.
- Isolation flag that guarantees no replay observation enters actual live detection statistics or live session log.

No mere presence of ESP32-C6 or an RTL-SDR dongle establishes Remote ID capability. No `SCANNING` without actual verified source callback health. `TEST DATA` must remain visible in replay on every related screen. Time and source identity are always explicit; avoid unsupported '0' figures and confidential device/network data in snapshots.

## Touch actions and navigation

- START requires explicit separate mode and clears/isolates live view counter context; never auto-enables receiver scanning.
- PAUSE/STEP/SEEK operates on bounded stored packet/event list, not real USB/SD device registers.
- RETURN TO LIVE empties replay cache/banners only after live source is rechecked; no synthetic packets remain in live aircraft list.

The screen is a client of lifecycle/worker interfaces; global header controls and overlays have priority. No in-page action may change C6 firmware, silently retune the RTL-SDR, reboot a device or start microphone/video recording. Every slow action paints pending feedback before work.

## No source / paused / stale / partial failure

- No fixtures -> NO TEST CAPTURES; no generated fake drones just to fill a screen.
- Bad schema/corrupt capture -> INVALID CAPTURE with reason; refuse silently partial payloads.
- Source unplug while replay -> TEST DATA STILL PLAYING (if independent) plus local live source OFFLINE, never fabricate regained RF.
- Replay ended -> TEST DATA ENDED, not source SCANNING.

Every source or service that is unsupported remains informative; never create phantom data to fill empty lists.

## Builder how-to

1. Build isolated fixture_replay adapter that implements same versioned event snapshot interface but cannot masquerade as a physical source.
2. Validate files off UI thread, bound payload and record sizes, control deterministic test clock and keep explicit separation from live data model.
3. Draw changed timeline progress/counter widgets only, maintain TEST DATA banner across every screen and export.

One `screens::Id::drone_watch` framebuffer owner for all 25 screens, and common category/page router. Model/worker owns service interaction and source/progress snapshots; UI draws no network/SD/USB callbacks. Paint structural chrome once and dirty rectangles for changing values; never clear the whole page on simple progress updates or idle ticks.

### Adaptive 720 × 720

720-square: two-step selector and replay controller with PREV/NEXT large buttons and persistent high-contrast TEST DATA. Use the same ViewSnapshot and measure actual font/hit bounds.

## Screen-specific acceptance criteria

- Imported fixture events cannot change live source counters or C6 connected state.
- TEST DATA persists in MAP pins, timeline rows, alert test notifications, media labels and any export.
- Returning to live shows SOURCE UNAVAILABLE if actual receiver is absent; 100 replay ticks do not clear entire framebuffer.

Add shared 1280×720/720×720 geometry tests, invalid/absent/replay tests, global Home/Settings/volume overlay priority, 100 changing-value ticks with unchanged full-screen-clear count, no inactive display writes, and actual physical Tab5/ESP-IDF/USB regression results before declaring hardware acceptance. The document alone does not implement the screen.
