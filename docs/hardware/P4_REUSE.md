# P4 display integration: reuse and acceptance ledger

Approved direction: one OrcSDR UI, reusable board and display/input profiles,
portrait and landscape, M5GFX retained. Mainline base `0b60a1664abeac93d0409e854a16374241a829fb`.

## Sources preserved

- Existing general/Hosyond board and M5GFX adapters imported from
  `claude/p4-board-support`, `a8dc860b8191592e67ce7349c32ba42668c30ae2`.
- LilyGO committed board facts/header from `8a6ded0253ed6af3daa72234deef6ef672bc30ac`.
- The existing uncommitted `lilygo_amoled.c/.h` implementation was copied from
  `claude/lilygo-t-display-p4-test-8cab12` before integration; source hashes and
  origins are in [p4-import-provenance.json](p4-import-provenance.json).
- Both original worktrees remain untouched. Their complete dirty source/patches
  were archived separately before import. Unfinished Ethernet app changes remain
  in their original worktree and preservation archive; they are not prerequisites
  for this display/input milestone.
- The newer Waveshare P4-ETH/C5 profile entries and target checks remain in the
  imported selector. New optional profile fields are explicitly absent on those
  boards; zero is not used as an absent GPIO/expander line.

The adapters live under `ports/p4/components`, retaining their existing component
names. This keeps the M5Unified shim out of the native Tab5 app's component search
path. The only new root component is portable display geometry, with no driver,
framebuffer, startup or M5 dependency. No screen implementation was forked or
replaced. No LVGL renderer was imported.

## Third-party touch protocol

GT9895 runtime discovery and packet handling in `gt9895_touch.c` and
`gt9895_protocol.h` are adapted from LakeShark v2.19.2, commit
`08578075c83c0d8da87c791e197cbd58a0aa4826`,
`components/lakeshark/board/ls_touch.c` and the touch-reset sequence in
`ls_board_hw.c`, by SAMS0N1TE/LakeShark. Its source credits LILYGO_L
`cpp_bus_driver/gt9895.cpp`. These files retain GPL-3.0-or-later SPDX notices;
the supplied GPL text is in `ports/p4/licenses/LakeShark-GPL-3.0.txt`. Preserve
these notices/source when distributing a derivative. The surrounding OrcSDR
repository retains its existing license.

The original Claude AMOLED source retains its existing provenance commentary.
It was not relabelled as a newly written panel driver. New geometry and its
touch mapping are separate from the controller protocol adaptation.

## Implemented in this milestone

- A shared C geometry contract for all four quarter-turn rotations, inverse
  touch calibration, physical safe insets and clipped dirty rectangles.
- One board-level native-pixel touch polling interface; GT9895 no longer needs
  the application to call an FT5x06 API or maintain its own rotation formula.
- Native dimensions and software view rotation are separate. The old fixed
  LilyGO-only `rotate_cw/ui_width/ui_height` fields are replaced by geometry.
- A small M5GFX-based screen/touch diagnostic with visible and serial rotation
  controls routed through the same UI owner queue. Touch footer updates are
  bounded; rotation is a deliberate layout repaint. It is not a radio app and
  not a replacement dashboard.
- `p4-ui-parity.csv` records all current registry entries, Home and shared
  workflows; no landscape/portrait dashboard acceptance is marked passed.

## Remaining acceptance gates

1. Build the imported driver and diagnostic with a clean current-main toolchain.
2. Physical AMOLED colors/geometry and GT9895 corners/center/release behavior.
3. Both orientations and inverse mapping on the glass; opposite rotations use
   the same contract. The diagnostic uses conservative rounded-corner insets.
4. Make the existing M5GFX scaling/font/layout bridge responsive. The imported
   legacy scaling layer still assumes a design canvas; geometry tests do not
   claim that shared dashboard layout migration is finished.
5. Adapt shared Home/FM/keypad/settings layouts and review actual renderings.
6. Complete the parity ledger against current main, connect current services,
   and prove radio/audio/Wi-Fi and bounded rendering under load.
7. Production rotation persistence, named serial/UI parity, scanout/tearing
   measurements and cross-device regressions. The probe intentionally does not
   write NVS or pretend to implement production settings.

Hardware checkpoint: after removing post-video DCS reads (which IDF switches
to command mode without restoring video), the user confirmed a visible stable
diagnostic. Serial recorded touch-driven successful transitions through all
four rotations. This establishes the sampled display/rotation/button path,
not exhaustive edge calibration, flicker-under-radio-load or finished UI parity.

Additional approved Settings requirement: profile-aware ROM-SDIO C6 update
with explicit hold/release C6 BOOT instructions; see [P4_C6_UPDATE_FLOW.md](P4_C6_UPDATE_FLOW.md).

GT9895 initially accepts one contact only. Unsupported multi-contact packets
cancel a gesture; they are not silently treated as a second supported feature.
Static physical calibration endpoints are vendor-derived and require actual
corner/edge checks. No controller firmware is uploaded.

## Focused host checks

From this worktree on the current Windows toolchain:

```
python -m unittest discover -s tests -p test_display_geometry.py -v
python -m unittest discover -s tests -p test_gt9895_protocol.py -v
```

Eight geometry tests and three packet/discovery tests pass. They compile and
exercise production C helpers through a native DLL. They do not prove a panel
lights, touch calibration is correct on hardware, or a dashboard is usable.

The full OrcSDR application is now being integrated from the existing P4 shell
and current shared dashboard sources. Source-built C6 recovery artifacts must
be used for ROM-SDIO updates; private device snapshots must never enter packages.
LakeShark restoration is not an acceptance gate for this OrcSDR port.

## Full application integration checkpoint

Latest hardware checkpoint (r8): the full app built and flashed with data hash
verification. The user confirmed a stable screen and audible FM. Serial showed
RTL-SDR connected/streaming near 2.4 MS/s and Wi-Fi driver startup with C6 Hosted
2.12.9. The P4 host is 3.0.6; no C6 write occurred. The previous GPIO reset
assertion did not recur in the sampled boot after the LilyGO reset adapter.
This is a work-in-progress checkpoint: Home's startup self-check still reports
FAIL, native PNG captures remain pending, all-dashboard layout/behavior parity
is not established, and auto-rotation calibration/Settings and ROM-SDIO Settings
integration remain incomplete. The FM migration compiled but has not received
complete focused or physical validation. Do not treat this branch as a release.

Physical observation for the flashed r6 application: the user saw the real
OrcSDR UI in portrait and heard FM radio audio. This confirms sampled portrait
rendering and the RTL-SDR/FM/audio path on this board. It does not establish
which dashboard was open, full control/layout acceptance, sustained reception,
landscape operation, or Wi-Fi stability. The captured boot log separately
contains the ESP-Hosted reset assertion; both observations remain valid.

- Shared Home now opts into safe viewport layout for controls, plots, dashboard
  browser, footer and popups. Tab5 retains its legacy default layout.
- Header and frequency keypad use shared drawing/input/focus geometry. The
  user accepted the diagnostic keypad in both orientations and the revised
  battery placement after removing VIS. Full application acceptance is pending.
- Focused host checks pass: display geometry (8), GT9895 protocol (3), shared
  keypad/header/focus/layout (7), orientation policy (5), C6 update bounds (1).
  Commands are `python tests/test_<name>.py`, with names `display_geometry`,
  `gt9895_protocol`, `freq_keypad_layout`, `orientation_policy`, `c6_rom_update`.
- The first full app link exposed missing current map/VDL2 dependencies in the
  old shell. Real service sources were added; the next compile identified map
  graphics adapter, JSON and C++ exception configuration gaps being resolved.
- Automatic rotation remains unavailable until physical sensor axes are
  verified. The C6 backend is not yet connected to Settings or hardware-tested
  as part of this application. Other dashboards still need layout migration
  and service/physical validation recorded in the parity ledger.
