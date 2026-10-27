# S03 — Session Logbook

> **Category:** SYSTEM · **Dependency:** P5 opt-in SD journaling and import/export · **Status:** planned; capabilities and integrations below are not automatically present in OrcSDR.  
> Read [mandatory UI rules](../../UI_UX_STANDARD.md), [navigation contract](../../NAVIGATION.md), [data validity](../../DATA_CONTRACTS.md), [draw/test workflow](../../RENDERING_AND_ACCEPTANCE.md).

## Function and user benefit

Offer a deliberate, auditable record of Drone Watch listening sessions—not silent surveillance or a claim of perfect monitoring. Every session states what data was recorded and whether gaps, storage errors or redactions occurred.

## How to use it

1. Open SYSTEM -> SESSION LOGBOOK; by default see NO SAVED SESSIONS until user enables opt-in recording.
2. Tap START SESSION RECORDING to create a versioned journal if SD is ready; LIVE session shows SAVING, SAVED, or FAILED and dropped counts.
3. Tap a saved session for manifest, source list, counters, gaps and verification; CLEAR/DELETE require confirmation.

## 1280 × 720 display layout

Reference shell: shared OrcSDR header y0–113 owned by `dashboard_audio_control`; 52–60px in-category selector y118–178; content x20–1260/y188–634; bottom five-category tabs y649–705. The following rectangles are **suggested layout zones**, not duplicate hard-coded offsets per page.

- x20–825: session list with relative/verified UTC timestamp, duration, source methods and save integrity status.
- x845–1260: selected session details with event count, dropped count, file hash/size, schema version and privacy policy.
- Big actions START/STOP SAVE, VERIFY, VIEW, DELETE with disabled reasons; no raw pilot/operator coordinate column by default.

Keep the selected state and any saving/error banner legible on the real 5-inch Tab5; 52px minimum touch heights and descriptive button text. OrcSDR palette: black, dark panel, cyan focus, green measured-active, amber paused/uncertain, muted unavailable. All generic system pages must still clearly say what scanning is actually doing.

## Truthful fields and service dependencies

- Session UUID/key, monotonic start/end, optional UTC, source identities, schema version, journal path and verified durable record state.
- Event counts vs lost/dropped, source pause intervals, file size/hash, storage errors, recovery status.
- Opt-in consent, retention/redaction and export/share options separated from device receive setting.

No mere presence of ESP32-C6 or an RTL-SDR dongle establishes Remote ID capability. No `SCANNING` without actual verified source callback health. `TEST DATA` must remain visible in replay on every related screen. Time and source identity are always explicit; avoid unsupported '0' figures and confidential device/network data in snapshots.

## Touch actions and navigation

- START visibly starts pending, worker opens new file and acknowledges; STOP flushes/syncs before reporting SAVED.
- VERIFY asynchronously checks SHA and complete JSONL sequence; VIEW EVENTS -> W02 replay/filter, clearly not LIVE.
- DELETE and EXPORT require deliberate action and confirmation; no automatic cloud upload.

The screen is a client of lifecycle/worker interfaces; global header controls and overlays have priority. No in-page action may change C6 firmware, silently retune the RTL-SDR, reboot a device or start microphone/video recording. Every slow action paints pending feedback before work.

## No source / paused / stale / partial failure

- No SD -> SD UNAVAILABLE; browsing RAM current-session still possible if recorded.
- Card removed mid-write -> RECORDING FAILED, partial file poisoned and never claimed saved.
- Unknown wall clock -> session relative time + CLOCK UNSYNCHRONIZED; raw bytes/coordinates absent when not consented.
- Corrupted/incomplete JSONL -> PARTIAL RECOVERY and file integrity warning.

Every source or service that is unsupported remains informative; never create phantom data to fill empty lists.

## Builder how-to

1. Use low-priority dedicated storage worker and bounded queue akin to Dead Air, but audit shared SD mount/locking/unmount constraints before reuse.
2. Implement versioned JSONL manifest, CRC/SHA verification, atomic session start/stop and explicit loss counters.
3. Render changing save/progress status widget only, not full list; UI posts async command and returns.

One `screens::Id::drone_watch` framebuffer owner for all 25 screens, and common category/page router. Model/worker owns service interaction and source/progress snapshots; UI draws no network/SD/USB callbacks. Paint structural chrome once and dirty rectangles for changing values; never clear the whole page on simple progress updates or idle ticks.

### Adaptive 720 × 720

720-square: session list then separate detail page with vertically stacked RECORD, VERIFY and DELETE buttons. Use the same ViewSnapshot and measure actual font/hit bounds.

## Screen-specific acceptance criteria

- Power loss mid-file yields clear partial-file recovery, never appends to an invalid tail silently.
- SD-full/reinsert/failed hash state preserved and distinctly labeled; opt-out writes zero records.
- Viewing/editing session while FM/ADS-B active doesn't call storage from UI/USB task and keeps DSP drop counters within accepted baseline.

Add shared 1280×720/720×720 geometry tests, invalid/absent/replay tests, global Home/Settings/volume overlay priority, 100 changing-value ticks with unchanged full-screen-clear count, no inactive display writes, and actual physical Tab5/ESP-IDF/USB regression results before declaring hardware acceptance. The document alone does not implement the screen.
