# LilyGO unified P4 completion ledger

This ledger tracks the full remaining goal. A renderer, host test, successful
flash or profile declaration is not hardware acceptance. Earlier checkpoint
documents remain evidence for their named slices, not current completion lists.

Target: `codex/p4-display-integration`, worktree
`F:\AI\OrcSDR-TEMP\lilygo-display-integration`. Main remains protected.

Legend: **partial** means implemented or checked only for the cited scope;
**open** means required work/evidence is missing. No row is closed by intent.

| Requirement | Implementation / software evidence | Device / human evidence | Remaining gate |
| --- | --- | --- | --- |
| Preserve Hosyond/LilyGO drivers and shared M5GFX UI | Existing profiles and shared renderers retained | Working LilyGO app, earlier display/FM acceptance | Cross-profile regressions and final audit |
| TX minimum text readability | `521ed01a`; normal + sanitizer TX suites; minimum-height/bounds/overlap assertions | r17 PNGs; owner accepted portrait and landscape text | Wider populated-state and input acceptance |
| LoRa RX views/navigation | Legacy renderer exists; native RX migration open | Native TX entry only | Shared geometry for all five RX views and six-tab capable navigation |
| P25 | Shared responsive renderer imported in `52e73c5c` / `0743c00b`; targeted layout tests pass; absent P4 route | Open | Adapter, decoder services, PCM output ownership and RF acceptance |
| FT8 | Shared renderer linked; route/runtime missing | Open | Responsive renderer, runtime/time/storage, adapter, reception |
| SSTV | Shared renderer/app exist; not in P4 app | Open | Responsive renderer, adapter and worker/audio/storage hooks |
| Dead Air | Shared renderer linked; app/route missing | Open | Runtime/worker/storage/audio hooks and responsive UI |
| Satellite | Existing Tab5 behavior is generic receiver preset | Open | Preserve and verify that behavior; do not claim an absent satellite decoder |
| Weather/Marine | Existing generic receiver preset fallbacks | Open | Audit tuning/mode/controls against existing Tab5 behavior |
| Dashboard browser/overlays/editors/focus | Partial shared migrations and focused tests | Sampled keypad/Settings acceptance only | Every workflow, populated/error states, both orientations |
| Reusable profile geometry and capability rules | Shared display/header/layout contracts exist | Multiple native dashboards run | Complete remaining fixed-coordinate assumptions and document authoring process |
| SX1262 integration | Pinned Semtech core + profile SPI/expander HAL and TCXO/calibration checks; focused failure tests pass | Three consecutive XOSC checks passed at programmed 1.6 V/5 ms, retaining initial `0x0020` and reporting zero new errors; owner confirms antennas/906.875 MHz and Heltec peer | Intermittent initial probe readback, RX and RF-path qualification, bounded TX test and UI/service integration; TX remains locked |
| GNSS | L76K pins documented; no service | Open | Passive UART parser/service, fresh valid fixes, location integration; wake polarity unresolved |
| Battery telemetry | Read-only BQ27220 worker, profile/bus ownership and shared M5 power provider integrated; 8 service tests, Home recording and 7 header/keypad tests pass | COM32 fresh samples: 22%, 3963–3964 mV; portrait/landscape r18 PNGs inspected | Physical percentage accuracy, unplug/no-cell behavior, sustained coexistence and remaining per-page live refresh |
| USB/RTL-SDR/audio | Existing shared radio/audio services run | FM previously audible; bounded streaming checks | Sustained workload/reconnect/reception/audio/drop measurements |
| Storage and input | SD mounted; native touch and IMU work | Sampled physical touch/rotation acceptance | Service actions, error/reconnect cases and all gesture paths |
| Settings actions | Connectivity/C6/storage slices implemented | Sampled Settings/C6 acceptance | Audit and wire all remaining advertised actions |
| Maps/catalog services | Existing maps infrastructure retained | Partial captures only | Actual catalogs/map actions, failure/offline behavior and persistent state |
| ADS-B metadata | Shared dashboard present | Layout captures only | Metadata backend, populated display and actual reception |
| LoRa logs/history/messages/live IQ | RX pipeline exists; TX UI states unavailable backend truthfully | One Heltec Meshtastic transmission observed as an RTL-SDR RF burst in r19 native PNGs; no message decode proven | CRC/message reception, real persistence, composition/activity and independent IQ wiring |
| Shortwave hunt/DSP/storage | Shared action definitions; P4 marks unsupported functions unavailable | Layout captures, no service acceptance | Wire supported services and verify actual results |
| Existing M5GFX/WASM web UI | Auth/server/control modules integrated; state and persistence tests pass; bundle not yet packaged | Real HTTP auth, rejection/replay checks, signed Home step-up/down and applied-frequency receipts passed | Existing assets, other shared routes, native password editor, browser workflows and audio/IQ |
| Automatic/manual rotation | Profile-aware IMU logic and persistence implemented | Clockwise correction accepted | All four poses, gesture protection, fallback and persistence matrix |
| Update-time rotation locking | Software guards exist | Open | Physical in-app critical-phase locks |
| Every page PNG and readable populated UI | Multiple capture passes and recording tests | Sampled human acceptance | Remaining pages/overlays, populated/empty/error states and touch/focus checks |
| Wi-Fi startup/recovery/coexistence | Saved-network and boot toggle fixes tested | Several soft boots and automatic joins passed | Occasional initial retry, cold boot, wrong password/AP outage and long coexistence |
| Bounded redraws/resources | Recording regressions on migrated slices | Short counter/heap snapshots only | Measure remaining views and sustained PSRAM/DMA/redraw/audio/receiver behavior |
| Installed C6 Hosted-to-Hosted ROM-SDIO | Implemented and tested | Owner completed embedded 3.0.6 installation without UART | Preserve matching package/version metadata |
| Installed-app factory ESP-AT conversion | Prototype proof exists separately | Not qualified in installed app | Full real installed-flow qualification; prototype is insufficient |
| Interrupted C6 recovery/errors | Software guard/cleanup tests exist | Open | Recoverable interrupted-write, timeout/cancel/corrupt/wrong-target/verify/restart tests |
| Appropriate updates on other profiles | Existing update methods retained | Open | Cross-profile regressions and package compatibility |
| Public recovery package provenance | Pinned package metadata/generator exist | Prior installation slice only | Final hashes/ranges/version/install checks; never include private device snapshots |
| Profile flags/acceptance records | Some flags/older CSV remain stale | Open | Update only to supported evidence; preserve genuine limitations |
| Unified package/manifests/install artifacts | Existing build scaffolding | No qualified unified release artifact | Profile-driven builds, source/hash/flash/C6 provenance, installation and boot validation |
| Tab5/Hosyond regressions | Representative host geometry tests, partial renderer coverage | Physical regression gates open | Focused software/build/device tests without disturbing other worktrees |
| Named serial parity | Several UI-owner queues exist | Sampled command/device checks | Complete changed actions, same validation/persistence/auth and observable outcomes |
| Final requirement audit | This ledger preserves scope | Open | Inspect authoritative evidence for every row; no merge/release without approval |

## Dependency-ordered execution

1. Preserve accepted TX text (committed), then migrate missing shared layouts
   and establish real P4 adapters one route at a time. LoRa RX navigation and
   P25 are first; preserve state/services on rotation.
2. Build profile-driven hardware seams and bounded workers. Start with read-only
   battery telemetry and passive GNSS. SX1262 is confirmed by owner, not yet probed.
3. Integrate existing web modules selectively against current Home/radio APIs.
   The parent owns shared UI-owner dispatch, startup, build and package wiring.
   A temporary auth/Home substrate is a dependency slice, not full web parity.
4. Complete service/backend actions, then reception/error/rotation/cold-boot and
   sustained-load acceptance. Record required physical dependencies explicitly.
5. Qualify installed recovery and final profile-driven artifacts, perform
   cross-profile regressions, then audit every requirement before completion.

## Current isolated tasks

- GPT-Luna P25: `lilygo-p25-responsive-20261102`; shared renderer/layout/tests only.
- GPT-Luna web: `lilygo-web-integration-20261102`; web modules/tests only; parent
  retains CMake, startup, native owner APIs and bulk-asset/package coordination.
- GPT-Luna battery: `lilygo-battery-driver-20261102`; read-only driver/protocol/tests;
  parent retains profile, bus, polling service and UI integration.

No agent accesses COM32, flashes, pushes, merges or edits another task's files.
The parent reviews/imports commits and performs builds/device acceptance.
