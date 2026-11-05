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
| LoRa RX views/navigation | Five existing receive views now share viewport-derived geometry, paged node/traffic lists, spectrum/waterfall bounds and 5/6-tab capability navigation; TX remains shared; sanitizer production renderer checks pass | r21/r22 native PNGs for five RX views plus TX Lab in both orientations; one 124-byte SX1262 packet populated Traffic/Nodes; on-device serial zoom/receiver-center applied; final r22 hashes/ELF verified | Final-build populated sentinel recapture, real logs/history, complete serial parity and remaining overlays |
| P25 | Shared responsive renderer and P4 control-channel monitor in `fc5d6f5e`; targeted layout/core tests and combined LilyGO build passed | Live 453.925 MHz control-channel grants, authenticated tune applied, native portrait and landscape Monitor PNGs | Other tabs and touch acceptance, profile management, spectrum feed, voice follow/PCM output, sustained receiver headroom |
| FT8 | Shared renderer linked; route/runtime missing | Open | Responsive renderer, runtime/time/storage, adapter, reception |
| SSTV | Shared renderer/app exist; not in P4 app | Open | Responsive renderer, adapter and worker/audio/storage hooks |
| Dead Air | Shared renderer linked; app/route missing | Open | Runtime/worker/storage/audio hooks and responsive UI |
| Satellite | Existing Tab5 behavior is generic receiver preset | Open | Preserve and verify that behavior; do not claim an absent satellite decoder |
| Weather/Marine | Existing generic receiver preset fallbacks | Open | Audit tuning/mode/controls against existing Tab5 behavior |
| Dashboard browser/overlays/editors/focus | Partial shared migrations and focused tests | Sampled keypad/Settings acceptance only | Every workflow, populated/error states, both orientations |
| Reusable profile geometry and capability rules | Shared display/header/layout contracts exist | Multiple native dashboards run | Complete remaining fixed-coordinate assumptions and document authoring process |
| SX1262 integration | Profile-aware LongFast RX worker, manual MMCX1 selection, driver-reported SPI chunk limits, generation/error handling and shared Messages adapter; 70 packet/limit cases, seven worker scenarios, event mapping and sanitizer UI checks pass | UI START RX reaches receiving; Heltec V4 packet at 906.875 MHz accepted by SX1262 (120 bytes, CRC, text, RSSI -41 dBm/SNR 7 dB), displayed with SX1262 attribution in native portrait/landscape PNGs | Onboard TX/output qualification and approval, atomic C6 quiescence, hardware error/reconnect qualification, logging/history and remaining LoRa views; TX remains locked |
| GNSS | Bounded original NMEA parser `34c71ba8`, RMC/GGA checksum/range/framing/no-fix tests pass; estimated/manual/simulated modes excluded | Corrected wake-high/RX22 receives framed NMEA at 115200 without checksum/UART errors; initial no-fix reports | Fresh satellite fix, location integration, loss/staleness and cold-boot qualification |
| Battery telemetry | Read-only BQ27220 worker, profile/bus ownership and shared M5 power provider integrated; 8 service tests, Home recording and 7 header/keypad tests pass | COM32 fresh samples: 22%, 3963–3964 mV; portrait/landscape r18 PNGs inspected | Physical percentage accuracy, unplug/no-cell behavior, sustained coexistence and remaining per-page live refresh |
| USB/RTL-SDR/audio | Existing shared radio/audio services run | FM previously audible; bounded streaming checks | Sustained workload/reconnect/reception/audio/drop measurements |
| Development diagnostics | Shared native `RTL_HEALTH`/BUILD/authenticated HEAP_CHECK; bounded private serial/build capture; four focused host tests; P4 core-dump build and unchanged existing partition entries verified | COM32 app/table hash-verified; 35-second capture matches the device ELF SHA-256, reports `coredump=flash` and advancing uptime; RX streaming/audio task running/Wi-Fi connected; unauthenticated heap scan denied | Deliberate panic/dump retrieval qualification; Tab5/Hosyond build and physical regressions; earlier unrecorded restart cause remains unknown |
| Storage and input | SD mounted; native touch and IMU work | Sampled physical touch/rotation acceptance | Service actions, error/reconnect cases and all gesture paths |
| Settings actions | Connectivity/C6/storage slices implemented | Sampled Settings/C6 acceptance | Audit and wire all remaining advertised actions |
| Maps/catalog services | Existing maps infrastructure retained | Partial captures only | Actual catalogs/map actions, failure/offline behavior and persistent state |
| ADS-B metadata | Shared dashboard present | Layout captures only | Metadata backend, populated display and actual reception |
| LoRa logs/history/messages/live IQ | Hardware RX worker and shared payload/message adapter integrated; data views retain receiver identity and hide unverified text; unavailable export/log actions disabled | Heltec CRC/message receive and Messages display proven in r20; r21 RX pages captured empty | Real persistence, message composition/TX qualification, populated current RX views and Dual Radio independent IQ wiring |
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
   battery telemetry and passive GNSS. SX1262 receive is now proven; TX remains locked.
3. Integrate existing web modules selectively against current Home/radio APIs.
   The parent owns shared UI-owner dispatch, startup, build and package wiring.
   A temporary auth/Home substrate is a dependency slice, not full web parity.
4. Complete service/backend actions, then reception/error/rotation/cold-boot and
   sustained-load acceptance. Record required physical dependencies explicitly.
5. Qualify installed recovery and final profile-driven artifacts, perform
   cross-profile regressions, then audit every requirement before completion.

## Imported isolated work

Earlier P25, web and battery worktrees are retained for provenance; their accepted
changes are imported. Their remaining adapter/service/hardware gates are listed
above. Parent owns shared interfaces, build wiring and COM32. New independent
Luna tasks receive narrow file ownership; no agent flashes or accesses the device.

## 2026-11-03 onboard RX / diagnostics checkpoint

- Shared Messages rendering remains one M5GFX implementation. Explicit START,
  CANCEL and STOP RX actions use the existing packet worker; there is no automatic
  RF start or transmit admission. Actual SX1262 frequency is separate from RTL
  tuning. Unknown/uninterpreted payload text stays hidden.
- Software commands/results: `wsl bash tools/test-p4-board-radio.sh` PASS;
  `wsl bash tools/test-p4-sx126x-probe.sh` PASS (70 payload/driver-limit cases,
  partial transfer failure, invalid limits, cleanup ownership/failure and retry);
  `pwsh -NoProfile -File apps/orcsdr-tab5/tools/test-p4-packet-radio.ps1` PASS
  (seven lifecycle scenarios); `wsl bash tools/test-p4-packet-radio-ui.sh` PASS.
- `ORCSDR_LORA_ASAN=1 bash tools/test-lora-tx-ui.sh` with
  `ORCSDR_ORCMAPS_ROOT=apps/orcsdr-p4-hosyond-console/managed_components/orcmaps`
  (absolute WSL path) PASS: 960 rendering/state combinations, native geometry,
  RX-only compatibility, admission gates, message-only redraw and RX controls.
- Native build and app-only flash passed. The RF/UI-tested development ELF is
  `5c3ebc42e7fe3d344a05cc94f1c2a379ab81e0bb14ddc7039d75768d124189d3`;
  source was committed as `6506298f` after the build (embedded description still
  says `058a4a16-dirty`). This is development evidence, not a clean release artifact.
- Private evidence: `.local/onboard-ui-rf-device.log`,
  `.local/captures/r20-mesh-message-portrait.png` and
  `r20-mesh-message-landscape.png`; matching symbols retained under
  `.local/diagnostics/r20-ui-before-probe/`. These contain device/message details
  and are not installation-package inputs.
- Raw PNG comparison proves brand/tabs/title/footer unchanged across idle and
  packet arrival. A preview initially appeared to omit unchanged content; the
  saved pixel data and human physical feedback confirm retained chrome. A bounded
  temporary readback probe was removed; no redraw workaround was introduced.
  New `tools/check_screenshot_regions.py` checks the actual saved pixels.
- The earlier diagnostics build ran for roughly 75 minutes between sampled
  endpoints: receiver streaming, audio task running/muted as configured, audio
  dropped=0 and underruns unchanged at 110. This is sampled same-boot evidence,
  not a continuous all-service soak or audible-audio acceptance.
- GNSS source scout: primary LilyGO driver commit
  `a47cf73e2c9d442c0f00b618749150f4482dcd0a` uses UART1 RX23/TX22 and initializes
  XL9535 IO11 low before L76K setup; it configures/retries 115200 baud. A passive
  reader must still establish actual output baud/startup on this unit. Board
  control has no GNSS wake operation yet; do not infer active polarity from the
  startup level or claim a fix without UART evidence.
  Sources: [LilyGO pin config](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_config.h#L105-L108),
  [L76K initialization](https://github.com/Xinyuan-LilyGO/lilygo_device_driver/blob/a47cf73e2c9d442c0f00b618749150f4482dcd0a/src/device/t_display_p4/t_display_p4_driver.cpp#L800-L867).

## 2026-11-03 shared receive viewport checkpoint

- Source `ea37b464` migrates the existing five receive views and adds shared
  five/six-tab navigation alongside TX Lab. There is one production M5GFX
  implementation, using viewport/safe-area geometry for draw/touch/focus,
  node/traffic paging, spectrum, bounded 128-pixel waterfall rows and map camera.
  Known unsupported P4 export/log/save actions are disabled explicitly.
- Checks passed: `wsl bash tools/test-lora-rx-data.sh` (ASan/UBSan recording
  templates), `wsl bash tools/test-lora-rx-integration.sh` (actual renderer,
  populated text bounds, five viewports, page/tab controls, map zoom/drag,
  TX-presence transitions and no ordinary full clear), and
  `ORCSDR_LORA_ASAN=1 bash tools/test-lora-tx-ui.sh` (960 state combinations,
  admission/revocation, readable native controls and message-only updates).
  Existing map-view tests also pass for pending/ready/unavailable behavior and
  full attribution wrapped at requested caption size 2. No full suite was run.
- The native build and app flash passed. Final development ELF:
  `cca129e7d4d59ce1f3b035e7f0145a427fe5f1393a3f3ec67bef280ced1e376e`.
  Exact symbols/config/map/binary are private under
  `.local/diagnostics/r21-rx-readable-final/`. Its build description predates the
  source commit and reports `34c71ba8-dirty`; it is not a clean release artifact.
- Twelve final native captures/JSON sidecars are in
  `.local/captures/r22-final/`, with matching ELF and verified PNG hashes. Earlier
  r21 empty and populated captures are preserved separately. Every RX page and
  TX Lab was opened in portrait/landscape. No clipping was observed in inspected
  final empty-state captures. The owner subsequently accepted all five receive tabs
  as readable with working touch after rotation in both orientations. This does
  not close populated/error states, map gestures, modal or keyboard-focus gates.
- RF proof on the preceding build: Heltec V4 at owner-approved 906.875 MHz,
  LongFast, antennas attached (LilyGO MMCX1), 5 dBm, hop limit zero. One 124-byte
  packet with exact test body and packet ID matched the SX1262 CRC/decode result
  (RSSI -36 dBm, SNR 7 dB). Traffic and Nodes displayed it in both orientations.
  Private evidence: `.local/lora-rx-populated-device.log` and
  `.local/captures/r21-populated/`. The populated captures exposed unknown battery
  and hops as 255; the final source renders `--`, covered by a focused regression.
  The corrected sentinel state has not been recaptured with another RF packet.
- Final on-device VIEW MAP / ZOOM_IN / ZOOM_OUT / RECEIVER commands each returned
  `applied=1`. Named serial controls use an authenticated UI-owner queue:
  `RTL_LORA CONTROL VIEW OVERVIEW|NODES|TRAFFIC|MAP|RF_HEALTH|TX_LAB`,
  `PREV`, `NEXT`, `DETAILS`, `FILTER`, `ZOOM_IN`, `ZOOM_OUT`, `RECEIVER`,
  `NODE_CENTER`, `FOLLOW_ON`, `FOLLOW_OFF`. Admission returns QUEUED; the later
  result reports applied/rejected. Palette/speed/channel-picker serial parity
  remains an explicit gap. Physical map gestures and modal/focus acceptance remain.
- Board restored to Home / 96.1 MHz with Auto-rotate enabled. Receiver briefly
  restarted during the return to 2.4 MS/s, then status verified connected and
  streaming at 2399781 samples/s; Wi-Fi connected. Audio task running, muted as
  configured, dropped=0, underruns=742 after the dashboard/capture sequence.
  This is not audible continuity/soak acceptance; investigate underrun behavior
  separately under an enabled audio workload before closing that gate.
- Parser-only GNSS source `34c71ba8` is separate progress; no GNSS hardware
  startup, fresh fix or location-service acceptance is claimed.
- [Dashboard authoring requirements](P4_DASHBOARD_AUTHORING.md) describe this
  reusable pattern and the source/build/device/human validation boundary.

## GNSS startup seam checkpoint

- Board profile now records UART1 RX23/TX22, vendor-configured 115200 baud,
  and the documented IO11 low startup latch. No wake/sleep polarity is inferred.
- The existing locked expander owner exposes GNSS preparation; it touches only
  bank1 bit1 and leaves absent profiles unchanged. It is not yet called at boot.
- `wsl bash tools/test-p4-board-radio.sh` passed mapping, neighboring-bit
  preservation, absent-profile and read/write/lock failure checks. No firmware
  build, flash, UART reception or GNSS fix is claimed for this change.

- Passive GNSS reader and `RTL_GNSS STATUS` are wired into the P4 application.
  Status omits coordinates and raw NMEA; saved receiver location is unchanged.
  Review requested focused service tests for UART ownership, failed cleanup,
  fix invalidation and retained-fix age before native build/device qualification.
- Pre-build device check: ELF `cca129e7d4d59ce1f3b035e7f0145a427fe5f1393a3f3ec67bef280ced1e376e`,
  power-on reset, uptime 625878 ms, RTL streaming 959990 samples/s at 906.875 MHz,
  internal free 50471 / DMA free 12687 bytes. Audio remains muted; underruns
  reached 18013 during LoRa use, so this is not audio-continuity acceptance.
  Private log: `.local/gnss-before-device.log`.

## Passive GNSS native checkpoint

- `wsl bash tools/test-p4-gnss.sh`, `wsl bash tools/test-p4-gnss-service.sh`
  and `wsl bash tools/test-p4-board-radio.sh` passed. Actual service host checks
  cover ownership/setup failures, cleanup reporting, RX-only pin mapping,
  fix/no-fix/error transitions, 4,999/5,000 ms expiry and retry backoff.
- Native ESP-IDF build passed; app-only flash at 0x10000 verified its data hash.
  Installed ELF matches archived symbols:
  `d688b17bb0ea0993d20b9f6f686ccdfd11346010e5e0a515b91b4cbd74d7b58d`.
  Private archive `.local/diagnostics/gnss-passive/`; logs
  `.local/gnss-native-build.log`, `.local/gnss-native-final-build.log`,
  `.local/gnss-passive-flash.log`, `.local/gnss-after-device.log`.
- At 10.8 seconds, UART1 RX23 at 115200 started with zero read/cleanup errors
  but zero bytes/sentences/fixes. This proves startup only, not NMEA reception.
  Establish the receiver startup/output configuration before claiming hardware
  GNSS support. Saved location is untouched; no active GNSS commands were sent.
- Same boot RTL streaming 2,399,330 samples/s at 96.1 MHz; audio running/muted,
  dropped=0, underruns=109. Internal free 62,143 / DMA free 25,275 bytes.
  Enabled-audio continuity and sustained workloads remain open.

## GNSS wake-sequence correction

- Following the outer initializer into the underlying `cpp_bus_driver` v2.2.0
  L76K implementation established `Init -> Sleep(false) -> WAKE_UP high`.
  The earlier profile incorrectly retained the outer initializer's temporary
  low/sleep level. Profile startup now requests high through the same locked
  expander owner; no UART configuration commands are transmitted.
- Primary reference: [L76K driver](https://github.com/Llgok/cpp_bus_driver/blob/v2.2.0/src/chip/uart/l76k.cpp),
  `Init` and `Sleep`. GPL source consulted for pin/polarity behavior only; no
  implementation copied. Existing low-level initialization evidence above is
  historical and does not establish an awake receiver.
- `wsl bash tools/test-p4-board-radio.sh` passed both startup levels, bank1
  bit1 mapping, neighboring-bit preservation and existing failure paths.

- Follow-through of the primary vendor UART constructor also corrected pin
  direction: `HardwareUart(tx, rx)` is constructed with `(kRx23, kTx22)`, so
  P4 RX is GPIO22 and P4 TX is GPIO23. Earlier RX23 statements above record
  the incorrect initial assumption; zero-byte checks on those builds do not
  qualify the actual GNSS connection. Profile now selects RX22; UART TX stays
  unconfigured. [UART constructor](https://github.com/Llgok/cpp_bus_driver/blob/v2.2.0/src/bus/uart/hardware_uart.h).

- `wsl bash tools/test-p4-gnss.sh` now also compiles every actual board
  profile and checks LilyGO RX22/TX23, wake-high and UART1 against the traced
  vendor connection. Other profiles remain GNSS-absent. Parser/profile checks
  passed, as did `wsl bash tools/test-p4-board-radio.sh`.

- Corrected RX22/wake-high firmware built and app-flashed successfully. Device
  ELF matches private archive `.local/diagnostics/gnss-rx22/`:
  `06a96c4e95bcdc9358fbd97eaca5f82944f00dcba5ffa7acf28c913773e7894d`.
  At 3.7 seconds: 251 received bytes, 8 complete sentences, 2 no-fix reports,
  6 ignored sentence types, zero parser rejects/read errors/cleanup errors.
  Communication and framing are now device-proven at 115200; no satellite
  fix or location integration is claimed. Private evidence:
  `.local/gnss-rx22-build.log`, `.local/gnss-rx22-flash.log`,
  `.local/gnss-rx22-device.log`.

- At 30 seconds on the same boot: 7,052 bytes / 224 framed sentences,
  56 no-fix reports, 168 ignored types, zero rejects or read errors. RTL
  streaming 2,399,843 samples/s at 96.1 MHz; audio running/muted, dropped=0,
  underruns=110. DMA free 24,291 and internal free 61,923 bytes. A clearer
  sky placement has been requested for satellite-fix acceptance; other full
  integration work can continue independently.

## Shared FT8 runtime source checkpoint

- Added `ft8_runtime_sources.cmake`, consumed by both native applications:
  30 existing FT8/FT4/JS8/WWV runtime sources now compile on P4. Tab5
  source membership is unchanged (195 existing UI translation units compared).
- `python tools/test-ft8-runtime-sources.py` passed, including actual CMake
  evaluation from another directory and repeated-include/path checks. All 30
  sources appear in the P4 compile database; native ESP-IDF build passed
  (`.local/ft8-runtime-native-build.log`). This build has not been flashed.
- Contrary to the initial scout, the old P4 FT8 mention was only a compile
  property, not source registration. Its renderer still needs responsive
  migration: no existing viewport setter was found. Screen lifecycle, queued
  IQ/discontinuity handling, verified clock, decode delivery and history
  remain open. Compiling runtime sources does not prove live FT8 decoding.
- P4 currently lacks network UTC synchronization; `utc_epoch()` only checks
  a plausible date. Shared clock freshness policy can be reused, but Tab5
  SNTP engine writes M5 RTC and must not be imported unchanged. A separate
  narrow platform adapter is being prepared without RTC assumptions.

- `wsl bash tools/test-ft8-port-core.sh` passed existing audio-tap and native
  backend regressions with ASan/UBSan, strict warnings and production source.
  No full FT8 suite or physical reception test was run for this source-list
  checkpoint. Currently installed GNSS/RX firmware remains unchanged.

## P4 system UTC integration checkpoint

- Shared clock freshness policy now drives a P4-local asynchronous SNTP
  adapter; no M5 RTC calls or RTC persistence claims. Existing Settings clock
  editor supports queued network sync/manual UTC and persistent UTC offset.
  `RTL_CLOCK STATUS`, authenticated `SYNC`, and authenticated `SET <epoch>`
  use the same clock owner/validation. Serial offset editing remains a gap.
- `wsl bash tools/test-p4-network-clock.sh` passed actual adapter tests under
  ASan/UBSan and shared policy tests: timeout/backoff, explicit offline request
  retention, reconnect, foreign owner, supported UTC bounds, manual-set error
  and trust invalidation. `python tests/test_settings_clock_recording.py`
  passed 6 tests; `python tests/test_settings_system_clock_layout.py` passed.
- Native build and app flash passed for ELF
  `88538295a6a46a0ae535d15241815a7f551827fabb55d32801c2473546a57590`,
  archived privately under `.local/diagnostics/network-clock/`. On-device
  NTP established fresh UTC; manual set changed revision 1->2 and cleared
  `last_sync_utc`; explicit resync restored fresh UTC/revision 3. Unauthenticated
  sync was rejected. Private log `.local/network-clock-device.log`.
- Initial 24-second polling window ended before Wi-Fi connected; a later
  read confirmed connection and automatic NTP success. This does not close
  Wi-Fi cold-boot/retry acceptance. At 75 seconds RTL streaming 2,399,953
  samples/s, muted audio dropped=0 / underruns=109 unchanged during clock
  checks, GNSS framing with no rejects/read errors and no satellite fix.
- Actual clock captures in `.local/captures/network-clock-r3/` show both
  orientations. Earlier capture attempts opened the wrong section because
  Settings enters Display/Audio, not Connectivity; they are not clock QA.
  Portrait showed a clipped offset label, now shortened to ZONE in the
  compact renderer; final rebuild and recapture passed. Existing
  Tab5 labels and hardware-clock behavior retain their defaults.

- Final label build/app flash passed; installed ELF matches private archive
  `.local/diagnostics/network-clock-label/`:
  `da819adf0572c2b4128f27099e175b90ee108c97c3b0e93f7ee1a0f54eaa2057`.
  Four native PNGs and identity sidecars in
  `.local/captures/network-clock-final/` cover System and clock editor in both
  orientations during offline/unset startup. ZONE is readable without overlap;
  landscape controls fit. A raw-pixel retained-header comparison passed between
  portrait System/editor captures. These are framebuffer checks, not human
  physical acceptance or proof of UI save/offset persistence across reboot.
- Final post-capture sample at 32 seconds: RTL streaming 2,399,566 samples/s,
  GNSS framing/no fix, muted audio dropped=0 / underruns=289. Increased
  underruns during capture remain separate from earlier unchanged NTP-test
  evidence and do not close audio continuity. Wi-Fi/NTP not up at this early
  final-build sample; automatic sync qualification is recorded separately.

- At 61 seconds on the final build, automatic network sync is device-verified:
  valid UTC, shared level synced, age 15 seconds, attempts=1, revision=1,
  no clock failure. RTL streaming 2,399,833 samples/s and muted underruns
  remained 289 after return to Home. Private log
  `.local/network-clock-final-device.log`. Human clock-control acceptance,
  manual save/zone persistence device checks, serial zone parity, NTP
  outages/reconnect soak and FT8 readiness integration remain open.

### LoRa decoder latency baseline

- LilyGO RTL-SDR, 906.875 MHz, ELF `4695a13656d03c832f01aa2a516993d491bc0a5a1ec2eff79357d070cd5fe84a`: one 3,855,104-byte capture took 1,745 ms, queue dispatch took 2 ms, and decode through packet storage took 23,630 ms with one CRC-valid frame. Queue timing starts at dispatch and does not include time waiting for a previous decode. Display latency was not measured.
- Priority-1 candidate ELF `3e92c2c2f78b3928451aafdbf18a50daa2ce5db4242bb3879a565e1cd6e392ca` compiled with ESP-IDF 5.5.4, passed app-only flash hash verification, and streamed approximately 960 kS/s on the same frequency. The first 55-second observation produced no decode timing record. A matched Tab5 comparison and stability under sustained decode load remain open.
- Subsequent no-reset Tab5 status showed FM at 96.1 MHz, IQ source idle, and zero native LoRa decodes. This session cannot serve as a matched LoRa reference until the Tab5 is listening on 906.875 MHz.
- Priority-1 candidate also reported a fresh BQ27220 sample through RTL_BATTERY STATUS: gauge present, 100%, 4141 mV, 6 mA, 665 ms old, ESP_OK. Percentage accuracy and no-cell behavior remain open.

### C6 recovery package verification

- The priority-1 build reports four embedded segments (1,167,104 bytes) for a 4 MiB C6, idle with no write enabled. The build-generated manifest matches the pinned ESP-Hosted 3.0.6 metadata and all segment hashes. App-only P4 flash verification is separate from C6 recovery acceptance.
- `python -m pytest -q tests/test_c6_rom_package.py tests/test_c6_rom_update.py` passed 6 tests and 3 subtests. `python -m pytest -q tests/test_c6_worker_guards.py tests/test_c6_worker_cleanup.py` passed 10 tests. Factory ESP-AT conversion and interrupted-update recovery still require installed-app hardware tests.

### P25 control-channel and screenshot checkpoint

- Shared layout tests passed 2 cases with `python -m pytest -q tests/test_p25_dashboard_layout.py`; `wsl bash tools/test-p25-core.sh` passed. ESP-IDF 5.5.4 built the LilyGO adapter and app-only flash hash verified.
- Build `7b86566358b089afaf7787e51aaa0dd69c5193d78540ae2fefaa471b34d6e4e4` applied the authenticated serial tune and decoded a live control-channel grant on 453.925 MHz. Native landscape capture: `.local/captures/p25-live-settled.png`. Portrait Monitor capture after manual rotation: `.local/captures/p25-portrait-monitor.png`. Other tabs, touch, profile management, spectrum feed, voice follow and playback remain open.
- An earlier capture panicked in M5GFX on a null DMA row allocation. The original crash report and matching ELF are in `.local/diagnostics/p25-screenshot-panic/`. The checked PSRAM row fix produced two native P25 captures with no observed panic; the first capture retained uninterrupted uptime and a DMA low-water mark of 2,343 bytes.

- P25 Spectrum receives the existing receiver FFT frames. Native portrait and landscape Spectrum captures show a live trace and waterfall on 453.925 MHz, with the selected view and frequency preserved across manual rotation. The lowest reported DMA headroom was 739 bytes. Sustained load and intermittent missing header captures remain open.
