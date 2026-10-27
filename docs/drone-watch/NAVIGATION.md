# Drone Watch — navigation and interaction map

**Architecture:** One OrcSDR dashboard registry entry, one ScreenController ID and framebuffer owner, five internal category groups and 25 internal screens. Each screen has stable logical `ScreenKey`, category, requirement gate, focus and scroll state. All implementation below is **planned**.

## Five category entry points and proposed screens

| Category | Default screen | Other screens |
| --- | --- | --- |
| WATCH | W01 Live Watch | W02 Event Timeline, W03 Alert Rules, W04 Coverage Monitor, W05 Session Summary |
| AIRCRAFT | A01 Aircraft List | A02 Aircraft Details, A03 Remote ID Inspector, A04 Observed Track History, A05 OrcMaps, A06 ADS-B Context |
| RF LAB | R01 Live Spectrum | R02 Waterfall, R03 Band Occupancy, R04 Frequency Profiles, R05 Signal Comparison |
| MEDIA | M01 Video Sources | M02 Live Analog FPV, M03 Media Gallery, M04 Acoustic Monitor |
| SYSTEM | S01 Sources, (or first available status) | S02 OrcNode Network, S03 Session Logbook, S04 Capture Replay, S05 Diagnostics |

Count = 5 + 6 + 5 + 4 + 5 = **25**. Do not create a separate top-level category for MAP or SETTINGS; map lives under AIRCRAFT and settings in existing OrcSDR Settings, with Drone Watch's source controls under SYSTEM/SOURCES.

## UX flow (examples)

```text
OrcSDR HOME
  -> DRONE WATCH [one dashboard entry]
    -> WATCH / LIVE
         tap latest observation -> AIRCRAFT / DETAILS
             inspect message -> AIRCRAFT / RID INSPECTOR
             show reported position -> AIRCRAFT / MAP
         tap event -> WATCH / TIMELINE (focused event)
         tap source badge -> SYSTEM / SOURCES (focused source)
    -> AIRCRAFT / LIST -> DETAILS -> RID INSPECTOR / TRACK / MAP
    -> RF LAB / SPECTRUM -> WATERFALL -> OCCUPANCY -> PROFILES -> COMPARE
    -> MEDIA / SOURCES -> VIDEO / GALLERY / ACOUSTIC (capability-gated)
    -> SYSTEM / SOURCES -> ORCNODE / LOGBOOK / REPLAY / DIAGNOSTICS
  HOME -> restores previous non-Drone-Watch radio session
```

## Navigation components

1. **Primary category bar:** WATCH, AIRCRAFT, RF LAB, MEDIA, SYSTEM. Use one shared bottom navigation renderer/hit test, rather than duplicating navigation per page. At 1280×720 five buttons fit; at narrow widths display shortened names with descriptions on selection.
2. **Secondary category screen selector:** a 52px-high shared row with page labels or `PREV` / current page number / `NEXT` on 720×720. Favor a large page name rather than six tiny labels. A contextual breadcrumb or `BACK` is visible when invoked from another page.
3. **Content detail:** list selection changes an optional side inspector on wide screens; on 720 square it navigates to a full detail page. Both use the same view-model.
4. **Top header:** reserved OrcSDR global Home/volume/Visualizer/Settings/battery controls, managed by canonical header API. Never place a new competing Drone Watch global Home control into its hit region.
5. **Help:** `i` opens a short explanation overlay for currently visible source, classification, validity, units and known limits; responsive even when offline.

## Back-stack and state policy

- Maintain a bounded back stack of `{category, page, selected_event_key?, selected_aircraft_key?, selected_source?, scroll_offset, filter}` snapshots (suggested cap 8; implementation must confirm memory).
- `BACK` pops the cross-category context link; if no nested selection exists it returns to that category default screen. OrcSDR `HOME` exits directly, with previous receiver state restored.
- Returning from global Settings respects ScreenController's documented `return_to`; no Drone Watch renderer may preempt the global overlay.
- A broadcast ID rotating does not automatically select another apparent aircraft. If an ID expires while detail is open, preserve the selected snapshot as clearly **STALE** or show `RECORD EXPIRED`; do not silently switch selection.
- User selections (sort/view/filter/alert preference) are held in RAM by default; persist selectively only with existing Settings/NVS conventions and explicit error/rollback behavior. Sensitive IDs/coordinates do not persist without opt-in.
- A user may always navigate to SOURCES and DIAGNOSTICS even when no receiver exists. Unsupported MEDIA page remains an explanatory screen, never a faked video.

## Cross-screen source-ownership rules

- A source's acquisition lifecycle is **independent of page drawing**. Moving from WATCH to AIRCRAFT must not inadvertently stop BLE reception; source pause/restore must be explicit and reflected in source-state timeline.
- RTL-SDR scans or video source tuning require arbitration with active radio session. No page may quietly seize tuning just because it was opened.
- No search/compare/map page starts SD/network work inside draw(); request asynchronously and update only changed regions when ready.
- One observed Remote ID message can appear in Watch, Timeline, Aircraft, Inspector and OrcMaps by stable observation key. Make source and clock provenance identical across all presentations; no duplicate contradictory synthetic values.

## Implementation sketch (not an existing API)

```cpp
enum class Category : uint8_t { watch, aircraft, rf_lab, media, system };
enum class Page : uint8_t {
  live_watch, event_timeline, alert_rules, coverage, session_summary,
  aircraft_list, aircraft_details, remote_id_inspector, observed_track, orcmaps, adsb_context,
  live_spectrum, waterfall, occupancy, frequency_profiles, signal_compare,
  video_sources, live_video, media_gallery, acoustic_monitor,
  sources, orcnode_network, logbook, capture_replay, diagnostics
};
struct ViewRoute { Category category; Page page; uint64_t selected_key; };
```

**Where:** `apps/orcsdr-tab5/ui/drone_watch_app.*` owns route/state; `drone_watch_dashboard.*` handles shared shell; individual views live in logically grouped submodules (five screen-category renderers or smaller bounded components as needed). Neither `main.cpp` nor ScreenController should accumulate 25 separate dispatch paths.

## Capability-aware selection behavior

Supported + active: page opens directly. Supported but unplugged: page opens useful setup/reconnect explanation. Not implemented: menu page can be hidden from production builds or marked `COMING LATER` in development; never display a fake active control. Experimental: show warning and tested hardware/firmware boundary; opt-in tests require explicit user action. Default landing is always WATCH even when scanner unavailable.

## Navigation acceptance

Use recording touch coordinates to assert global hit precedence, exact screen count, every internal route, stable focus/scroll on back, bad/expired keys, disconnected source, overlay blocking and no draws after ScreenController loses ownership. On 720×720 a single hand can reach every primary/secondary action without microscopic text.
