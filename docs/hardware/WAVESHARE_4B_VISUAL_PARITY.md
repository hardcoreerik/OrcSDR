# Waveshare 4B — OrcSDR Theme and Home Visual Fidelity Gate

## 2026-10-14 user acceptance: FAILED visual parity

The first Home IQ build compiled and appeared on hardware, but **is not an
acceptable OrcSDR UI port**. It invented its own dark-teal background, branding,
header/status line, control boxes and spectral palette instead of following
the established OrcSDR look. Never describe the current Home as visually
verified or ready to merge solely because it draws at 720x720.

**Preserve the established OrcSDR theme path:** core radio state and actions
feed screen contracts; shared status/header and design tokens define the
theme; board-specific layout arranges the SAME visible identity and
interactions. Initial baseline is the existing Fallout Terminal-style OrcSDR
theme, not a new bespoke Waveshare color scheme. Do not introduce an ad-hoc
second styling engine as a shortcut. The proposed runtime theme engine remains
a future architecture; parity with CURRENT working Tab5 UI comes first.

### Concrete, reviewable source of truth (origin/main)

- `apps/orcsdr-tab5/ui/dashboard_audio_control.cpp` and `.hpp`:
  shared header shell; 88px Orc badge; `OrcSDR` wordmark / screen subtitle;
  battery, HOME, MUTE, VIS, Settings icon positions; black ground,
  cyan and green outlined panels. The 1280x720 status-bar reserved area
  (x420..866, y12..66) is a **semantic contract**, not coordinates to scale.
- `apps/orcsdr-tab5/ui/home_dashboard.cpp` and `.hpp`:
  Home's cyan panel outlines, green live values, waterfall and frequency
  placement, status Wi-Fi/RTL-SDR/clock groups, rail navigation and tune
  workflow. Never transplant Tab5 pixel coordinates.
- `apps/orcsdr-tab5/ui/waterfall_style.cpp` and `.hpp`:
  the ACTUAL Home classic default plus user-selectable palettes/speeds.
  Waveshare must reuse this module or a shared extraction, NOT a custom
  heat-map function.
- `apps/orcsdr-tab5/main/orc_badge_104.rgb565` and `.png`:
  the existing Orc emblem to reuse without resynthesizing the artwork.
- `docs/images/dashboards/home.png`:
  an available original Home visual reference; compare rendered Waveshare
  screenshots with this plus CURRENT Tab5 build images.

### Mandatory screen-by-screen gates

1. Capture Tab5 current Home screenshot and document exact visible hierarchy,
   typography, theme tokens, status groups, navigation, and controls.
2. Map that structure to explicit 720-square regions while preserving
   recognizability and touch affordances. Review a static screen capture BEFORE
   approval to flash a visual experiment as the user-facing Home port.
3. Create a shared design token/header model that can be consumed by the
   LVGL 4B renderer without altering the Tab5 M5GFX pageflip path.
4. Display the actual Orc badge and OrcSDR HOME wordmark; reproduce
   cyan/green/black frame and type treatments. Preserve Wi-Fi/RTL-SDR/status
   grouping with truthful unavailable labels; do not invent connectivity,
   battery readings, audio playback, or functional settings/navigation.
5. Reuse existing Home waterfall palette logic. Add reference screenshot
   comparison (manual image approval first; automation afterward), label
   and action hitbox tests, and physical touch verification.
6. Repeat the same gates for EACH additional screen, never bulk-mark a
   dashboard family as ported simply because one viewport rendered.

### Known gaps in existing Waveshare Home

- No Orc badge and no actual shared header/status shell.
- Handpicked colors `0x07121C`, `0x12323E` and `0x75F4DD`
  differ from active source themes (Tab5 shared header uses panel RGB565
  `0x0841`, cyan `0x2E7F`, green `0x6FE8`; Home frame uses
  cyan `0x05FF`, green `0x6FE0`).
- No left navigation/browse visual contract; no Home/audio/visualizer/
  settings shell treatment.
- Handcoded waterfall `palette()` bypasses `waterfall_style`.
- "WAVESHARE 4B" is featured as the UI's product identity where the
  primary identity must be **OrcSDR**.

**Status: visual acceptance FAILED.** RF functionality and native compilation
are independent and cannot turn this into a UI pass.
