# SCAN screen: hardware acceptance record

Device: M5Stack Tab5 on COM17, firmware built from this branch (ESP-IDF 5.5.4, app-only flash, settings kept). Antenna: the outdoor antenna, V4L dongle, no LNA, bias tee off.
Date: 2026-11-23. Script: `scan_device_session.py` (real touch handlers via `RTL_UI TAP`, PNGs via `tools/tab5_png_screenshot.py`, counters via `RTL_DV STATUS`/`RTL_MEM STATUS`).
Raw log: `device_session.jsonl`. Screenshots: `png/` (physical Tab5 framebuffer, PNG tool, **not** host-generated).

## What was exercised on the device
| Step | Screenshot | Result |
|---|---|---|
| Dashboard open, saved list | `01_scan_ready.png` | READY, plan line, buttons enabled per `action_state()` |
| `PLAN >` tapped | `02_plan_2m_amateur_selected.png` | plan changed to the first built-in range |
| START tapped on a range | `03_range_scanning.png` | FREQUENCY RANGE, `CH 12 / 320`, progress bar, estimated pass time |
| `RTL_DV PLAN` four Terra channels, START tapped | `04_..._ready.png`, `05_channel_list_scanning.png` | scanner held on real activity: DMR CC13 integrity-validated at 152.1575 MHz, discovered-signal row, ladder lit to INTEGRITY, `SAVE: ALREADY SAVED` reason |
| HOLD tapped | `06_hold.png` | HOLD banner |
| PAUSE (serial) | `07_paused.png` | PAUSED banner |
| RESUME, then STOP | `08_stopped_after_scan.png` | back to READY |

## Measurements (from the firmware's own counters during ~3.5 min of scanning with the UI updating)
- **Render cost** (time in `Surface::update` when something was drawn, UI task; final firmware): average about 1 ms per repaint while scanning (0.84 to 2.9 ms across the session), slowest single repaint 40.7 ms (once, as the range scan started). 1,247 repaints. A first run on the previous firmware gave 0.74 to 2.0 ms average and 21 ms maximum.
- **IQ continuity:** `loss` 0, `gaps` 0, `ring_drops` 0 throughout. DSP block time unchanged at 14.0 to 14.1 ms (earlier baseline 14.2 ms); decode worker 24 to 33 % of its core.
- **Memory:** PSRAM free 21.60 MB idle, 21.28 MB scanning. Internal RAM free 54.7 KB with the receiver stopped, 36.7 KB scanning (the receiver, not the screen, uses it; the earlier measured figure with the receiver running was 35.7 KB).
- Not measured: task stack high-water mark of the UI task, display transfer time, frame timing. No claim is made about them.

## Still outstanding (needs a person looking at the panel)
1. **Physical flicker acceptance.** The host tests prove which rectangles are drawn; only the panel shows whether anything still flickers. Watch SCAN while scanning at least one full pass with live activity, in both densities (SETUP toggles the density), and while the shared volume tray opens and closes.
2. Analysis-first density: exercised on the device once (`png/09_analysis_first_scan.png`, abbreviated evidence chips fit and nothing overlaps); a full scan pass in that density has not been watched.
3. A long soak (hours) with the screen on, comparing `loss`/`ring_drops` before and after.

## Update after the text-size and tag fixes (final firmware, 2026-11-23 22:51)
- Ladder chips and counts now use the normal text size (full words in discovery density, `CAND/SYNC/INTEG/IDENT/REC` in analysis density); NEW/SAVED tags moved to the first row line so they cannot overlap the last-seen time.
- Re-run of `scan_device_session.py`: render about 1.0 to 1.3 ms average per repaint (max 30 ms, once), `loss` 0, `gaps` 0, `ring_drops` 0 across ~4 minutes of scanning; the scanner again held on live DMR at 152.1575 MHz and discovered 153.068 and 152.685 MHz.
- Physical flicker acceptance is still outstanding (a person has to watch the panel).
