# P4 P25 shared renderer migration

This change makes the existing `orcsdr::p25` renderer responsive to the P4
console's selected safe viewport. It keeps one renderer and the existing
`Snapshot`, `View`, `Action`, receiver, decoder, and profile models. It does not
add a P4 screen adapter or route; those remain a separate runtime integration
gate.

## Viewport behavior

`p25::set_viewport(x, y, width, height)` is called by the UI owner after a
display geometry transition. It accepts the legacy Tab5 viewport by default and
the tested 800 x 480 Hosyond, 424 x 1088 LilyGO portrait safe area, and 1088 x
424 LilyGO landscape safe area. The same `Layout` rectangles drive drawing,
touch handling, keyboard focus, plot geometry, and the text editor's owned area.
An invalid viewport leaves the current layout active.

The transition preserves the selected P25 view, profile cursor, talkgroup page,
program detail page, control-channel page, and active rename draft. Ordinary
snapshot updates only refresh the owned P25 widgets; unchanged snapshots do not
draw. Tab/page changes and viewport transitions are structural redraws.

The native program view uses separate Systems and Details pages. Stored profiles
are paged, and all configured control channels are visible across the Details
page. Talkgroups are paged on short landscape displays. The P25 focus and
coordinate-free `Control` seam return the same `Action` values the parent UI
uses for touch handling; they do not call radio, storage, or audio services.

## Host checks

Run the focused geometry and production-renderer/input checks with:

```text
python -m pytest tests/test_p25_dashboard_layout.py -q
```

The tests check legacy geometry, safe bounds, minimum control targets, populated
and empty lists, maximum profile/talkgroup/channel counts, all five views,
representative touch and named-control actions, resize-preserved selection and
rename draft, focus targets below the legacy 720-pixel extent, and value updates
without another full-screen clear. The recording fixture also bounds the P25
trace sprite to the legacy 1186 x 143 x 2-byte allocation (339,196 bytes).
The persistent waterfall color row is 1188 x 2 bytes (2,376 bytes) in PSRAM.
The recording host checks the P25 allocation upper bound; it does not measure
target-board free/largest PSRAM blocks or DMA behavior.

## Remaining P4 acceptance

- Add and review the P4 `screen_p25` adapter, router registration, and runtime
  service hooks in the separately owned P4 integration change.
- Run the selected native IDF build and the P25 service checks after integration.
- On LilyGO, capture the P25 screen in portrait and landscape, verify touch and
  keyboard focus, populated program/talkgroup pages, resize state preservation,
  radio continuity, and physical flicker/tearing. Host recording evidence does
  not establish these hardware results.
- Verify the target's actual text metrics and available PSRAM under concurrent
  radio, decoder, and map workloads. The renderer sets native text to M5GFX text
  size 2 and allocates no full-frame buffer.
