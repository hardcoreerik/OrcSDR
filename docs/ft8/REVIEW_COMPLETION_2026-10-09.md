# FT8 / FT4 / JS8 review completion

Scope: the supplied Gemini and Grok reports, Claude's unfinished changes on `claude/js8-decode`, and the useful work unique to PR #172. Original worktree scripts and the separately committed raw-IQ corpus are preserved.

## Findings

| Finding | Classification | Resolution |
|---|---|---|
| Last half-symbol start and exact 157/158-row JS8 capture excluded | FIX BEFORE MERGE | Match sync's 157-row span; include equality. Tests recover all 79 tones at row 29 and at row zero in exact-length audio. |
| Ninth JS8 message dropped by runtime's eight-element array | FIX BEFORE MERGE | Live and injection paths use the same 24-output capacity as FT8/FT4. A clean nine-message synthetic slot returns all nine from the real backend. |
| Interrupted header and padding writes damage journal alignment | FIX BEFORE MERGE | Recompute append prefix from actual file size. Fault tests cover all 15 partial header lengths and all 39 partial record lengths with repeated short writes, followed by successful replay. Foreign header prefixes are protected. |
| Station band/dial assigned at pending-result drain | FIX BEFORE MERGE | Audio continuity latches atomic RF context; decoder freezes it before processing. A retune during copying invalidates the slot; one during decoding cannot restamp its results. Callback no longer reads or mutates UI tuning globals. |
| OrcDial advertises no decoder and refuses hunts / item activation | FIX BEFORE MERGE | Advertise actual runtime and FT8-only hunt capabilities; route start/stop/best and station-history activation. |
| Fine-tuning hints overwritten or disagree with touch region | FIX BEFORE MERGE | Live fine mode keeps its step/tap hints and accepts a tap on the displayed band hint. Hunter uses band selection without overlapping fine hints. |
| Custom dial invisible at 10 Hz steps | FIX BEFORE MERGE | Header repaints on dial changes; header and Live chip show six decimal MHz digits. |
| Rejected tuning request becomes subsequent step origin | FIX BEFORE MERGE | Steps start at the accepted snapshot dial; application restores its override on queue rejection and does not log success. |
| OrcDial detents lost while command is pending; 13–20 rejected | FIX BEFORE MERGE | Coalesce tune/band/item actions by kind; send bounded FT8 batches and accept/clamp up to 20. |
| LISTEN BEST retains custom dial | FIX BEFORE MERGE | Return to the table through the same path as explicit band selection. |
| Waterfall graticule moves with data | FIX BEFORE MERGE | Restore underlying pixels before scrolling fixed horizontal lines; redraw the graticule after painting. |
| Gain popup AUTO/MANUAL and bar stale | FIX BEFORE MERGE | Repaint controls on gain-mode/step changes, with readout-only updates for input level. |
| FT4 Hunter cards/actions show FT8 frequency | FIX BEFORE MERGE | Resolve every card/action through the selected mode's dial table. Unverified FT4/JS8 hunt starts remain unavailable. |
| OrcDial tab change leaves green info indicator | FIX BEFORE MERGE | Repaint indicator after closing overlays. |
| FT4 clock warning describes 15 s | FIX BEFORE MERGE | Show the 7.5 s FT4 boundary. |
| Heard conditions stale after receiver location change | FIX BEFORE MERGE | Include receiver position in repaint decision. |
| Fast-hunt help says 15 s; FT4 facts say 103 symbols | FIX BEFORE MERGE | State six-second energy dwell and 105 symbols including ramps. |
| Replay drops partial reads; station history overwrites older snapshots | STALE / IGNORE | Already fixed in Claude's committed replay/merge implementation; existing chunked-read and history tests pass. |
| Mojibake degree and decode row/page overlap | STALE / IGNORE | Already fixed in current source. |
| OrcDial accepts step indices 6/7 | STALE / IGNORE | Six-step write clamp and read labels already fixed and tested. |
| JS8 reported SNR could become measured SNR | OPTIONAL | Current unavailable flag is correct; sender report remains separate from measured receiver SNR. |
| OSD should search higher orders after an accepted word | OPTIONAL | Current first-accepted-order policy is documented false-accept control and tested. Changing acceptance policy requires a separate sensitivity/false-accept experiment. |
| Unused candidate energy allocation | OPTIONAL | No allocation cleanup in this repair. |

## Verification

- `wsl bash tools/test-ft8-review.sh`: eight focused suites, optimized and ASan/UBSan builds. Includes 3,000 noise-only decoder trials per variant (zero valid frames/messages), exact/late-frame recovery, short-write replay, dashboard actions/repaints, model, tuning, information text and OrcDial controller.
- `C:\Users\hardc\.platformio\penv\Scripts\platformio.exe run -d orcdial -e dial`: OrcDial firmware build.
- ESP-IDF 5.5.4 exported, then `idf.py -B build-native-hosted3 build` in `apps/orcsdr-tab5`: Tab5 firmware build, existing pinned C6 image; no flash.
- `python tools/check_documentation_truth.py` and `git diff --check`: documentation and diff checks.

Build success is separate from hardware acceptance. No device flash, live UI acceptance or new live RF decoder claim is made for these follow-up changes. CodeRabbit skipped the large PR because of its file/credit limits; a green skipped status is not a completed independent code review.

PSK Reporter establishes band activity only. The earlier live soak is not an exact-IQ/derived-WAV/standard-decoder reference fixture, and does not establish zero false messages. Raw capture reference pairing remains the dataset's ground-truth boundary.

## PR #172 integration

Most source files are identical; differences in sync scoring, SNR, WAV tools and test runner are later improvements in #176 and must be retained. Carry forward the missing external-WAV validation workflow and research links. Fold native-decoder path coverage into the existing FT8 workflow instead of adding a duplicate full-suite job. Keep the reference and host tests on the same downloaded WAV. No stale decoder branch merge or source rollback is needed.

## Final worktree audit

Claude's parity worktree contained commit `12428c4`, a diagnostic tool not included in the original PR branches. It is now integrated through reopened PR #172, based on current main. The diagnostics and CRC-valid trace payload compile only with `ORCSDR_FT8_DIAG`; production keeps its original candidate call. Skipped/short slots clear diagnostic state, invalid CLI arguments are rejected, and JSON escapes reference strings. `wsl bash tools/test-ft8-diagnostic.sh` checks production, diagnostic and ASan/UBSan paths, accepted and unsupported CRC-valid payload traces, skipped slots, JSON and invalid arguments.

The dirty WAV detail-print change in the decoder worktree is already present on main and remains preserved in that worktree. The owner-captured JS8 corpus stays committed locally at `1705472`, following the owner's local-dataset instruction; it is not part of this firmware merge or uploaded to GitHub.
