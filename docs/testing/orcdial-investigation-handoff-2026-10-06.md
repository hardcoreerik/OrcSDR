# Claude handoff: OrcDial pairing, connection, and Tab5 coexistence

Please take over this investigation. Scout the current state before editing. Do not assume the symptoms below share one cause, and do not claim the root cause is known. Preserve dirty changes, logs, recovery binaries, and device trust. Do not merge OrcSDR until the user explicitly asks.

## Workspace and source boundaries

- OrcSDR working checkout: `F:\AI\OrcSDR-TEMP\m5dial-vfo`, branch `codex/m5dial-vfo`, HEAD `1d896648730caa3abf439d5b872dccdcfa0124f8` plus uncommitted changes. Previously 11 ahead / 0 behind the locally fetched origin/main; recheck before integration.
- Original `F:\Ai\OrcSDR` main checkout is protected. Do not reset it, switch it, or move work into it.
- All OrcSDR worktrees belong under `F:\AI\OrcSDR-TEMP\`.
- Standalone OrcDial repository: `F:\Ai\OrcDial`, branch `codex/import-orcdial`, HEAD `753609441a0c83c697dfb56d748d320aa68affef` plus uncommitted changes. Remote `https://github.com/hardcoreerik/OrcDial.git`, draft PR #1. The OrcSDR checkout also contains an `orcdial` subtree; do not assume the two copies or flashed Dial are identical.
- User AGENTS instructions: narrow scout first, narrow tests, protect main, ask before expensive build/test loops, no unsolicited merge/rebase/force-push. Grok and adversarial review mean the same global `adversarial-review` skill; closed diff packet only. Do not send the repository/worktree to Grok. Do not invent a paid security-review requirement.

## Connected hardware

- COM17: Tab5 ESP32-P4 native USB Serial/JTAG. This is the tablet application and receiver/UI/DSP log.
- COM20: CH9102 USB UART through the orange M5Stack ESP32 Downloader, plugged directly into the Tab5 C6 ISP connector, matching M5Stack's official photo. Captures the ESP32-C6's own diagnostic output at 115200, 8N1. Tablet is opened and upside down with adapter attached. The user wants us to issue tablet controls through serial rather than asking for screen taps.
- COM14: M5Stack M5Dial / OrcDial USB serial, 115200.
- COM18 is an unrelated CH343 device. Do not use it. Other COM ports are not substitutes without identification.
- Nooelec Smart V5 R820T2 is attached to Tab5 USB-A. Profile `nooelec_smart_v5_r820t2`. Latest capture 2.4 Msps, FM 94.508 MHz.
- Router hardcorewifi: actual association observed on channel 11. Do not extract passwords.
- Serial ports are opened with DTR/RTS deasserted; no automatic flash/reset through the C6 downloader. The full adapter also has power/reset/programming connections, so its attachment may affect the electrical setup. We have not proved that it has no effect. Do not treat an expander latch as a voltage measurement.

## Immediate handover: five-minute user play capture

**Updated status: this capture has completed and its monitors are closed.** The process information below describes the earlier active window; do not assume PID 45468 still belongs to it. The concluding findings and post-play status are at the end of this handoff. No monitor from this session currently owns the three ports.

Historical capture setup (completed; all monitors are now closed):

- Python process PID 45468 (verify it is still the same process before acting): `python -u orcdial/.pio/wifi-investigation/c6-uart/hands_on.py`.
- Codex terminal handle 37509 is private to this session; Claude should inspect the process and files rather than assuming it can use that handle.
- Evidence directory `orcdial/.pio/wifi-investigation/c6-uart/hands-on-20261006T165723Z`.
- Started 2026-10-06 09:57:23 America/Los_Angeles (16:57:23 UTC), intended duration 300 seconds. Ends around 10:02:23 local. Inspect `events.log` and `report.json` to verify completion.
- It owns COM14, COM17, COM20. Do not open competing monitors. It reopens absent ports once they return, records port-loss events, and performs NO serial writes or automatic controls. Opening a returning native USB port can itself affect the device; account for that when interpreting startup tests.
- User is trying controls, unplug/replug, battery-only operation, Dial-first and Tab5-first startup. Allow this phase to finish; do not interrupt with automated actions. User messages can serve as action markers.
- Latest observed Dial log during this capture: LINKED and incoming RADIO_STATE at 94.508 MHz. This is an observation, not proof of all reconnect scenarios.
- An earlier play window `hands-on-20261006T165346Z` was interrupted because trust had not been established; its logs are preserved and report explicitly says interrupted for initial pairing. Do not count it as an accepted control test.

## Intended behavior

OrcDial is OPTIONAL. OrcSDR must remain fully usable with no Dial, no router, or a powered-off accessory. ESP-NOW is the preferred control transport; Wi-Fi Direct was removed after severe audio/receiver problems and must not be silently restored.

Pairing and connection are distinct:

- One trusted tablet per Dial and one trusted Dial per tablet. Pairing is an explicit one-time trust operation; both locally enter a bounded window and approve matching six-digit codes. Persist unique device identities and pairing secret. No automatic trust from MAC discovery or shared preinstalled credentials.
- Connection uses existing trust at staged boot or on demand from either device. Dial can listen early; tablet waits for splash/Wi-Fi/channel settlement, with a bounded router attempt so standalone ESP-NOW works without a router. Keep the router's channel; do not retune the tablet away from it.
- Disconnect ends the session, retains trust, and suppresses retries for the current boot on both devices. Explicit Connect may resume. Forget revokes local trust even if peer is offline; Forget & Re-pair requires fresh confirmation.
- Separate trust/connection status, boot-connect toggle, and Pair/Connect/Disconnect/Forget actions in Tab5 Settings -> Accessories & Companion and local Dial Devices dashboard. Devices is separate from receiver dashboard IDs and must remain usable/offline without being displaced by incoming dashboard updates.
- v4 secure protocol: P-256 numeric comparison, authenticated fresh session establishment, directional AES-GCM keys, counters/replay rejection, bounded fragmentation through existing 64-byte relay frames. Do not downgrade to unauthenticated v3. Legacy records should show Pairing upgrade required.
- Dial follows active OrcSDR dashboard and sends acknowledged tuning/actions. UI must stay readable within 240x240 circular display, smooth and flicker-free. Normal audio and spectrum/waterfall operation must survive connection, disconnect, absence, and startup order.
- Keep serial commands available so all tablet-side testing can be automated while it is upside down. User also wants a roughly five-minute hands-on phase in the regression routine, where they can physically cycle devices while logs continue and automation stays out of the way.

## Observed issues, without root-cause claims

1. Earlier pairing attempts intermittently coincided with USB receiver disconnect/re-enumeration followed by SDIO 0x107 timeouts; Wi-Fi could then stop reconnecting. Both stopped and running receiver cases have failed historically. A failed event had successful expander reads showing WLAN and USB output latches still enabled. This does not exclude rail transients or prove the C6 crashed.
2. The old SDIO credit loop counted nominal 20-us increments rather than elapsed time. A source-extracted test simulated a 200-ms wait lasting about 10.2 seconds; local elapsed-time fix bounds it near 200 ms. This explains delayed failure reporting in that loop, not the initiating fault.
3. Wi-Fi reconnect policy depended on boot-connect preference, so a manually initiated connection with boot Wi-Fi off could fail without retrying. Local runtime-session policy fixes this. Earlier on/off boot matrix passed scan/connect; audio acceptance is separate.
4. Offline transport failure could bypass poll_wifi recovery because idle early returns and connected/connecting gates skipped it. Local regression/fix detects once before ordinary RPCs, holds requests until recovery, and blocks accessory initialization on a failed/recovering link.
5. One deliberate C6-only power interruption recovered after bounded Hosted teardown (`esp_hosted_deinit=0`), acknowledged power cycle, and reconnect with boot Wi-Fi off. Wi-Fi and capture returned in about 35 seconds including AP retries. A separate spontaneous pairing failure restored C6 transport but receiver restart failed at USB profile record 30. Do not conflate these results.
6. Current three-port same-firmware comparison: receiver stopped reached matching codes; running test reached Exchanging keys on both then explicit Timed out on both after the full window. Wi-Fi stayed associated, Hosted ready, capture running, audio chunks advanced and audio_drops=0. No observed C6 panic or P4 SDIO timeout in that window. This reproduces a pairing-under-load failure, but packet loss, reassembly, scheduling, cryptographic latency, and other mechanisms are unproven hypotheses.
7. C6 relay ignores immediate esp_now_send results and registers no completion callback. Dial checks immediate acceptance but also has no send-completion callback; shared worker delays 8 ms per fragment. Espressif recommends waiting for prior send completion. We have compared this guidance but have NOT implemented a completion-driven radio path or proved this explains our failures.

## Current trust and firmware

The user couldn't play because the previous diagnostic tests cancelled before trust confirmation. We then stopped capture and initiated fresh Pair on both through serial. Verified matching code 608049 and sent CONFIRM to both. Both reported trust=1 and boot=1. The immediate connection timed out. A subsequent explicit CONNECT on both, with capture stopped, succeeded within a few seconds. Capture resumed; immediately before the new play window, P4 reported trust=1 Connected, Wi-Fi connected, capture_state=3, audio_enabled=1 and audio_drops=0. Trust must be preserved now.

The initial-pair harness report has `confirmed_trust=false` because it used that field for 'trust AND subsequent connection verified'; however its raw statuses prove both records became trusted. Do not read that boolean as 'no trust saved'. `trusted-connect-20261006T165701Z/report.json` proves the later mutual Connected state. `paired-pre-play.log` proves connected receiver-running baseline.

Tab5 currently flashed NORMAL app SHA256 `336567006A256B4252FA2A7EC2CF01FEF0CAF9318DC3D26960454CDBF09EB343`, built from dirty worktree. Application-only flash at 0x10000 hash verified. Includes recovery fixes. `ORCSDR_C6_FAULT_TEST=0`; the diagnostic power-off command is absent from normal binary. C6 remains Hosted 3.0.6; no C6 firmware changed in this investigation. Dial flashed image remains earlier 7536094 baseline, not all newer local standalone changes.

## Local changes and tests

OrcSDR dirty source: main.cpp (startup pause, power diagnostics, retry/recovery order, compile-gated lab command), wifi_service.cpp (failed transport cannot report ready), build-tab5-idf.ps1/main CMakeLists (fault-test OFF by default), apply-esp-hosted-trampoline-fix.ps1 plus new elapsed-credit patch. Shared secure_session.hpp Forget/Disconnect notice fix also dirty.

New targeted tests: `python tools/test_sdio_credit_deadline.py` passed ten source-extracted loop checks; `python tools/test_wifi_reconnect_policy.py` passed five retry and five offline-recovery checks. They use mocks and do not substitute for hardware recovery evidence. `tools/test_wifi_accessory_startup.py` tests boot on/off scan/manual connect. No whole-suite rerun needed unless scope justifies it.

Standalone OrcDial dirty: src/control/secure_session.hpp, tests/security_test.cpp, tools/release_check.py, tests/test_release_check.py. Prior Debug/Release CTest and release-check suite passed locally; changes not committed/pushed/re-reviewed. Review status is not closed. Do not broadly re-triage PR history unless necessary.

## Evidence and recovery preservation

Primary narrative: `orcdial/.pio/wifi-investigation/investigation-20261006.md`.
All evidence below is under that investigation directory (mostly git-ignored; preserve explicitly):

- `c6-power-test/`: diagnostic/normal build and flash logs, controlled report, spontaneous-pair logs, normal-status PASS, pre-test normal app.
- `c6-uart/running-pair-20261006T164940Z`: full-window failure with receiver running.
- `c6-uart/stopped-pair-20261006T164822Z`: matching-code success with receiver stopped.
- `c6-uart/initial-trust-20261006T165601Z`, `trusted-connect-20261006T165701Z`, `paired-pre-play.log`.
- Earlier stopped/running comparisons, boot matrix and SDIO evidence are indexed in the narrative.
- Recovery app images: `orcdial/.pio/secure-pairing-recovery/tab5-on-device-before-acceptance.bin` and `F:\Ai\OrcDial\.pio\capture-recovery\pre-v4-on-device.bin` (verify location if needed). Never erase NVS or flash recovery blindly.
- Existing serial authentication key at `F:\Ai\OrcSDR\.orclink\ui-doc.key`; use locally via helpers, never print/copy into logs or reviews. Do not replace it.

## Suggested continuation

The play capture is finished. Inspect its saved action markers, port-return events, all three logs, and the post-play status below; do not assume monitors are still running. Then investigate the observed failures using narrow evidence collection. If adding instrumentation, distinguish frame queue acceptance, Hosted RPC completion, C6 ESP-NOW completion, reassembly, and authenticated application acknowledgement. Avoid guessing fixes from a single success or silence in the UART log. Keep the user informed; issue tablet controls yourself. No merge/release, no C6 firmware update without explicit approval. Leave v0.1.0-beta.1 unchanged.

Primary references already checked: Espressif ESP-NOW v5.5 guide (callback pacing/current channel), esp-hosted-mcu issue #240 (similar 3.0.6 recovery wedge, not proof of our cause), M5Stack Tab5 schematic and C6 restore guide. Verify relevant source versions before using upstream code. No upstream implementation copied in this investigation.

## Handoff update after the user play window

The five-minute hands-on capture hands-on-20261006T165723Z completed normally after 300.04 seconds; monitors closed. Dial recorded 31001 bytes with two port openings, P4 9407 bytes with three openings, C6 zero bytes with one opening. Zero C6 output during that window is not evidence of a C6 power cycle or health verdict. User reported Tab5 cable unplug/replug, immediate audio return, then unplugged Tab5 and Dial USB while leaving the C6 downloader USB attached. Treat these as adapter-attached cable tests; battery state and C6 rail voltage were not established. Later user markers appended to events.log may be outside the actual capture window. At the latest port inventory COM14, COM17 and COM20 were all still enumerated despite the user's unplug report; do not equate enumeration with physical device power or infer a wiring error. No monitors remain active from this five-minute window. The user is still physically testing; coordinate any new capture before issuing automated controls.

User reports Dial reconnected each time and audio recovered; tablet screen was not visible, so continuous Wi-Fi association was not physically observed. A subsequent read-only COM17 status snapshot (c6-uart/post-play-status.log) confirms CURRENT Wi-Fi connected=1, Hosted ready=1/link_failed=0, capture_state=3, audio_enabled=1/speaker_running=1/audio_drops=0, and OrcDial trust=1 Connected boot=1 failure=None. No mutation/authentication/reconnect commands issued for that snapshot. This supports present recovery, not uninterrupted Wi-Fi through every unplug event. Snapshot monitor closed. C6 adapter remains attached, so full power-cycle behavior is still unproven.

## Five-minute capture findings for the next investigator

Capture: c6-uart/hands-on-20261006T165723Z, 300.04 seconds, passive only, no serial control writes. The monitors reopened COM14 once and COM17 twice during the window. A further port-loss event near the end was not followed by a recorded reopen before the timer expired.

The Dial log contains 85 outgoing TX records and 627 parsed RADIO_STATE responses (628 RX prefixes, one not parsed as a complete type record). Returned frequency values changed, supporting actual tablet control rather than only local Dial animation. These counts are log observations, not a command-by-command acknowledgement audit; do not infer every command succeeded.

One observed Dial restart sequence: ESPNOW_V4_INIT_OK at monotonic 685567.277, OFFLINE at 685567.297, incoming RADIO_STATE at 685570.969, and LINKED at 685572.294. It resumed the tablet frequency 94.508 MHz without fresh Pair/Confirm commands. This supports persistent-trust reconnect in this tested setup; it does not prove every startup order.

No Guru Meditation or SDIO timeout marker appeared in the saved P4/Dial logs. The C6 UART file contains zero bytes during this specific play window; its readable earlier startup/pairing logs confirm the connection worked earlier, but silence during play is not a health or power-cycle verdict. The passive capture did not continuously poll Wi-Fi, and native USB logs were unavailable while COM17 was absent. Do not claim uninterrupted Wi-Fi or complete failure coverage.

User reported Dial reconnection each time and normal audio. After Tab5 USB replug, audio returned immediately; the user suspected the UART adapter kept things working. This is a plausible hypothesis, not a measured conclusion. The C6 downloader remained USB-attached throughout, and battery power was not ruled out. Therefore these are adapter-attached cable/restart tests, not established full cold-start tests. Some user action markers were recorded after the actual five-minute window and must not be used to extend its coverage.

Post-play read-only snapshot in c6-uart/post-play-status.log confirms current Wi-Fi connected=1, hosted_ready=1, link_failed=0, capture_state=3, audio_enabled=1, speaker_running=1, audio_chunks=2001, audio_drops=0, and OrcDial trust=1 Connected boot=1 failure=None. Snapshot did not issue pairing/connect/recovery mutations and its port was closed afterward. The latest user report says Tab5 and Dial are plugged back in, C6 adapter still attached. Revalidate ports/state before further testing.

Next work: preserve the now-established trust and working device state, analyze the running-versus-stopped pairing evidence, and design a true cold-start test that explicitly accounts for battery and downloader power. Let the user handle physical power/cable actions while the agent issues tablet controls through serial. Do not silently forget trust or turn this observed hands-on success into a broad release/security acceptance claim.
