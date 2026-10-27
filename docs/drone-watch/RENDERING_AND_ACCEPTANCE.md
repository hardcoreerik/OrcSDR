# Drone Watch — per-screen implementation and test recipe

All 25 screens must follow [UI_UX_STANDARD.md](UI_UX_STANDARD.md), [NAVIGATION.md](NAVIGATION.md), [DATA_CONTRACTS.md](DATA_CONTRACTS.md) and OrcSDR's [rendering standard](../UI_RENDERING_STANDARD.md). Screen-specific checks appear in each file. This describes future development work, not executed tests.

## Renderer decomposition

```text
main.cpp (minimal adapt/wire)
  -> drone_watch_app (one ScreenController owner, source snapshots, selected route)
     -> drone_watch_dashboard (canonical header, secondary selector, category footer)
        -> watch_views / aircraft_views / rf_views / media_views / system_views
           -> bounded ViewSnapshot and cached widget geometry
           -> render_static(layout)   on page enter / structural reflow
           -> render_changes(prev, now, dirty)  only changed widgets
           -> hit_test(x,y) -> semantic Action
```

The active draw target is `M5.Display` through existing shared UI service, or an explicitly sized bounded sprite for difficult graphics. Never draw in C6, USB, DSP, decoder, map/storage worker callbacks. Never allocate a separate full-frame double buffer as the default implementation.

## Logical screen layout

Reference 1280×720 landscape: canonical header `y=0..113`; second-level page bar `x20..1260, y118..178`; content `x20..1260, y188..634`; bottom five categories `y649..705`. The shared layout solver must recalculate actual dimensions after rotation or display change, respect safe insets, keep touch >=52×52, clip panels, and avoid unreadable shrink-to-fit. 720×720 switches side panels to a stack or detail page and secondary navigation to a selector, without duplicating the display code.

## Redraw and resource tests (every screen)

1. **Entry**: one structural clear/draw, one shared header/footer, all touch rectangles unique and inside actual bounds. Nonexistent source displays a genuine empty state.
2. **Changed-value**: emit two snapshots changing exactly one field. The changed widget pixels/text update; unrelated header/footer/panels retain their previous framebuffer data; full-screen-clear count **does not increase**.
3. **No-op / background**: 100 idle ticks and 100 source updates while a different page is foregrounded. No whole-page draw, no buffer leaks and no background display writes from inactive view.
4. **Structural**: list row count changes or orientation reflow redraw only the needed content safe region; global header/global overlay remains owned by shared service.
5. **Overlay**: volume tray / Settings/modal covers content; lower touch must not pass through; renderer doesn't erase tray or map attribution. Immediate pending indicator before async save.
6. **Staleness**: no-source, paused, stale, clock not valid, malformed packet and invalid coordinates each produce correct wording and omit unusable numbers/maps.
7. **Navigation**: every category and secondary page reachable; Back preserves prior list selection/filter/scroll until associated record expires; global Home returns to previous receiver mode.
8. **Geometry**: host layout bounds at 1280×720 and 720×720; 5-inch Tab5 tap targets, contrast and text truncation confirmed on physical hardware separately.
9. **Load**: bounded queues/rows and bursty updates, disk removal, network loss and nonblocking worker state. No hidden C6 firmware write or new radio tuning on page entry.
10. **RF regression**: compare driver USB overruns, app IQ drops, memory/stack, camera/map redraw duration, and FM/RDS/ADS-B/LoRa/Dead Air continuity against same-version baseline. Host success is not hardware acceptance.

## Rendering example (pseudocode, adapt to actual OrcSDR APIs)

```cpp
void WatchPage::render_changes(const Snapshot& old, const Snapshot& next) {
  if (old.last_seen_text != next.last_seen_text) {
    const auto r = layout_.last_seen_rect;
    clip(r);
    fillRect(r, palette_.panel);
    drawTextWithin(r, next.last_seen_text);
  }
  if (old.packet_count != next.packet_count) {
    const auto r = layout_.packet_counter_rect;
    repaintWidget(r, next.packet_count);
  }
  // No fillScreen(), no page-wide clear, no redraw of unchanged tabs.
}
```

Exactly how to render a custom M5GFX region, M5GFX sprite, or shared map output is an implementation choice; preserve the caller's clipping and buffer ownership contract. A fixture-driven host visual regression should record affected rectangles and inspect **zero full-screen clears on data ticks**.

## Release and evidence workflow

From a clean implementation branch: read current `apps/orcsdr-tab5/ui/AGENTS.md` and `docs/UI_RENDERING_STANDARD.md`; write failing host tests; add the smallest screen slice; run targeted host and UI recording tests, capture command outputs in the invocation directory; run actual pinned native ESP-IDF build; record hash, partition margin and memory; then test **only on explicitly authorized physical hardware**, log serial and RF observations. Keep the P1/P2/P3... status and `PROJECT_STATUS.md` evidence terms separate.

- Rendering-only completed: **Implemented / host regression** if host fixtures truly ran; not RF-Verified.
- Working simulated UI: always `TEST DATA`; cannot claim receiver availability.
- Actual Remote ID: measured raw authorized frame + reference decoder + matching screen and source firmware metadata.
- OrcMaps: only real decoded valid broadcast location, aged correctly; ensure geography/labels/attribution preserved.
- Optional video/acoustic: physically tested accessory, displayed real frames/audio and explicit handling of scan interruptions, not mockups.

Use [VALIDATION.md](VALIDATION.md) for the system-wide D0–D13 matrix. Each screen document's acceptance bullets are additional requirements, not replacement evidence.
