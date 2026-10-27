# S05 — Diagnostics

> **Category:** SYSTEM · **Dependency:** P3 basic source status; P2 C6 qualification; P9 production trace gates · **Status:** planned; capabilities and integrations below are not automatically present in OrcSDR.  
> Read [mandatory UI rules](../../UI_UX_STANDARD.md), [navigation contract](../../NAVIGATION.md), [data validity](../../DATA_CONTRACTS.md), [draw/test workflow](../../RENDERING_AND_ACCEPTANCE.md).

## Function and user benefit

Give advanced users and developers a trustworthy technical health view for the exact installed image, receiver versions, resource limits, packet/drop counters and rendering load without exposing Wi-Fi secrets or trying to fix hardware automatically.

## How to use it

1. Open SYSTEM -> DIAGNOSTICS or from a source error; see READ-ONLY status first.
2. Choose a source to inspect supported transport, last event, firmware, adapter error reason and actual diagnostic counters.
3. Tap COPY SUMMARY/EXPORT DEBUG only when a safe authenticated/opt-in redacted export exists; run guided self-checks only with no unintended radio interruption.

## 1280 × 720 display layout

Reference shell: shared OrcSDR header y0–113 owned by `dashboard_audio_control`; 52–60px in-category selector y118–178; content x20–1260/y188–634; bottom five-category tabs y649–705. The following rectangles are **suggested layout zones**, not duplicate hard-coded offsets per page.

- Left x20–800: health sections RECEIVER / C6 HOSTED / UI / STORAGE; each card with ACTIVE/PAUSED/FAILED and actual counter values.
- Right x820–1260: selected exact version/commit, source interface, last measured incident, bounded log detail and VIEW SOURCE action.
- Footer content: RECHECK STATUS and HELP; no factory reset, secret readout or forced firmware update.

Keep the selected state and any saving/error banner legible on the real 5-inch Tab5; 52px minimum touch heights and descriptive button text. OrcSDR palette: black, dark panel, cyan focus, green measured-active, amber paused/uncertain, muted unavailable. All generic system pages must still clearly say what scanning is actually doing.

## Truthful fields and service dependencies

- OrcSDR app git SHA/build image, native ESP-IDF version, driver pin, C6 Host/coproc version, connected board/dongle models if known.
- USB overrun/application IQ drops, event queue fill/dropped, source packet parsing errors, invalid field counts and actual listening gaps.
- Memory: free internal DMA/PSRAM floor where instrumentation exists, UI repaint count/full-screen-clear count, storage/mount error and map worker state.

No mere presence of ESP32-C6 or an RTL-SDR dongle establishes Remote ID capability. No `SCANNING` without actual verified source callback health. `TEST DATA` must remain visible in replay on every related screen. Time and source identity are always explicit; avoid unsupported '0' figures and confidential device/network data in snapshots.

## Touch actions and navigation

- Source section focus changes local inspector only; RECHECK request asynchronous source-specific probe without resetting C6.
- COPY SUMMARY redacts network credentials, exact operator/home positions, IDs as configured; default no external upload.
- OPEN SOURCES -> S01; OPEN SESSION -> S03; no hidden reboot/reflash and no new tuner ownership.

The screen is a client of lifecycle/worker interfaces; global header controls and overlays have priority. No in-page action may change C6 firmware, silently retune the RTL-SDR, reboot a device or start microphone/video recording. Every slow action paints pending feedback before work.

## No source / paused / stale / partial failure

- No counter instrumented -> NOT MEASURED, not 0 drops.
- Coproc mismatch -> FIRMWARE MISMATCH, explain instead of claiming Bluetooth ready.
- No SD or network -> show separate errors without converting unrelated sources to FAILED.
- Diagnostic sample stale -> AGE label and RECHECK; disabled export explains why.

Every source or service that is unsupported remains informative; never create phantom data to fill empty lists.

## Builder how-to

1. Create read-only aggregated diagnostic snapshot from currently instrumented services and validated version strings; never peek raw private credentials.
2. Bound console line length/scroll, include units and timestamp per counter, and encode no dynamic user text as printf format.
3. Render sections once; only changed numeric/health widgets repainted. Keep diagnostic polling low priority and protect USB IQ callbacks.

One `screens::Id::drone_watch` framebuffer owner for all 25 screens, and common category/page router. Model/worker owns service interaction and source/progress snapshots; UI draws no network/SD/USB callbacks. Paint structural chrome once and dirty rectangles for changing values; never clear the whole page on simple progress updates or idle ticks.

### Adaptive 720 × 720

720-square uses paged RECEIVER/C6/UI/STORAGE diagnostics and an explicit SOURCE DETAIL button; no microscopic simultaneous metrics. Use the same ViewSnapshot and measure actual font/hit bounds.

## Screen-specific acceptance criteria

- Injected missing metrics must show NOT MEASURED and never fabricated zero.
- No secret/password/operator coordinate appears in screenshot, copy summary or log by default.
- Source reset and SD failure don't cause screen seizure; 100 counter updates preserve header/footer and full-screen-clear count.

Add shared 1280×720/720×720 geometry tests, invalid/absent/replay tests, global Home/Settings/volume overlay priority, 100 changing-value ticks with unchanged full-screen-clear count, no inactive display writes, and actual physical Tab5/ESP-IDF/USB regression results before declaring hardware acceptance. The document alone does not implement the screen.
