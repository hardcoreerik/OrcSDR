# SCAN screen (Digital Voice) - behaviour, data sources, rendering contract

**Code:** `digital_voice_scan_view.{hpp,cpp}` (retained widgets), `digital_voice_dashboard_model.*` (inputs, `action_state()`), `dv_scanner.*` + `dv_bandplan.hpp` (plans),
`digital_voice_app.cpp` (fills the snapshot from the receiver/scanner/decoders). **Tests:** `tests/digital_voice_dashboard_preview/dashboard_scan_tests.cpp`
(`dv_dashboard_scan`, ctest `digital_voice_scan`). **Scenario manifest:** `artifacts/digital-voice-integration/scan/scan_scenarios.json`.

## What the screen says (top to bottom, left card)

| Widget | Shows | Source (nothing is invented) |
|---|---|---|
| Status banner | `READY / SCANNING / SYNC ACQUIRED / INTEGRITY VALIDATED / IDENTITY VERIFIED / HOLD / PAUSED / ERROR` (+ receiver state) | `ScanStage`, derived in `refresh()` from the scanner state and the decoders' own evidence |
| Frequency | the channel being listened to | scanner's current channel, or the last decoded event's frequency |
| Plan line | `CHANNEL LIST` or `FREQUENCY RANGE`, plan name, `CH i / n`, estimated pass time | scanner plan; the pass time is `channels x (settle + dwell)` and labelled `PASS ~m:ss` (an estimate) |
| Progress bar | position in the plan (not a completion percentage) | scanner index / count |
| Counts | VERIFIED, SYNC, NEW, PASSES | session ledger of channels by best evidence reached (VERIFIED = integrity or identity, SYNC = sync only); NEW = system not already in the discovery database when first validated |
| Evidence ladder | CANDIDATE, SYNC, INTEGRITY, IDENTITY, RECORDED lit up to the level reached on the current channel | decoder evidence; RECORDED = a validated signal for this frequency is in the discovered list |
| Reason line | why the most relevant control is unavailable (`SAVE: NOT VERIFIED YET`, `START: NO RECEIVER`, ...) or the standing note that sync is not identity | `action_state()` |
| Buttons | START/STOP SCAN, HOLD/RESUME, SKIP, SAVE, `< PLAN`, `PLAN >` | each enabled only when `action_state()` says so, so a disabled control draws disabled **and** does nothing when touched |

Right card: the **discovered signals** list (newest first, four rows: protocol, colour code / RAN, evidence, frequency, times seen, last time, NEW/SAVED) and a **selected signal** panel
with protocol-specific fields. DMR: CC, TS, `SRC`/`TG`/`DST` (only for identity-validated events), group/private. NXDN: RAN (RAN 0 is a value), NXDN48/96, channel type. Unknown stays
`UNKNOWN`; a candidate never inherits an identity from another event or from a directory.

## Scan plans

| Plan | Implemented | Notes |
|---|---|---|
| Saved channel list (<= 16, protocol per entry) | yes | the existing `dv_plan` setting; `RTL_DV PLAN`, or SAVE on a verified signal |
| Frequency range (start, step, count <= 4096) | yes | `dv::Scanner::set_range`; six built-in ranges (`dv_bandplan.hpp`) cycled with `< PLAN` / `PLAN >`, custom range `RTL_DV RANGE <start MHz> <end MHz> <step kHz>`; validated against the tuner limits (24 MHz..1.766 GHz) and the 6.25 kHz minimum step |
| Window sweep | **no** | needs a wideband survey stage (power across a window, then decode only the candidates). Not built; the screen never offers it. |
| RF-activity (energy) stage | **no** | there is no energy detector in the decoder chain; the evidence ladder starts at what a decoder reports |

The range raster matters: a digital voice channel must be within a few kHz of the tuned frequency. Presets start on the grid their band uses (VHF high band on the 7.5 kHz-offset grid
that the Terra channels sit on, UHF/800 MHz on the plain 12.5 kHz grid). A range is only as good as its grid; adjust with `RTL_DV RANGE` when a system uses another one.

One scanner, one receiver owner: plans change what the existing `dv::Scanner` tunes through the existing radio hooks. The renderer never tunes, never starts a scan and never writes storage;
`Surface::touch()` only returns an `Action`, which the app validates again (serial uses the same handlers: `RTL_DV SAVE`, `PRESET`, `RANGE`).

## Rendering contract (docs/UI_RENDERING_STANDARD.md)

- **Entry** paints the layout once: cards, titles, progress-bar outline, footer note, then every widget.
- **Updates** repaint a widget only when the hash of its displayed inputs changed. Each widget fills its own rectangle (opaque, so the old value is gone) and clips its text to it.
  Changed counters that are not shown on SCAN (IQ drops, sync counts, signal level) draw nothing, whatever the revision counter says.
- The shared context strip repaints only when the origin or density changed. Other pages still repaint the whole page when the revision changes; that is a known, documented gap
  (they are outside this task).
- No full-screen clear, no full-frame buffer, no drawing from workers, overlay/ownership checks unchanged.
- **Proven on the host** (recording stand-in, not pixels): repeated frequency / progress / list / selection / evidence / start-stop / hold-resume updates draw inside the intended
  rectangles only, erase before drawing, do not redraw chrome, keep the full-clear count at 0; unchanged and invisible changes draw nothing; overlay and non-ownership draw nothing and
  lose nothing; re-entry repaints once. A negative control confirms the detectors fire when a whole-page repaint is forced.
- **Not proven by host tests:** physical flicker. Hardware acceptance is recorded separately (see `artifacts/digital-voice-integration/scan/HARDWARE_ACCEPTANCE.md`).

## Discovery intelligence: what exists and what does not

Measured observations: times a channel produced activity (once per visit), first/last time (receiver clock, empty when the clock is invalid), evidence level, protocol fields,
NEW (not in the persistent discovery database when first validated), SAVED (in the saved list). Calculated: the counts above and the pass estimate. Persisted: the discovery database
(`/orcsdr/dv_discovery.txt`, systems / talkgroups / units). **Missing integration (documented, not faked):** ranking by activity frequency across sessions, per-talkgroup activity
history and user annotations need a store with more than the current 32/128/256-entry table; the discovered list shows only the newest four signals (the Systems page and the
database keep the rest); sync-level entries carry no protocol when the plan rotates decoders and none of them claimed the sync.

## Screenshots

Physical captures use the existing PNG tool (`tools/tab5_png_screenshot.py`, firmware `FT8 SNAP`), never documentation mode. Host scenario hashes are hashes of the recorded
draw-call stream, not of pixels. See the scenario manifest for which is which.
