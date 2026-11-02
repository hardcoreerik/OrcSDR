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

r15 handoff checkpoint: full app build and P4 write hash verification passed.
The authenticated `tools/test_p4_recovery_access.py` hardware test reports PASS:
Hosted teardown acknowledged access=2, then resume returned access=0 and the
Wi-Fi driver restarted. RTL-SDR stayed connected/streaming at 96.1 MHz near
2.4 MS/s before, during and after. No ROM connection or C6 write was attempted.
The loader now deinitializes only its SDMMC slot, preserving the other slot's
host ownership. Concurrent mounted-card/ROM operations still require hardware
validation. The eventual worker must retain ownership through loader cleanup,
and enforce its own busy guard against the diagnostic resume command.

r14 package checkpoint: the complete source-built C6 package is embedded in the
P4 app after explicit manifest-digest/provenance/binary validation. Full build
and P4 write hash verification passed. Serial reports `RTL_C6_ROM_STATUS
embedded=1 segments=4 bytes=1167104 flash_size=4194304 write_enabled=0`.
Home self-check still passes; RTL-SDR streams near 2.4 MS/s and free PSRAM is
about 29 MB. C6 firmware remains Hosted 2.12.9. No C6 write was attempted; the
on-device ROM update worker/BOOT interaction is still required. Five package
validator tests and the C6 policy test pass. Generated images stay in ignored
build/cache paths, not repository source or device snapshot bundles.

r13 follow-up: full application built with the manifest-pinned ROM-SDIO flasher
dependency and backend, then P4 app write hash verification passed. Sampled boot
reports Home self-check PASS and RTL-SDR streaming near 2.4 MS/s with about
29 MB free PSRAM. No C6 write occurred. The shared Settings Display & Audio
and Firmware layouts compile but remain unselected by the P4 adapter until the
remaining compact sections/editors are ready. Layout contracts (Home/FM/Settings),
catalog invalidation, C6 policy and orientation policy pass; actual Settings
renderer/progress acceptance and ROM-SDIO worker/BOOT flow remain required.

r11 follow-up: full app build and P4 write hash verification passed. On-device
Home self-check reports legacy=1, landscape=1, portrait=1, restored=1 and PASS.
Native PNGs were captured and hash-verified at 568x1232 and 1232x568. Radio
streaming remained near 2.4 MS/s through manual rotation to 90 degrees. Sensor
samples are present; axes remain unverified and automatic rotation stays off.
Image review found portrait Contrast text overlap and landscape rail label
clipping; follow-up source fixes pass the seven shared layout tests but are not
yet flashed. Full FM state preservation, physical r11 appearance and the other
dashboard/workflow acceptance gates remain open. This supersedes the r8 Home
self-check failure below; r8 remains the user's stable-screen/audio checkpoint.

r12 follow-up: shared label corrections built and flashed with hash verification.
Authenticated serial actions opened FM's Spectrum view, rotated to portrait,
observed `RTL_DISPLAY_SCREEN_RESIZED screen=FM` and `RTL_UI_STATUS screen=fm`,
and captured hash-verified native PNGs before/after. Both images retain the
selected Spectrum tab. Receiver streaming continued near 0.96 MS/s at 96.1 MHz.
The script returned to Home afterward. This proves the sampled FM resize route,
not all FM views, every modal, physical audio continuity or other dashboards.

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

## r20 readability and capture-free timing

Built and app-only flashed successfully; esptool verified the data hash. Home
stepper labels/values now use size2 on LilyGO portrait and landscape; landscape
allocates more width to Span/Tune/Step and less to arrow-free Filter/Gain.
`python tests/test_p4_layout_contracts.py` passes 4 contracts. The native portrait
PNG shows larger text; physical readability feedback is pending.

A 25-second observation with no PNG capture confirms a periodic presentation
cost: ordinary frames repaint386rows in about66ms; status/footer frames repaint
925rows, with Home frame intervals p50~84ms/p95~176ms. RTL-SDR remained near
2.4MSps with zero reported drops. Existing dirty tracking unions all changed
rows into one bounding range, including unchanged space between plot and
footer. A bounded disjoint-row tracker is being implemented/tested before
presenter integration. No performance fix is claimed yet.

## r21 disjoint redraw hardware checkpoint

The shared ScaledDisplay now tracks up to16 sorted dirty-row intervals rather
than a single bounding span; overflow conservatively redraws extra rows. The
P4 presenter converts every interval before one flip and mirrors each changed
native rectangle into the other framebuffer. This adds approximately128bytes
of fixed tracker storage on the 32-bit target, with no heap allocation.
`python tests/test_dirty_rows.py -v` passes5 tests, and
`python tests/test_present_recording.py -v` passes3 tests using actual presenter
and geometry with recording board/display stubs. Grok reviews remain pending.

Full app build and app-only flash passed, including esptool hash verification.
A capture-free40-second observation recorded typical intervals82–84ms and
p95~83–88ms after startup tracing ended, versus baseline~84ms/p95~176ms.
Ordinary presents copied376rows in~65ms and a combined plot/status update
copied412rows in~71ms. RTL-SDR remained~2.4MSps with zero reported drops.
The user confirms the waterfall is more consistent. This validates reduced
periodic stutter on this portrait setup, not a30fps claim or other dashboards.
Larger-font physical acceptance, landscape performance and long-run tests
remain separate gates.

## Settings safe-area migration enabled

The P4 adapter now calls the shared Settings viewport API with the selected
profile safe rectangle before entry and on rotation. All9sections are exposed
through shared previous/next section navigation; compact editors consume the
existing shared text editor below the header. Location uses3pages and DataMaps
uses an overview plus one page per nonempty catalog entry, preserving original
sparse catalog slot identity and removal confirmation. A modal holds rotation
until exit. Display settings route manual/auto preferences through the existing
orientation coordinator; auto remains unavailable until axes are verified.

Five actual-renderer/input Connectivity/form tests pass, including every section
and page inside both LilyGO safe areas, masked SSID/password flow, coordinate
validation and catalog identity. Five clock recording tests,4layout contracts,
and separate Radio/Companion and System geometry tests pass. Full P4 build and
flash passed; esptool verified the image hash. Native captures cover all9first
pages, DisplayAudio landscape, and the portrait WiFi keyboard. Rotation reported
PASS and retained screen=settings. Returned to portrait DisplayAudio. Receiver
remained at96.1MHz, streaming~0.96MSps while Settings was active (existing policy).
The keyboard PNG decoded back to RGB565 matched the raw sprite SHA-256 exactly.
Physical fit feedback and scoped Grok review are pending. Service actions that
the P4 adapter does not yet implement remain separate integration limitations;
this checkpoint establishes layout coverage, not full service parity.

Physical acceptance: user confirms Settings now fits comfortably in portrait,
with readable values and reachable controls. This closes the portrait-fit gate
for the tested Settings build; service parity and all workflow acceptance remain
separate.

## Settings navigation spacing

Current native capture showed title fragments immediately below section
navigation. Compact System/clock title anchors placed size3 glyphs above the
content owner; page clearing left their upper pixels. Content now begins16px
lower, titles use their rectangle centers, and structural page clearing includes
the gap below navigation. A recording regression checks both title bounds.
Six clock recording tests,5form/editor tests,4layout contracts, and5+5C6worker
checks passed. Full build and app-only flash passed with hash verification.
Native captures of System followed by Accessories confirm the stray blue pixels
are gone and the extra spacing is visible. Left Accessories open for the user.

Settings-fit review also identified existing P4 backend gaps: map picker/network
location lookup are unimplemented; IP-result confirmation needs persistence
routing. These remain required before service-parity acceptance. The loaded-fix
flag objection lacked enter() context: enter initializes both coordinate flags
from location_configured. Partial unconfigured-coordinate persistence still
needs a dedicated backend check; layout tests do not establish persistence.
