# Drone Watch — developer handoff

**Prepared:** 2026-10-26; **updated 2026-10-28 PT:** W01 is host-tested; P1 bounded OpenDroneID message/pack parsing and source-aware event history now have passing host CI (3/3). Hardware acquisition and native build remain pending.  
**Proposed implementation repo:** `hardcoreerik/OrcSDR`.  
**Design baseline read:** `main` commit `07c777ca05af4bcbf8af6074dbc0b17d9a891af1`; recheck against current `origin/main` when coding starts.  
**Private research:** [REA-Results draft PR #3](https://github.com/hardcoreerik/REA-Results/pull/3); committed inbox source survey + 15/15 synthetic test record. Follow REA-Results `AGENTS.md` for all new findings.  
**Execution state:** W01 is wired and host-verified as an unavailable/empty-state screen. A reusable Remote ID protocol decoder and bounded event history are now host-tested. No live BLE/Wi-Fi RF acquisition, physical rendering acceptance or other 24 screens are completed.

## Mandatory screen reference before any UI implementation

Use [SCREENS.md](SCREENS.md) to find the named screen and its per-view design spec. [UI_UX_STANDARD.md](UI_UX_STANDARD.md), [NAVIGATION.md](NAVIGATION.md), [DATA_CONTRACTS.md](DATA_CONTRACTS.md) and [RENDERING_AND_ACCEPTANCE.md](RENDERING_AND_ACCEPTANCE.md) apply to **all 25** and must be read with the current repository `apps/orcsdr-tab5/ui/AGENTS.md` and `docs/UI_RENDERING_STANDARD.md`.

The five categories are **internal routes within one dashboard**. Do not create 25 top-level DashboardRegistry entries or 25 ScreenController owners. Build only capability-gated working screens: do not fill not-yet-implemented MEDIA or C6 modes with simulated live data.

## First implementation work package (P1): decoder contract, no UI or C6 modifications

**Owner requested continuing on this existing branch, not creating another branch.** P1 core is now implemented and host-tested here. Keep future P2 receiver acquisition separate until qualification:
1. Inspect `docs/drone-watch/DESIGN.md`, `PHASING.md`, `VALIDATION.md` and `Roadmap.md`, plus `apps/orcsdr-tab5/ui/dead_air_model.*` for fixed-memory event patterns.
2. Retrieve and pin `opendroneid/opendroneid-core-c` at research SHA `849c03adbe0cdd68997a462fafab397edcfe53f3`, or explicitly choose a newer revision and re-evaluate licensing/API/fixtures before use. The official library is Apache-2.0; preserve notices and attribution. **Do not copy Kismet GPL-2.0 implementation or Dronetag unlicensed Flutter plugin.**
3. Start with deterministic failing host tests against upstream message vectors, not synthetic-only evidence-label tests. Add invalid length, endian, invalid-location sentinels, encoded message type/version, unsupported and malformed. Assert inputs cannot crash or create false geographic claims.
4. Implement a bounded, UI-free `drone_watch_model` and thin `drone_remote_id` wrapper behind a mockable `SourceAdapter`. Prefer exact packet timestamps/capture metadata over building a speculative drone classifier.
5. Supply a **single** host test invocation (document it in `tests/drone_watch/README.md`), record logs, sanitizer outcome or NOT RUN, and build a minimal native ESP-IDF consumer only after host fixtures pass.
6. Keep changes in the existing Drone Watch draft PR #213 and update the dated evidence and CI links. Do not advertise live reception until a hardware adapter is RF-verified.

**P1 progress:** host parser and bounded model tests PASS. The required native ESP-IDF consumer build, sanitizer run, complete multi-message conformance, C6/RF validation and unchanged radio regression have NOT RUN; this is not a P1/G1 full closeout.

## Separate hardware scout (P2), nonproduction Tab5 only

- Read `docs/user-guide/wifi-tab5.md` and `docs/validation/esp-hosted-c6-compatibility-2026-09-29.md`; inspect the installed `apps/orcsdr-tab5/ui/wifi_service.cpp` and ESP-Hosted HCI configuration.
- Enumerate actual existing C6 capabilities: (a) BLE4 advertising, (b) BLE5 extended advertising, (c) BLE5 coded PHY, (d) 802.11 beacon vendor elements and (e) Wi-Fi NAN. **Five distinct outcomes**; lack of feature support is not automatically a driver defect.
- Work with a known test device broadcasting an owned/authorized Remote ID advertisement. Save full setup, version, raw advertising bytes, RSSI if available, packet/drop counts and coexistence behavior. Compare reference scanner when practical.
- Keep Wi-Fi scan/catalog and C6 reset/update/recovery unchanged. The documented Wi-Fi flow currently pauses SDR; observe and label coverage gaps rather than trying to remove them in this phase.
- Do not flash or switch C6 firmware without explicit separate approval after verifying M5 Launcher/factory compatibility; if Hosted HCI is absent, select the OrcNode/Android passive receiver fallback.

## First UI pass (P3), after P1 but independent of P2 result

- Add `drone_watch_app`, `drone_watch_dashboard` and registry/screen routes only. App snapshot API should accept a test replay reader so UI work can advance without pretending physical detection works.
- Screens: WATCH, AIRCRAFT and SOURCES first; use the existing shared dashboard header and single ScreenController framebuffer owner.
- Ensure **TEST DATA** is on every replay UI surface; production must not silently show demo aircraft.
- Later integrate OrcMaps only through existing shared service, with age and unverified broadcaster-claim labels; no unrelated map refactor.
- Test on Tab5 1280×720 and reflow 720×720 on the adaptive UI work. The 5-inch screen is the readability benchmark; avoid densely packed tactical-style panels.

## Standard local development / verification

1. `git fetch origin`, inspect current `origin/main` and applicable repository instructions; create your own `codex/`, `claude/` or `chatgpt/` feature branch from the reviewed baseline, not a published release tag.
2. Review `apps/orcsdr-tab5/tools/build-tab5-idf.ps1`, `apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1`, `tools/` and active native ESP-IDF 5.5.4 build instructions. Run and log relevant baseline tests before changes.
3. Write P1 tests, then code. Run exact host target, sanitizers if available and `git diff --check`; preserve stdout/stderr in the directory from which you ran the commands.
4. Run native build via the current maintained script, recording exact firmware image SHA-256, dependent version pins, app free-space and DMA/internal heap. A compilation is only Build-Verified.
5. Flash a spare/approved Tab5 **only after passing build/tests and receiving device-specific approval**; capture serial logs, exact physical receive result and regression counters. Never silently flash C6 or apply an unreviewed OTA update.
6. Run baseline FM/RDS, ADS-B, LoRa, Wi-Fi scan/connect/catalog, SD, Dead Air, OrcMaps and dashboard navigation regressions. Work around existing test-suite constraints by labelling gaps, not assuming success.
7. Commit, push a branch and open a draft PR with actual `PASS/FAIL/NOT RUN` matrix. Keep generated research findings in **REA-Results inbox on a separate branch**; do not hand-edit REA-generated indexes. Don't claim every REA target has been analyzed when only source was inspected.

## Suggested branch/PR units (not created in this planning session)

| Unit | Branch suggestion | Done when |
| --- | --- | --- |
| P1 | `codex/drone-watch-rid-core` | decoder + host fixtures, no C6/UI |
| P2a | `codex/drone-watch-c6-feasibility` | passive BLE/wifi capability matrix and physical logs |
| P3 | `codex/drone-watch-dashboard-shell` | readable host-replay UI, clearly TEST DATA |
| P4 | `codex/drone-watch-live-rid-maps` | actual verified source + OrcMaps reported positions |
| P5-P8 | independently scoped | optional journal, OrcNode, RF activity, compatible analog video, acoustic experiments |

These are naming examples, **not live branches**.

## Risks and product decisions to resolve

- **C6 Hosted access:** missing HCI/monitor frames, Wi-Fi competition, firmware version mismatch; choose fallback instead of harming stable Wi-Fi.
- **No universal drone video:** external analog front end and digitizer must produce frames before the screen can offer `VIDEO`; supported digital links are not universally decodable.
- **False location assurance:** Remote ID fields are transmitter claims. Never hide missing/invalid data or suppress staleness.
- **Memory/USB load:** no simultaneous dongle/video claim before actual hub/power/DMA tests on P4.
- **Map integration baseline:** evaluate current OrcMaps version/label preferences; don't assume separate branches have merged.
- **Storage privacy:** sensitive operator locations or raw audio should not be archived automatically.
- **Commercial/copyleft:** review any new source being bundled and its commercial distribution implications (official OpenDroneID C vs Kismet vs RX5808 references).
- **UI availability:** should Drone Watch open if all inputs are unavailable? Suggested yes, with a clear `CONNECT A COMPATIBLE SOURCE` state; confirm in product review.
- **V1 source path:** onboard C6 if P2 proves it, otherwise independent OrcNode/phone; never force all customers to use special C6 firmware without clear docs.

## Planning PR closeout criteria

The *planning* PR is ready for review when README/DESIGN/PHASING/VALIDATION/HANDOFF link correctly, no source code has changed, no current capabilities were misrepresented, and every future phase specifies an exit gate. This does **not** satisfy any implementation phase or hardware gate.
