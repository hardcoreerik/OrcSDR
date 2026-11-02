# Shared dashboard requirements

An OrcSDR dashboard has one implementation across board profiles. Hardware
selection belongs in the board adapter; screen layout belongs in shared UI
code. A profile-specific copy of a dashboard is not a supported porting method.

## Geometry and state

- Use the selected display geometry's view size and safe rectangle. Physical
  insets rotate with the panel; do not apply a second touch rotation in a page.
- Compute named rectangles once and use them for paint, touch, keyboard focus,
  plots and overlays. Fonts retain their aspect ratio. Keep controls at least
  44 pixels in each dimension; reflow or paginate content rather than hiding it.
- Expose a UI-owner viewport transition, following Home and FM's opt-in APIs.
  Preserve tuning, selected subview, entered text and modal state. Invalidate
  pixel caches whose dimensions changed; avoid resetting receiver services.
- Keep legacy Tab5 geometry as the default until its migration is explicitly
  accepted. A successful LilyGO test does not establish Tab5/Hosyond acceptance.
- Raw touch is calibrated into native pixels, then transformed by the inverse
  presentation geometry. Invalid samples cancel a gesture instead of clamping
  onto an unrelated control.

## Rendering and services

Follow `docs/UI_RENDERING_STANDARD.md`: only the UI owner draws. Workers publish
state, and ordinary value/progress updates repaint bounded widgets without
clearing static chrome or the last valid map. Give immediate button feedback
before slow work. Header/keypad overlays retain drawing and input ownership.

Get capabilities from the selected hardware/service, including the attached
receiver. Do not equate an unavailable sensor or coprocessor with an unavailable
whole application. New user actions need named serial controls with the same
validation, persistence and authentication as their UI path.

## Validation matrix

Use the same renderer and input implementation at these representative views:

| Target | View or safe rectangle |
| --- | --- |
| Tab5 legacy | 1280 x 720 at (0, 0) |
| Hosyond landscape | 800 x 480 at (0, 0) |
| LilyGO portrait | 424 x 1088 at (72, 72) in a 568 x 1232 view |
| LilyGO landscape | 1088 x 424 at (72, 72) in a 1232 x 568 view |

LilyGO's current insets are conservative. Physical edge/corner validation may
refine the profile without changing dashboard source.

For each screen, verify all controls fit, hit targets and focus use the painted
rectangles, and rotation/exit/re-entry preserve state. Recording-display tests
must show changing values and repeated animation ticks without increasing the
full-screen-clear count. Check plot-buffer bounds and record PSRAM/DMA cost.

Existing narrow checks run as `python tests/test_display_geometry.py`,
`python tests/test_gt9895_protocol.py`, `python tests/test_freq_keypad_layout.py`
and `python tests/test_orientation_policy.py`. Home/FM geometry executables are
in `apps/orcsdr-tab5/tests`; they do not replace renderer or hardware tests.

On hardware, capture native PNGs with their transfer hash and firmware identity,
then separately record physical readability, touch, flicker/tearing and sustained
receiver/audio continuity. Track each screen/workflow in `p4-ui-parity.csv`;
missing evidence stays unverified.

## Current migration limits

Home, header, keypad and FM expose shared geometry. Home's on-device self-check
passes all three tested viewports; FM rendering/state acceptance remains open.
The screen router now offers a
state-preserving geometry callback, wired for FM; other dashboards still return
to Home on rotation. Preserving every active dashboard remains required.
Remaining dashboards and text/setup/map workflows need migration and
validation. These requirements describe the completion gate, not achieved parity.

Settings now has a pure shared layout and a viewport/page seam. Display & Audio
rows use the same geometry for painting and actions, including optional board
orientation policy fields. Firmware & Updates now uses two compact pages for
status/progress and instructions/actions, with progress repaint restricted to
the status region. Its current actions preserve Hosted OTA; the ROM-SDIO
worker/physical BOOT interaction is not wired yet. Other compact sections and editors remain
unconverted. The P4 adapter deliberately does not opt in yet; no feature should
be hidden to claim responsive completeness. Host geometry checks cover legacy
positions, both LilyGO safe rectangles, Hosyond and a portrait reference. Actual
Settings renderer, progress/overlay and physical acceptance remain separate gates.

Run `python tests/test_p4_layout_contracts.py` for production Home, FM and
Settings geometry across representative viewports. This compiles and executes
the C++ contracts, retaining legacy positions and checking bounds/minimum sizes.
It does not substitute for actual renderer/input or hardware tests.
