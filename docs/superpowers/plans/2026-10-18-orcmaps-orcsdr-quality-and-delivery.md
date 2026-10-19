# OrcMaps / OrcSDR Map Quality and Delivery — Approved Execution Plan

> **For agentic workers:** Continue the existing implementation under applicable repository instructions and available skills. The user approved execution on October 18, 2026 ("approved. please enable this."). Current delivery work is Phase 4; Phase 2 map quality, Phase 3 responsiveness and outstanding Phase 1 gates remain open. This approval does not authorize paid services, publication, destructive operations, or bypass the user's approval requirement for high-usage work.

## Current phase and handoff — October 19, 2026

**Active delivery work: Phase 4; Phases 2 and 3 remain open.** Labels and cached
gestures run on COM17. Delivery now has a signed catalog consumer and hidden
resumable staging, connected worker/setup UI and bounded COM17 evidence. The
next delivery gate is actual signed HTTPS transfer and recovery. Persistent catalog trust,
managed ownership and pending-install state now have focused host/P4 checks;
authenticated acceptance saves trust before exposing a catalog. These delivery
APIs now have a coordinated installer on the existing map worker. Catalog fetch,
ordinary installs and setup download list/progress/cancel/retry are connected in
source at `21c5c5a2`, native-built and app-only deployed on COM17 from
`5052153e`. Bounded boot, existing picker, retained-map SD and radio checks pass;
actual HTTPS install/recovery validation remains open. Production URL/key are
unconfigured.
Journaled activation/recovery now passes focused host tests and P4 syntax checks;
it is wired before discovery and native-linked; normal boot is tested, but
physical interrupted-install recovery is not.
The 33 ms target, boundary hierarchy and worldwide font/quality acceptance
still require work. The execution ledger below is chronological history; earlier
statements such as "not pushed" or "approval pending" describe that earlier step,
not the current state.

| Phase | Current status |
| --- | --- |
| 1 — baseline | Candidate, hashes, reference views and bounded P4 measurements recorded. Full source/view acceptance and final budgets remain open. |
| 2 — labels/style | Bounded labels and real-font adapters implemented; host and COM17 evidence recorded. Boundary hierarchy candidate 3d9d01c passes the approved configured host regression (7/7); reference renders and device acceptance remain open. Contrast and international coverage remain open. |
| 3 — fluidity | Cached preview tested on COM17 and physically accepted: moves with the finger and feels noticeably better. Bounded RF, SD and memory-fallback checks passed. Latest native-order preview measurements show pan paint p50/p95 61/83 ms and zoom233 ms (three scaled samples). The 33 ms target is not met; continuous-motion consistency remains open. |
| 4 — catalog/delivery | Global candidate preflight passes. Signed catalog, staging, activation/recovery, persistent trust/ownership and pending intent have focused host/P4 syntax evidence. Recovery precedes discovery; trust acceptance persists before exposing catalogs. Native build and COM17 normal boot/map/retained-SD/radio checks pass. Actual HTTPS install, physical NVS/activation recovery, transfer scheduling and physical wizard download validation remain open. No global-pack device installation or publication yet. |
| 5 — setup | Wizard-owned UI is committed at210002f4 and device-tested in bounded scopes. Complete flow, downloads, interruptions and physical acceptance are incomplete. |
| 6 — integration | OrcMaps PR #6 merged and verified on origin/main at 722d0d3. OrcSDR source pin/lock updated with focused checks; merged pin native-built and app-only deployed on COM17; no OrcSDR main merge or release. |

Checked items below close only their stated behavior/test scope, not an entire phase.

### FT8 whole-world map migration — sandbox candidate, October 19

- Review branch `codex/ft8-orcmaps` starts at verified shared-label checkpoint
  `af877ed7189d99f99873f61f5be3995ad893db3b`. This is an isolated
  review candidate **not merged** into the integration branch or main.
- FT8 now requests the embedded, offline `firmware-world` through the existing
  shared asynchronous map service. A local 512-square Mercator-to-equirectangular
  compositor preserves one full world, 30-degree Maidenhead grid, receiver and
  decoded station overlay agreement, dateline seam and true polar grid-only caps.
  No engine or shared-worker changes or interactive pan/zoom. The independent
  FT8 `LABELS ON/OFF` control uses existing NVS preference; shared city
  label zoom threshold remains unchanged.
- Pure projection test passes a full 824x420 pixel-centre sweep under
  ASan/UBSan. Draft PR #204 FT8-core CI run 39335433015 passes both optimized
  and sanitizer dashboard/projection suites and FT8/FT4 diagnostic integration
  after a caught/fixed unavailable-frame status assertion and async redraw race.
  Full native/P4 link, source performance, missing-world partition and COM17
  physical acceptance remain open; an unrelated existing architecture.md
  ScreenController-ID mismatch fails Documentation Truth. **This is not
  integrated device completion.** See
  [the exact limited evidence and device checklist](../../evidence/maps/2026-10-19-ft8-orcmaps-sandbox.md).

### Geographic label controls source checkpoint - October 19

- Picker, LoRa and ADS-B now have independent persisted label controls in source.
  Shared frame/preview identity includes the setting; ORCMAP1 geographic labels
  honor it too, retaining geometry, attribution and radio overlays.
- Three focused host checks pass, including the real global archive, picker
  control and failed-save behavior; seven actual P4 syntax checks pass.
- Native link/COM17 persistence and physical overlay checks remain open. FT8's
  visible control is still paired with its unfinished basemap migration.
- [Exact tests, logs and limits](../../evidence/maps/2026-10-19-geographic-label-controls.md).

### Download flow source checkpoint - October 19

- `21c5c5a2` connects catalog fetch and the self-contained Get Maps screen to
  the existing worker. Five targeted host programs, six configuration tests and
  six P4 syntax checks pass. This is source/host evidence, not device acceptance.
- Review found pending recovery hid Retry and missing archives appeared installed;
  both are corrected. Missing-archive repair and a cut after repaired archive
  publication pass the host transaction test.
- Downloads default to explicitly unconfigured pending an approved HTTPS catalog
  directory/public key. Installed and embedded maps remain usable offline.
- Native build/placement and COM17 app-only flash now pass, with normal boot,
  existing picker, retained-map SD and active stream checks. Actual signed HTTPS
  transfer and physical recovery remain open; no production source is configured.
  COM39 remains reserved for Dead Air.
- [Commands, logs and open gates](../../evidence/maps/2026-10-19-map-download-flow.md).

### October 19 goal continuation and clarified scope

- Activation/recovery source committed as `119baadb`. Focused transfer/activation
  CTest passes 1/1, shared SD DMA check passes 1/1, and four actual P4 source
  syntax checks pass. Recovery precedes inventory discovery and preserves old
  archives. No native link/flash or physical SD interruption test in this step.
  [Commands, logs and unclosed gates](../../evidence/maps/2026-10-19-map-activation-recovery.md).
- The user clarified the target: worldwide **regional** geography, cities/towns,
  supported administrative boundaries, major roads and water. Street names and
  street-by-street coverage are not required. Assess the existing 543 MB global
  candidate before introducing additional detailed packs.
- Add a visible geographic Labels On/Off control per map, with independent
  persistent dashboard choices. Keep radio overlays and map geometry visible;
  retain zoom-dependent label thresholds. This is approved scope, not deployed.
- Dashboard source inventory: LoRa and ADS-B use `maps::draw_region`; FT8 still
  uses `draw_world_compat` with static world-outline data. FT8 migration and final
  dashboard-specific interaction/overlay tests remain required. Picker acceptance
  does not establish dashboard-wide or full-global-pack acceptance.
- Catalog trust, explicit managed ownership/selection and one pending install
  now have a bounded NVS record and authenticated acceptance APIs. Focused state,
  NVS-stub and transfer tests pass 3/3; 37 signed cases exercise direct/persistent
  acceptance; P4 syntax passes 4/4. OrcMaps producer `e50c6bd` passes 11 focused
  tests. Both sides require stable product IDs in the unpublished v1 contract.
  No producer push, native build/flash, actual NVS write or network request.
  [Exact commands, logs and limitations](../../evidence/maps/2026-10-19-map-delivery-state.md).
- Next delivery milestone: connect trusted catalog fetch, pending intent,
  staging/activation/state commit/finalization and reboot reconciliation to the
  serialized worker and self-contained wizard, then validate on COM17.
- Coordinated installation and offline restart recovery now pass the focused
  state/NVS/transfer checks (3/3), with five P4 syntax checks passing. Recovery
  runs on the existing worker and reports status in Local Maps. Review exposed
  an A → B → A artifact-reuse bug; its regression was red, then passed after
  stale-marker preparation preserved older metadata in hidden history. No
  native link/flash or physical SD/NVS validation yet. Network catalog fetch
  and normal download controls remain the next implementation step.
  [Commands, logs and remaining gates](../../evidence/maps/2026-10-19-map-install-coordinator.md).

Current deployed source: `aca08b75` (native RGB565 preview), following committed
wizard checkpoint `210002f4`. Host test registration `344765d1` changes no
firmware. The bounded actual-M5GFX pixel test, native build/app-only flash,
startup/map/nav/radio and retained-map SD readback pass. Current pan paint
p50/p95 61.397/83.178 ms; zoom233 ms (three samples). **33 ms remains unmet.**
Both preview and target now report format272, rotation3 unchanged. No added
preview memory. The user accepted physical colors and controls for this candidate on October 19.
Prior save-feedback and strip candidates have user acceptance. Preserve all
artifacts/recovery. Session-wide routine build/COM17 approval remains in effect.

Continuation: [ready-to-use completion goal prompt](2026-10-19-orcmaps-completion-goal-prompt.md).

### Current-session authorization — October 19

The user explicitly requested fewer interruptions and granted full approval to
continue routine builds, COM17 app-only flashes and regression tests for this
session. Do not ask again for each such cycle. Preserve NVS, SD data, embedded
world, C6 firmware and recovery; COM39 remains Claude's device. This temporary
session authorization does not automatically carry into a new chat or authorize
main merges, releases/publication, paid services or erasing user data. Historical
approval requests below remain evidence of earlier boundaries.

### Hardware ownership — October 19

- **COM17 remains the maps test device.** Confirm USB serial `30:ED:A0:E2:E6:95`
  before device work. Preserve recovery, NVS, SD packs, embedded world and C6 3.0.6.
  Current-session routine build/app-flash/test authority is recorded above.
- **Claude owns COM39 for the Dead Air dashboard**, with user authorization to
  flash it. Maps work must not open its serial port, reset or flash it without
  coordinating a new handoff. Earlier COM39 app observations are historical;
  inventory its current firmware before future reuse. The user's COM39 C6 1.4.1
  preservation constraint remains in force.
- Subagents are permitted when useful. Give them bounded scope and explicit
  file/device ownership; this does not waive expensive-work approval requirements.

### Latest verified device state — October 19 native-order preview

- COM17 app `maps native preview 2026-10-19`, sourceaca08b75; appSHA256
  `a5ed68c89fe832ecb5dab52a6a99c131d5992ad24e0fb289bc7abedbd00f4547`.
  App5065040bytes, headroom1226416; labels/update/preview/facts remain external
  BSS, DMAreserve40960. No extra preview allocation. Pin722d0d3/driver0.9.3/
  C63.0.6 unchanged. COM39 untouched.
- Artifact `.local/maps/preview-format-aca08b75-20261019/`, previous210002f4
  app and fresh non-app backups verified/retained. Native build/miniz/FT8/pin
  gates, app-only flash/preservation/startup pass. Exact commands/logs in evidence.
- Pan24samples paintp50/p95/max61.397/83.178/83.643ms;
  transfer48.459/83.157/83.621ms. Zoom3samples paint232.754/233.155ms,
  transfer200.906/201.236ms. Observed lower sequential-run cost, not a general
  percentage speedup or continuousFPS claim. **33ms unmet.** Worker-publication
  conversion cost not separately quantified.
- Runtime sourceDepth272/targetDepth272; both native RGB565 byte order.
  Internalrotation3 unchanged, so direct M5GFX rowcopy remains unavailable.
  Real-M5GFX host test covers65536values/eight traversals/reverse destination,
  passes1/1; P4 syntax and native gates pass. Physical colors/controls accepted by the user.
- Retained-map existing8KiB SD readback hash matches with2835456mapbytes held;
  no new SD write. Complete SD/read/resume plan passes. Navigation Home/RFLab/
  repeatedSettings/Home passes graphics1; picker reopened ready. Driver~2.4MSPS,
  zero observed drops/overruns, C6still3.0.6. No RF-decode-quality assertion.
  Device left receiving BROWSE26591754Hz at picker, serial released.
- Prior checkpoint210002f4 external-setup editor exit passed editor1→0,
  receptionIDLE→STREAMING. Save feedback and saved label physically accepted;
  coordinate persistence verified and original44.7145048,-123.2137829 restored.
  No new location writes in this native-order cycle. Final memory/pairing,
  worldwide quality and complete setup/download acceptance remain open.

Evidence: [native-order comparison/logs](../../evidence/maps/2026-10-19-preview-native-order-com17.md),
[actual library test/source](../../evidence/maps/2026-10-19-preview-native-order-source.md),
[preceding setup checkpoint](../../evidence/maps/2026-10-19-maps-checkpoint-com17.md),
[save/reboot/physical acceptance](../../evidence/maps/2026-10-19-location-save-feedback-com17.md).
No new overnight soak required.

### Remaining work in execution order

1. **Reduce transfer cost using measured facts.** Save feedback and saved-label
   display pass native and physical acceptance. Owned editor teardown passes
   host/device checks. Internalrotation3 and RGB565 byte-order difference are
   observed; native-order preview now removes repeated conversion, but direct
   M5GFX rowcopy is still unavailable at that rotation. Investigate
   an ownership-safe transfer improvement without changing the user's default
   orientation, arbitrary DMA calls or vendoring managed components. Pan/zoom
   still miss33ms. Continuous gestures/cancellation/all-orientation/finalmemory
   remain open. Keep global quality/delivery progress moving as separate gates.
2. **Validate the real global pack on COM17.** Former protomaps-v4 manifest
   rejection was compatibility failure, not SD failure. OrcMaps PR #6 fixes it
   and is merged at 722d0d3; OrcSDR source/lock pin it at 04b118a4. Requested,
   locked and fetched hashes match; unrelated dependencies are unchanged.
   Configured engine host CTest passed 7/7 at dbb1b98, and all four premerge CI
   jobs passed. Consumer map/preview/timing checks passed 3/3 against
   code-equivalent c39f827; only STATUS.md differs from merged engine code.
   Native linking and bounded COM17 boot/map/radio/SD checks of that pin now pass;
   real-global-pack acceptance remains open. Reuse the
   existing 543,380,195-byte global candidate and recorded hash, prepare valid
   metadata, verify recovery, and install without overwriting existing packs.
   Check reference views, labels, retained-map SD and active-radio continuity.
   [Merge and consumer evidence](../../evidence/maps/2026-10-18-orcmaps-main-merge.md).
3. **Finish geographic quality.** Distinct country/state/county styling,
   road/water/land-cover contrast, international font coverage and missing-glyph
   presentation remain open. English-first names and bounded label storage do
   not establish worldwide readability. Verify closer labels on the real global
   device pack; the positive zoom-6 case is currently host-tested.
   OrcMaps local candidate `3d9d01c` now distinguishes country/region/local
   boundaries, preserves legacy classification and bumps style versions.
   Focused profile CTest passes 1/1; standalone RunStyleTests passes with zero
   failures. The approved configured host build and CTest pass 7/7, including new
   direct/cache pixel parity and style coverage. Documentation commit 73b0d94
   records the run; worldwide reference-view and device acceptance remain open.
   This candidate is not
   pushed, merged, pinned in OrcSDR or deployed. Evidence/log commands:
   `F:/AI/OrcSDR-TEMP/orcmaps-quality-delivery/docs/evidence/2026-10-19-boundary-hierarchy-source.md`.
4. **Finish delivery and the self-contained wizard.** Choose and cost the
   owned catalog/hosting model, implement trusted resumable verified installation
   and failure recovery, then connect wizard-owned network/location/maps/download
   pages. Preserve manual imports and offline/no-SD paths. Setup pause passed
   bounded active/stopped, Wi-Fi overlap and Finish/Cancel checks, with zero
   observed drops/overruns. Physical hotplug, previously active audio restoration
   and automatic true-first-boot entry remain unverified.
   [Setup pause evidence](../../evidence/maps/2026-10-18-setup-reception-pause.md).
5. **Close combined regression and OrcSDR integration.** The repeated-Settings
   graphics fix passed host/native checks and 14 device state snapshots; user
   confirmed Home spectrum/waterfall work. Repeat relevant navigation/radio/SD/
   pairing-memory acceptance on the final candidate, reconcile preserved edits,
   obtain review and explicit OrcSDR merge approval, then verify remote main.
   OrcSDR is not merged or released.
   [Graphics evidence](../../evidence/maps/2026-10-18-home-graphics-disabled.md).

Supporting interaction evidence:
[physical cached-preview acceptance](../../evidence/maps/2026-10-18-preview-com17.md)
and [timing scout](../../evidence/maps/2026-10-18-preview-timing-scout.md).
The earlier two-frame SD scratch/hash and low-memory reclaim checks passed;
they do not close final-candidate memory or long-term reliability gates.

No new overnight soak is required. Deferred Wi-Fi-crash work and unrelated
receiver/DSP/USB changes are outside this goal. Custom provider/custom-area
features follow the offline baseline. No new large download, hosting purchase,
map-data publication or device erase is authorized by this checkpoint.

## Execution ledger — historical checkpoints

- October 19: sourceaca08b75 preconverts immutable previews to native RGB565.
  Actual-M5GFX host pixel regression1/1, P4 syntax, native build/app-only flash,
  startup, map/navigation/radio and retained-map SD readback pass onCOM17.
  Pan61/83ms p50/p95, zoom233ms(three samples);33ms unmet. No orientation or
  allocation change. Physical colors and controls accepted by the user; global goal remains open.

- October 19: location-feedback native build/app-only flash/save/return/reboot
  passed on COM17; user physically accepted Saving and saved label. Original
  point restored and screenshot coordinate fields matched byte-for-byte.
  Committed owned wizard/location work at210002f4 after bounded review; verified
  abandoned-editor bug corrected with generation ownership and real-editor
  focused tests2/2. Native checkpoint build and app-only preservation pass;
  runtime/editor/SD/radio checks in progress. Session routine authority recorded.

- October 19: exposed-strip native candidate 4eaf9fb5/eb04ae2e deployed on COM17;
  bounded timing/radio, retained-map SD and navigation checks pass. Pan-only paint
  p50/p95 69/99 ms, zoom 248/253 ms (three scaled samples); 33 ms remains unmet.
  User accepted previous candidate's controls/dragging. New location-feedback
  source fix passes host 2/2 and three P4 syntax checks, not deployed.

- October 19: approved native build and COM17 app-only deployment completed from
  34d3366c plus preserved wizard edits. Merged engine 722d0d3 boot/map checks,
  bounded radio/navigation and retained-map SD pass. Measured paint costs miss
  33 ms. The separate approved boundary regression passes 7/7 at 3d9d01c.
  Exposed-strip clearing/stage timing is prepared with focused host/P4 checks,
  not native-built or deployed. Physical confirmation remains pending.

- October 18 latest checkpoint: cached movement physically accepted on COM17
  ("Moves with finger; noticeably better"); evidence recorded at `477f087a`.
  User then requested city names only at closer zoom. Shared pipeline policy
  and focused host regression are prepared and pass; no subsequent native build
  or flash has occurred. Phase 2 and Phase 3 remain open.
- October 18: execution approved; existing wizard changes preserved. Read-only local archive inventory and three single-tile inspections completed. No firmware build, flash, download, or device operation in this assessment.
- Evidence: [Worldwide-base assessment](../../evidence/maps/2026-10-18-worldwide-base-assessment.md). Phase 1 remains open: no complete global regional-detail candidate or hardware performance baseline yet.
- Ruling: keep the embedded Natural Earth overview as the fallback, not the regional base. Its declared layers omit roads and land cover; its builder includes country/state boundaries but no county source. This avoids promising unavailable detail.
- Ruling: do not relabel a different vector schema as OpenMapTiles. A candidate with different layer/property conventions requires a verified adapter or a compatible production profile.
- October 18 continuation: reviewed the shared 0.3.0 cache diff and CYD evidence limits; reproduced and corrected thick-stroke cache bounds on isolated OrcMaps branch `codex/map-quality-delivery` (focused red then green test). COM17 read-only baseline captured with reception active, C6 unchanged; fresh gesture measurements remain open. Public global-build metadata returned HTTP 403; no bulk download attempted.
- OrcMaps fix committed locally as `10fc262`; four scoped files, focused `orcmap_screen_bounds_tests` 1/1 pass after eight thick-stroke failures before the fix. Not pushed, pinned, merged or flashed. Root red/green logs retained in the OrcMaps worktree.
- October 18: user approved one global candidate extraction capped at 600 MB. Acquired z0–8 candidate, 543,380,195 bytes, SHA-256 `16adde0b4f67734535db6871b89949e68c7e1dabf1c34d2bd7bd23842a9e5991`; proxy enforced ETag/ranges and transferred 543,439,268 bytes in five requests. Structure verification and eleven bounded reference tile decodes pass. Protomaps-v4 classification remains unsupported; this is source-data evidence, not rendering acceptance. Evidence: `F:/AI/OrcSDR-TEMP/orcmaps-quality-delivery/docs/evidence/2026-10-18-global-regional-candidate.md`.
- User selected English place names where available, then local names. Retain UTF-8 data and verify glyph support; this is not authorization to silently drop unsupported local names.
- Global candidate/acceptance evidence committed in OrcMaps as `70dcb47`. Explicit Protomaps-v4 engine classifier committed as `5572ed6` passes focused red/green tests, preserving existing profile behavior and source names/hierarchy fields. Actual 1280x720 Eugene host render finds 24/24 tiles; visible tile-edge grid and missing labels mean quality is not accepted. Next: reproduce/fix multi-tile seams, then labels/hierarchy styling. P4 pin and both devices remain unchanged by this work.
- Ruling: add an explicit engine-owned profile for the measured candidate instead of changing its metadata to OpenMapTiles. Profile classification does not by itself close rendering, licensing, global quality or hardware gates. Remaining Phase 1 hardware timing/glyph details stay open while bounded host correctness work proceeds.
- October 18 continuation: tile-edge grid reproduced in six synthetic cases across immediate/streamed/cached drawing and both tile orders. Prepared native-scissor fix in OrcMaps (uncommitted): composition/stroke bounds 2/2 focused tests pass; parent/empty/offscreen clip checks pass. Actual Eugene 24-tile host render no longer shows the grid. Full host regressions and adapter checks requested for approval; no native build, pin change or flash. Evidence: OrcMaps `docs/evidence/2026-10-18-tile-composition.md`.
- User approved the full host/adapter checks. Full build and 4/4 CTest entries pass; real SDK syntax checks pass for P4 map consumer, P4 renderer and CYD consumer. Seam fix committed locally as OrcMaps `5437b8c`. No native link/flash or P4 performance claim. Concrete bounded label API/resource design prepared in OrcMaps `docs/PLACE_LABEL_DESIGN.md`; original plan left final text API choices to the baseline review.
- Provenance continuation: added a distinct prebuilt Protomaps-v4 source record with publisher terms, input identity and explicit separation from raw OSM/source-policy versus pack/release approval. Candidate metadata reports v4.14.3 and OSM replication time March 31, 2026. Targeted provenance validator passes (11 records, 0 errors/warnings), log `global-candidate-provenance-20261018.log`. Found two still-closed compatibility gates: engine pack validation and OrcSDR streaming pipeline; do not enable them based solely on the host batch classifier. Label-design approval remains pending; no devices changed.

**Goal:** Deliver useful, fluid, offline-capable maps and a self-contained OrcSDR setup experience, with a catalog and reproducible map production under our control.

**Architecture:** OrcMaps owns reusable rendering, labels, camera behavior, caches and pack compatibility. OrcSDR owns setup, catalog browsing, verified installation and radio overlays through the shared map adapter. Map production, catalog schema/publishing and reusable pack verification live in OrcMaps; there is no separate OrcTiles product/repository. Published catalogs and storage remain replaceable.

**Tech stack:** Existing native ESP-IDF/M5GFX Tab5 application, OrcMaps C++, existing host pack builders and PMTiles-compatible vector data. Hosting and catalog transport remain decisions, not dependencies selected by this document.

**Spec:** User's Google Normal / Mesh Map screenshots and approved six-screen setup mockup. Orc branding and palette; state/county boundaries, city/town names, roads, water and forests; fluid interaction; self-contained Wi-Fi/location/download setup. The screenshots are visual references, not permission to redistribute Google map content.

## Confirmed user decisions — October 18, 2026

### Historical execution evidence — October 18 continuation

These entries preserve earlier checkpoints. Use the October 19 handoff above for
current source/device ownership and the priorities below for remaining work.

- User approved the bounded label design. OrcMaps `5f3a1eb` implements the
  fixed 128-candidate/64-placement layout, real-font adapters and profile-aware
  streaming classification. Full host regression before the final classifier
  extraction: 5/5 CTest programs passed; focused classifier test after extraction:
  1/1 passed. Real-font test: 1/1 passed. Eight worldwide z8 previews inspected;
  these are host evidence, not final style, script-coverage or device acceptance.
- User approved the development-branch push. Remote OrcMaps
  `codex/map-quality-delivery` was verified at full SHA
  `5f3a1eb767b26aa7df00158308f40331b298d7ef`. No main merge or release occurred.
- OrcSDR `66afa335`/`8d1eaa48` pin that engine through a component-manager
  regenerated lock; unrelated dependencies compare unchanged. Shared streaming
  integration collects labels in caller-owned storage; map service places the
  fixed layout in external RAM and reports unsupported-name counts.
- Focused OrcSDR host test with the actual global candidate passes, including
  repeat rendering and Eugene-to-London label reset. Two P4 syntax checks pass.
  Evidence: `docs/evidence/maps/2026-10-18-streamed-place-labels.md`.
- User approved the native build and, after an environment-related failure,
  one corrected retry. Corrected native build passed: 5,051,280-byte app; labels
  occupy 15,416 bytes in .ext_ram.bss; DMA reserve stays 40,960 bytes; pin, miniz
  and FT8 placement gates pass. Evidence: `docs/evidence/maps/2026-10-18-label-native-build.md`.
  No devices were changed; hardware approval and acceptance remain pending.
  Existing wizard edits remain preserved and uncommitted. The global profile
  remains closed at pack validation/discovery, pending full compatibility work.
- Boundary scout: country/region/county source information currently collapses
  to generic kBoundary, drawn similarly to roads. Proposed narrow engine scope
  is recorded in OrcMaps `docs/BOUNDARY_HIERARCHY_DESIGN.md`; not implemented.
  All phase gates below remain governed by their actual acceptance evidence.

1. Offline maps are the first-release baseline after download. Internet is for acquisition and updates, not a requirement for installed maps.
2. The first release targets worldwide regional detail, not an Oregon-only or U.S.-only release.
3. A global regional-detail base is the default downloadable map, with optional higher-detail local packs.
4. Map-building/catalog tooling is part of OrcMaps. “OrcTiles” was not a request for a separate repository/product.
5. Quality reference: Google Normal in the Meshtastic / Mesh Map Android app, including the supplied Oregon regional screenshot. Use our own licensed data and styling.

The built-in overview remains the no-SD/no-network fallback. It must not be described as equivalent to the global regional-detail base. Regional means useful cities/towns, administrative boundaries where supported by the data, major roads, water and land cover; worldwide street/building detail is not part of the default pack.

Standard compatible PMTiles packs, our own signed catalog, replaceable hosting and manual compatible-pack import form the initial design. Optional custom online providers and custom-area build queues are later work. No cloud account, subscription, publication or spending without approval.

### Decisions settled by measurement before production

- Exact base-pack bytes, zoom cutoffs, simplification and feature inclusion; no small-size promise before a real build.
- One logical global base versus transparently managed physical shards. The UI should present one global product; sharding must not silently change it into user-selected regions.
- Worldwide glyph coverage. User selected English names where available, then local names; never silently drop unsupported place names or claim native-script parity without testing.
- Hosting provider, retention/update cadence and spending limit after pack-size and 20,000-user cost estimates.
- Numeric hardware pass budgets after baseline measurements; the initial cached-motion target remains approximately 33 ms.

## Initial planning baseline — historical, superseded by current handoff above

- OrcSDR checkout: F:/AI/OrcSDR-TEMP/maps-integration-completion, branch codex/maps-integration-completion, last commit dfde5cfd. Work-in-progress wizard-owned screens are preserved, uncommitted. Their four focused host tests passed before implementation was paused; native build and hardware acceptance remain untested.
- Cached origin/main comparison: 0 behind, 37 ahead. Refresh/reconcile before final PR preparation; this is not a claim that remote main was checked live during this planning pass.
- OrcSDR pins OrcMaps dc6bc12 (v0.2.3). Local F:/Ai/OrcMaps/main is clean at 86e27a0 with version 0.3.0 and new caching/rendering work. Publication/tag status not established by the local version number.
- Current renderer explicitly skips label features. Existing packs or a provider URL alone cannot supply city labels on Tab5.
- Existing host builders already produce world-overview and regional PMTiles/manifest/checksum triplets. Reuse and validate them before creating another builder.
- OrcMaps world-overview-1 release was read-only verified as a draft. Its overview archives are not a published regional catalog or a worldwide street-detail map.
- OrcSDR's signed historical data catalog is distinct from arbitrary OrcMaps regional discovery; legacy ORCMAP1 is distinct from PMTiles.

## Global constraints

- Protect main/master in both repositories; preserve all current work, logs and recovery images. Use scoped branches/worktrees and review existing integration work before overlapping changes.
- New OrcSDR worktrees, if needed, belong under F:/AI/OrcSDR-TEMP/. No automatic rebase, force push, merge, release or publication.
- COM17 is the initial P4 acceptance device; retain C6 3.0.6, NVS and SD data. COM39 retains C6 1.4.1 and is untouched unless a later comparison test is explicitly selected.
- Preserve RTL driver 0.9.3, full 40 KiB DMA reserve, existing display ownership and receiver servicing. Avoid unrelated main.cpp/DSP/USB/audio/Wi-Fi restructuring.
- Embedded overview must work without SD or Wi-Fi. Regional download failure must not remove working maps.
- Setup uses its own pages and inputs, while sharing backend services. No jump into Settings for Wi-Fi or location.
- Target Tab5 typography: 40 px headings, 32 px main text, 24 px secondary text; main touch targets approximately 80 px high. Verify on the physical 1280x720, 5-inch panel.
- Respect source licenses and attribution; preserve source dates, hashes, derivation and compatibility metadata.

## Review focus

- Label density and county geometry vary with zoom; a tile file may omit required features even when its declared zoom range is sufficient.
- Cached motion can hide stale pixels or wrong projections. Basemap, labels and overlays must share one camera and generation.
- Map memory must remain bounded in PSRAM and must not recreate the DMA shortages affecting SD and pairing.
- Downloads must handle interrupted power/network, corrupt files, missing/removed/full SD cards and server changes without activating partial content.
- Interrupted setup, cancellation and migration must retain existing location, map choice, credentials and unrelated settings.

## Phase 1 — Establish a reproducible quality and performance baseline

Files: focused evidence notes in both repositories; existing pack-builder manifests/profiles and benchmark tooling. No renderer redesign yet.

- [x] Review the exact 0.2.3 -> 86e27a0 engine changes, APIs, tests and hardware evidence; distinguish reusable engine cache APIs from CYD-demo-specific logic. Shared cache reviewed; bounds regression corrected separately; CYD evidence is synthetic and not Tab5 proof.
- [x] Inventory the existing OrcMaps world and regional builders. Keep all pack-building and catalog tooling in OrcMaps; do not create an OrcTiles repository. Inventory recorded in the worldwide-base assessment and OrcMaps builder documentation.
- [ ] Verify available worldwide source datasets and existing Oregon/Springfield fixtures for place names, administrative boundaries, roads, rivers and land cover. Save exact source and pack hashes; do not broadly inspect large fixtures.
- [x] Build/inspect a representative global regional-detail candidate before setting a download budget. Measure which zooms retain county-equivalent boundaries and towns: an overview archive with larger zoom metadata is not sufficient. Actual z0–8 candidate and z7/9 detail samples inspected; county kinds present in eight regional z8 samples. This does not establish production schema compatibility or complete worldwide quality.
- [x] Define a small worldwide acceptance set spanning continents, dense/sparse areas, an island/antimeridian case and international scripts; include Oregon regional, Lane County and Springfield/local as repeatable reference views, not release-coverage limits. Record expected names and boundaries at each. OrcMaps `tools/pack-builder/global-base-acceptance.json` defines eight views, expected place alternatives, boundary context, zooms and acceptance limits.
- [x] Capture current Tab5 map timings/memory and active-radio counters using bounded tests. COM17 baseline recorded above; gesture percentiles remain a Phase 3 requirement. No overnight soak requirement.
- [ ] Set final gesture/render/label budgets from measured baseline and the user's quality expectations.

**Gate:** Reproducible source/pack/view inputs, documented gaps and measurements. No assumption that a later engine version or higher zoom pack alone solves quality.

## Phase 2 — Add labels and geographic styling in OrcMaps

Primary files: src/render/renderer.cpp, include/orcmap/render_target.hpp, style and feature/classification interfaces; focused new label layout implementation/tests; existing host/M5GFX adapters. Final API shape follows Phase 1 review.

- [x] Implement focused host coverage for bounded place-label extraction/layout, English-first selection, priority, clipping, overlap and UTF-8 handling; merged in PR #6. This does not establish worldwide font acceptance.
- [ ] Prove supported-script/fallback behavior across the worldwide views, including visible missing-glyph handling. International names remain a first-release global-base requirement.
- [x] Add the minimum graphics-independent text/measurement capability and label placement needed for settlements and selected regional labels. Merged in OrcMaps 722d0d3; broader script/quality acceptance remains open.
- [ ] Retain and distinguish country/state/county boundary information; style states and counties visibly at appropriate zooms.
  Shared-engine source candidate 3d9d01c and focused profile/style checks exist;
  full regressions, real reference renders and panel acceptance remain open.
- [ ] Preserve useful road hierarchy, water, forests/parks and quiet colors beneath bright radio overlays.
- [x] Use bounded PSRAM-backed candidates/scratch on P4; avoid per-label unbounded allocation. Fixed pool linked in external BSS; tile RenderScratch reused. Runtime memory headroom remains open. No full MapLibre style interpreter.
- [ ] Add real-geography comparisons at the fixed views, plus malformed/oversized-name and dense-city tests. Maintain miniz and polygon-ring gates.
- [ ] Verify CYD/other existing adapters remain functional where interfaces change; do not claim Tab5 performance from desktop/CYD tests.

**Gate:** Readable, deterministic labels and required boundaries in reference renders; compatibility checks pass; bounded allocation failure is safe.

## Phase 3 — Prove fluid interaction on the Tab5

Primary files: reviewed OrcMaps camera/cache APIs and OrcSDR ui/map_pipeline.cpp, ui/map_service.cpp and map picker integration.

- [x] Integrate the verified engine through the component pin and regenerated lock. Earlier 63f0573 is device-tested; merged 722d0d3 is pinned at OrcSDR 04b118a4 with focused consumer checks. Never edit managed_components sources.
- [ ] Native-build and hardware-validate the merged pin; source/host success does not close this gate.
- [x] Reuse rendered geography during gestures through a bounded PSRAM completed-frame snapshot and shared camera transform. COM17 physical dragging and low-memory fallback passed; continuous timing remains open.
- [x] Keep the map visible during pending redraw and hide city names below zoom 6. User accepted the opening view; closer-label presence is host-tested and still needs real-global-pack device acceptance.
- [ ] Cancel superseded requests; prioritize the newest viewport and keep final labels/overlays aligned with it. Preserve single UI ownership of framebuffer pushes.
- [ ] Measure touch-to-feedback and p50/p95/max visible-update intervals, cache hit/miss behavior, render/SD/transfer time, PSRAM, free/largest internal DMA block and cancellation response.
- [ ] Target roughly 33 ms visible motion updates on cached pan/zoom; treat this as a hardware target, not a claim that every uncached tile can render in 33 ms. Agree numerical pass gates after Phase 1.
- [ ] Repeat tests while receiving, monitoring IQ/audio loss, USB errors and resets. Exercise HOME <-> RF Lab <-> maps/Settings transitions and confirm no stale screen content.
- [x] Pass bounded retained-map 8 KiB SD write/read/hash and snapshot reclaim/recreation checks on the earlier COM17 candidate.
- [ ] Repeat retained-map SD and pairing headroom checks on the final candidate against explicit memory budgets.

**Gate:** User accepts physical map quality and interaction; measured budgets and radio/SD continuity pass on COM17. Otherwise optimize the measured bottleneck before expanding delivery scope.

## Phase 4 — Own the pack catalog and delivery pipeline

Primary files: existing OrcMaps pack builders/verification, a small catalog schema/publisher, and a shared OrcSDR map acquisition adapter with focused storage/download tests.

October 19 continuation: the existing 543,380,195-byte z0–8 candidate now has
immutable pack metadata/checksum. Its full local hash matches the recorded
extract; both the current OrcMaps host tool and OrcSDR's exact pinned component
accept one pack, rejecting zero. See OrcMaps
`docs/evidence/2026-10-19-global-candidate-pack-preflight.md`; consumer logs are
`global-pack-consumer-{configure,build,verify}-20261019.log` in this worktree
root. The verifier's "installed" count is host discovery, not a device install.
The upstream full-source hash remains unverified, explicitly recorded in
provenance. No new download, SD write, flash or publication occurred.

The existing OrcMaps host provisioner at `862a1ad` was hardened against overwriting packs,
missing manifests, corrupt copied bytes and partial activation. Its focused
Python suite passes 24 tests; evidence is
`docs/evidence/2026-10-19-provision-staging-safety.md` in OrcMaps. This is not the
device resumable installer: power-loss durability, orphan recovery and actual
SD-card testing remain open. The older signed data catalog and 64 MiB serial
uploader remain separate; neither is being relabeled as PMTiles delivery.

OrcMaps `83695cd` now provides the host-side owned catalog builder and
`tools/pack-builder/MAP_CATALOG.md` contract: full engine manifests,
content-addressed HTTPS mirrors, size/hash, bounded revision/UTC metadata,
canonical payload and verified detached P-256 signature. Nine focused catalog
tests pass. A temporary-key smoke run used the actual global archive and
reserved `example.test` URLs; it is explicitly not a production catalog.
The builder's dependency on strict archive preflight also exposed and fixed
the host verifier returning success after an invalid-header warning; two
actual CLI tests now pass for valid and rejected header/coverage/zoom cases.
See OrcMaps `docs/evidence/2026-10-19-map-catalog-builder.md` for commands/logs.
No production key, endpoint, publication, firmware pin or device state changed.
Next delivery step: implement the bounded authenticated catalog consumer,
freshness/rollback policy and shared asynchronous HTTP/SD resume worker, then
wire it to wizard-owned downloads. These device gates remain open.

Subsequent source checkpoint `06c7a4fb` implements that bounded catalog consumer
and checks signatures before JSON interpretation. It validates the actual
engine manifest, entries/URLs, expiry and the caller's accepted revision/hash,
leaving previous output unchanged on rejection. Twenty-nine signed host cases,
actual OrcMaps producer-payload interoperability and P4 syntax pass. No complete
native link, device test or HTTP/SD/NVS caller is present yet. The consumer
requires a caller-supplied approved public key and trusted UTC; unavailable UTC
disables new downloads, not already-installed offline maps. OrcMaps `182c56d`
aligns producer URL/date/structure limits; its ten focused builder tests pass.
Commands and log paths: [signed consumer evidence](../../evidence/maps/2026-10-19-signed-map-catalog-consumer.md).
Next: implement bounded catalog fetching, persistent accepted revision/hash and
the shared archive resume/activation worker, then wizard download controls.
Production key/endpoint, memory budgets and hardware recovery remain open.

Source checkpoint `f80c2980` adds hidden resumable archive staging and an
ESP-IDF HTTPS transport. The worker-side core uses checked alternating resume
records, synced prefixes, response identity checks, SD read-back SHA-256 and
actual PMTiles header/root validation. It prepares files under a hidden
subdirectory; it deliberately does not publish an installed manifest or replace
an old pack before recovery-safe activation exists. The only shared-storage
addition is shrink-only `File::truncate`, used on owned staging tails.
Transfer CTest 1/1, existing SD DMA-buffer regression 1/1 and P4 syntax 3/3 pass.
Actual TLS/FAT/hardware allocation/radio behavior remain untested; no Tab5 was
accessed. See [transfer staging evidence](../../evidence/maps/2026-10-19-map-transfer-staging.md)
and [shared SD reliability record](../../SD_CARD_RELIABILITY.md).
Next: implement journaled activation/version retirement and recovery, wire
catalog acquisition/persistent trust/worker state, then test the complete
download workflow before offering it in the wizard.

- [ ] Define pack entries: stable ID/name, bounds/coverage, detail/zoom, schema compatibility, source/build date, download bytes, SHA-256, attribution/license, immutable URL and mirrors.
- [ ] Sign trusted catalog metadata using a reviewed approach consistent with existing project trust practices, without confusing the historical data catalog and map manifests.
- [ ] Build one logical global regional-detail base and optional local-detail packs. Reuse identical versioned artifacts across users, validate them, and publish only after approval. Recommend local packs by location without generating a unique pack for every user by default.
- [ ] Measure representative pack sizes/build cost. Model storage, reads and delivery for 20,000 users before choosing hosting; keep catalog URLs/provider replaceable.
  Candidate-size traffic and request-cost scenarios are now recorded in the
  [delivery sizing note](../../evidence/maps/2026-10-19-global-pack-delivery-cost.md).
  Production build cost, optional-pack sizing, provider selection and endpoint
  validation remain open; no hosting or publication has been performed.
- [ ] Implement asynchronous resumable install with explicit size/free-space checks, bounded buffers, verified TLS/hash, staging and atomic activation preserving previous packs. Resume must validate object identity, not append unrelated versions.
  [Portable response/resume guard](../../evidence/maps/2026-10-19-map-download-response-guard.md)
  now passes focused host checks (1/1) and standalone P4 compiler syntax. It has
  no production call sites yet. Trusted metadata, HTTP/SD adapter, body/hash
  verification, persistence and activation remain open; downloads are not enabled.
- [ ] Test short/corrupt/wrong-schema downloads, cancellation, network/power interruption, full/removed SD, version replacement and hash mismatch.
- [ ] Keep manual import of compatible packs available without our hosting service. Preserve explicit legacy support/migration.

**Gate:** The global regional-detail base passes the worldwide acceptance set and can be downloaded, verified and used offline alongside optional local packs. Actual pack size and 20,000-user delivery costs are recorded. Failed/interrupted downloads preserve working data; hosting/publication approvals are complete.

## Phase 5 — Finish the self-contained branded setup

Files: preserved ui/setup_ui.hpp/.cpp work, map_screen, small backend hooks, focused host tests and setup persistence.

- [x] Pause reception throughout setup and restore the prior active/stopped state on exit in bounded host/device tests; user accepted the notice and responsiveness.
- [x] Preserve the original graphics state across repeated Settings opens; host/device navigation checks and physical Home graphics acceptance passed.
- [ ] Verify previously active audio restoration, physical hotplug while paused and automatic first-boot entry.
- [ ] Reconcile the prepared UI work with the approved branded preview and the real acquisition API; preserve its passing tests.
- [ ] Complete Welcome -> Wi-Fi -> Location -> Maps -> Download -> Ready, with wizard-owned password/address/coordinate inputs, Back, Skip, cancellation and interrupted recovery.
- [ ] Present network location as approximate; confirm each proposed location before saving. Show the saved point clearly.
- [ ] Offer the global regional-detail base as the standard download, then recommend optional local packs after location selection. Clearly distinguish built-in overview, global regional base and local detail; show size/status/progress/error/retry and no-SD/offline states.
- [ ] Reuse the actual Orc badge/palette. Test font/layout/targets at native resolution and physical size; keep the selected fresh-install orientation.
- [ ] Test true first boot only with fresh verified backups and explicit erase authorization; test rerun without erasure as well.

**Gate:** A user can complete all setup methods without leaving the wizard; saved location, chosen maps and interrupted progress survive reboot; offline/no-SD paths remain usable.

## Phase 6 — Expand optional local coverage and integrate reviewed work

- [ ] Expand optional higher-detail local packs through the proven build/catalog process. Worldwide regional-base acceptance and international-name handling must already pass before first-release claims.
- Deferred beyond this completion goal: custom-area build queues and selected online provider adapters. No generic “any map API” promise.
- [ ] Run the narrow relevant regression matrix, expanding shared-engine checks where shared infrastructure changed.
- [x] Review and merge OrcMaps PR #6 with explicit approval; verify merge 722d0d3 on fetched origin/main.
- [x] Record the merged engine pin and regenerated consumer lock at OrcSDR 04b118a4 with focused checks.
- [ ] Reconcile preserved OrcSDR edits, complete applicable acceptance gates, prepare a reviewable change and obtain explicit OrcSDR merge approval.
- [ ] Verify the OrcSDR merge on remote main before claiming integration landed. Tags/releases/publication remain separately approved actions.

## Reporting and stop points

Each phase reports files/commit, exact tests/results, device, saved log paths, remaining blockers and next milestone. Save hardware logs beside the invoked test program/script. Stop for missing required input, paid deployment/publication, destructive operation or a new expensive build/test cycle outside the user's approved scope. Never mark an untested phase complete.
