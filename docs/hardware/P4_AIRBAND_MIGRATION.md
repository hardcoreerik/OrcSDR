# P4 Airband responsive migration

The P4 Airband adapter now uses the same renderer and touch handling as Tab5.
The shared renderer derives all six tab rectangles, action controls, list rows,
scope plot bounds, and setup-page controls from one `airband_dashboard_layout`
object. `dashboard_set_viewport()` is a UI-owner operation: it changes geometry
and redraws while retaining the active tab, scanner settings, pagination, and
an open record-detail overlay. The P4 geometry callback does not retune or
restart radio/audio services.

The migrated tabs remain **Listen**, **Scan**, **Airports**, **Activity**,
**Setup**, and **Scope**. Scan-bank, catalog, and activity lists expose every
snapshot record through previous/next paging. Tapping a record still emits its
existing tune action and opens a detail overlay that displays the full stored
label and record fields. Setup uses three pages on landscape layouts so its
44-pixel controls and help text remain usable.

The named UI-owner seam is `dashboard_control(Control, value, applied)`. Its
`Control` enum names each supported user action (for example, `tune_up`,
`clear_activity`, and `filter_set`) plus `select_tab`. It returns an `Action` for
the screen owner to apply using the existing handlers and rejects invalid
values/capability-gated requests. It is not a renderer entry point for serial
worker threads; the parent integration must route named serial commands through
the UI owner.

Ordinary snapshot revisions update bounded cards, rows, and scope regions. The
activity age refresh updates only visible rows. Screen entry and page or
viewport transitions repaint the viewport. Scope sample and waterfall storage
is bounded to 1,232 pixels: 7,392 bytes for the RGB565 row and float level
arrays. The spectrum sprite is allocated only for the current plot dimensions
in PSRAM; its largest supported geometry is 1,230 × 148 × 2 = 364,080 bytes
(about 356 KiB), matching the legacy 1280 × 720 layout. A dimension change
replaces the sprite; no full-frame dashboard buffer is allocated.

## Host validation

Run `python tests/test_airband_dashboard_layout.py`. It compiles the pure
geometry contract and the production Airband renderer/input implementation
with a recording display. The matrix covers Hosyond 800 × 480, LilyGO safe
424 × 1088 and 1088 × 424 rectangles at (72, 72), additional 416 × 1136
and 1136 × 416 safe-area boundaries at (72, 72), and legacy Tab5 1280 × 720.
The renderer fixture fills the catalog, scan bank, and activity log to their
32-record capacities with long labels and maximum-width numeric values. It
also checks empty/default pages and a long current station label. Every recorded
text paint is checked against the native viewport; narrow gain labels remain
visible, all gain controls stay in bounds, and scope starts with a nonzero span.
It checks final-page selection, detail text, tab/setup/scope input, visible
value updates, focused-control retention, repeated age updates, no full-screen
clear, and state retention when the viewport changes.

Host validation does not establish the P4 IDF build, radio/audio continuity,
touch calibration, visual readability, flicker/tearing behavior, or sustained
scope performance. Those remain separate physical acceptance gates; no P4
hardware run is recorded by this migration.
