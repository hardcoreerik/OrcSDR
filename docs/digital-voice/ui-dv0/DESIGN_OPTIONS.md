# Two native-renderer design options — scanner first

Both concepts share the **same compiled C++ M5GFX drawing code**, common header boundary, navigation geometry, and source-pinned snapshots. `Density` changes the actual rectangles and fields rendered; neither concept is a separate mockup.

## A — Discovery-first / SCAN-first (default)

Emphasizes the SCAN entry view, a large tuned-frequency/receiver state panel, a capability-gated start/stop control, and a separate observation/evidence panel. Live shows a large protocol/call identity panel alongside protocol fields. Goal: learn a system without knowing a codeplug, talkgroup or NXDN definition. Large touch targets, clear plain-language states, origin and availability labels. Uses `layout::kBasicHero` x20 y158 w802 h478 and `kBasicFacts` x834 y158 w426 h478.

**Strength:** approachable for amateur, entry-level scanner and casual monitoring users. **Trade-off:** fewer diagnostics visible at once.

## B — Analysis-first

Emphasizes receiver/acquisition details, integrity and diagnostics, and tighter identity/service panels. The 610/618 pixel split permits two comparable bodies of RF evidence. Uses `layout::kAdvancedMain` x20 y158 w610 h478 and `kAdvancedFacts` x642 y158 w618 h478.

**Strength:** credible technician workflow and less navigation for deep metadata. **Trade-off:** denser interface; shorter text gets clipped in narrow fact cells and must be checked on hardware.

## Recommendation

One **Digital Voice** dashboard with **SCAN** as the first tab, six views, and a simple **Discovery / Analysis** density setting. Share facts and controls, not duplicated protocol-independent receivers. Default to Discovery-first. Persist the density preference only after the UI owner approves storage wiring. The current source does not write preferences; it receives `UiState` from the caller.

## Approval gallery

No approved images are available. `screenshots/option-a/` and `screenshots/option-b/` are intentionally empty. 16 required A-state captures + 6 B-view captures are planned in the manifest. Generating approximate PNGs would violate the task's absolute screenshot requirement.