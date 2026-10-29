# Drone Watch — test and acceptance matrix

**State:** proposed validation protocol, 2026-10-26. Nothing in this table implies tests have passed unless explicitly marked. These gates follow `PROJECT_STATUS.md` evidence vocabulary. Perform source inspection, host tests, firmware build, exact flash, real RF reception and long-run coexistence **separately**.

## Required capture metadata for every test

Capture in a versioned directory, preferably `evidence/drone-watch/<date>_<device>_<gate>/` on the test machine (or private REA intake for independent research), with `README.md`, `results.md`, `console.log`, `manifest.json` and saved captures only when permitted. Record:
- `gate_id`, test command, wall-clock date/time zone, operator and purpose;
- OrcSDR branch/commit, exact app binary SHA-256, native ESP-IDF version, M5GFX version, `esp_rtl_sdr` immutable pin;
- board model + revision, connected RTL dongle identifier/model, C6 P4-host and slave versions, antenna, source firmware revisions;
- baseline (Wi-Fi state, active SDR session, sample rate, memory floor, display size, SD state), actions and exact observed outputs;
- counters: packets seen, valid/invalid decodes, Rx errors, dropped/overrun frames, queue utilization, reported source availability/gap time;
- pass/fail/blocked/not-run with reason; links to raw logs, screenshots and fixture hashes; redact network credentials and unnecessary operator/home coordinates.

**Do not** copy private radio captures, unauthenticated personal data, proprietary firmware or raw third-party source into the generated REA-Results `targets/` or `comparisons/`. Outside findings go to `REA-Results/inbox/` on a branch; generated indexes only via the owner's local `rsa.py publish`.

## Gate matrix

| Gate | Run on | What to prove | Hard stop / rejection | Status |
| --- | --- | --- | --- | --- |
| D0 provenance | GitHub / REA inbox | SHA-pinned source/license list, documented unknowns and 15/15 synthetic event-model tests | claim of hardware proof from software-only test | RESEARCH REPORTED |
| D1 decoder | Host (C/C++) | upstream-valid BasicID/Location/System/SelfID/Auth/pack messages + deliberately truncated/corrupt/mixed-version frames; bounded memory/sanitizer | crash, out-of-bounds, invented position, failure to reject malformed | PARTIAL: all six individual message-family host tests and pack PASS; 5,000 deterministic cases; combined suite 6/6 PASS, ASan/UBSan 6/6 PASS; native/hardware NOT RUN |
| D2 source model | Host | monotonic timing, stale/unknown positions, source distinction, duplicate packets, overflow/drop metrics, BLE+Wi-Fi merge policy | stale map point; RF energy promoted to 'aircraft'; source metadata lost | PARTIAL: 32-event source-aware history, replay independent of transport, BLE4 AD ingress and overflow host PASS; no real multi-radio association/native RF yet |
| D3 C6 BLE | Spare Tab5 | actual BLE4 advertisement bytes from known authorized source through the **current Hosted C6**, versioned RSSI if present | HCI unavailable, Host crash, Wi-Fi regression, unsupported capability incorrectly green | HARDWARE NOT RUN; P2a compile/link capability diagnostic HOST PASS; current configuration lacks Bluetooth HCI and host stack |
| D4 C6 extended / Wi-Fi | Spare Tab5 | each supported BLE5 extended/coded and Wi-Fi beacon/NAN path separately (with actual frames) | extrapolation from BLE4 to BLE5/NAN, or current Wi-Fi scan presented as raw frame monitor | NOT RUN |
| D5 compatibility | Tab5 + V4 + SD | Wi-Fi scan/connect/catalog + saved networks, SDR stop/resume, USB recovery and M5 Launcher compatibility | hidden firmware update, broken Wi-Fi, false full-duty scan claim | NOT RUN |
| D6 UI | 1280×720 Tab5 and 720×720 reference | legibility, header touch ownership, expired events, no-signal/source-off, navigation, restore previous mode | unreadable 5-inch text, orphaned screen ownership, synthetic data shown as live | **W01 PARTIAL: host drawing recorder 1/1 PASS; native and physical NOT RUN** |
| D7 OrcMaps | Validated chosen board and decoded replay/live frames | map labels preference, valid claimed positions, proper age markers, missing packs, no GPS and offline handling | bad coordinates plotted, duplicate basemap, stale pins, operator data exposed | NOT RUN |
| D8 journal | Tab5 + optional SD | opt-in-only writes, full/remove/reboot, bounded queue, integrity and source provenance | SD writes on receiver/UI task, silent data loss, operator/home persistence by default | NOT RUN |
| D9 OrcNode | Independent known device + Tab5 | authenticated adapter envelope, source ID, replay detection, network drop/recovery and clock drift | cross-source aircraft fusion or raw network data falsely labeled as C6 RF | NOT RUN |
| D10 SDR activity | Tab5 + V4/other supported receiver | tuner/sample/channel facts and measured signal/occupancy; scanning time coverage | arbitrary RF peak labeled a drone or an undocumented tuning range | NOT RUN |
| D11 analog video | Chosen compatible external RX + digitizer | real frames, format/drops/USB power, unplug recovery and scan pause indication | RSSI presented as decoded video; unverified digital/proprietary format claim | NOT RUN |
| D12 audio | Actual Tab5 microphone | opt-in behavior; recorded positive/negative corpus, false-alarm rate and resource impact | simulated audio or unmeasured classification/range presented as validated | NOT RUN |
| D13 release | Exact firmware image/target boards | no FM/ADS-B/LoRa/Dead Air/OrcMaps/Wi-Fi regression; controlled power/restart/soak; reversible settings | USB regression, unbounded heap pressure, C6 mismatch or unrepeatable test | NOT RUN |

## Precise host / firmware / physical verification workflow

### A. Before touching hardware
1. `git fetch origin`; confirm `main` and exact implementation branch commit. Leave `main` unchanged.
2. Read current `AGENTS.md` if present, `PROJECT_STATUS.md`, `Roadmap.md`, `docs/user-guide/wifi-tab5.md` and the research report's pinned sources.
3. Run baseline unit/CI commands already provided by the project; do not guess that every historical command still exists. In particular, check `apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1` and the current native build helper.
4. Run targeted host decoder tests with fixtures under `tests/drone_watch/`. Capture stdout/stderr and exit status to a log in the test working directory. Include separate sanitizer/fuzz results if enabled; report `NOT RUN` when unavailable.
5. Check `git diff --check`, dependency lock files, bundled source attributions, binary size and internal DMA-capable heap headroom against a measured baseline.

### B. Native build (does not establish physical operation)
Run from the OrcSDR repository root in a configured **Windows ESP-IDF 5.5.4** environment:

```powershell
# Use the existing maintained Tab5 build helper; inspect its parameters first.
Set-Location F:\Ai\OrcSDR\apps\orcsdr-tab5
.\tools\build-tab5-idf.ps1 *>&1 | Tee-Object -FilePath .\drone-watch-build.log
```

Record the build exit code, app image SHA-256, partitions, C6 pin, memory headroom and dependency pins. If any tool is unavailable, do not mark this as Build-Verified. Re-run existing smoke/self-checks with `apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1` per the current docs after checking supported switches.

### C. Spare Tab5 + C6 probe
1. Disconnect or identify the production device; choose a **spare** test Tab5 with known serial port and installed C6 firmware version. Record board and attached RTL-SDR. Back up settings if intended to change.
2. First confirm current normal boot, access point scan, network join, signed data catalog retrieval and RTL receive before/after Wi-Fi operation. Save serial logs.
3. Without re-flashing the C6, run a passive BLE4 advertisement probe using an owned source. Save bounded raw HCI/advertising bytes, API availability, interval/gap counters and actual packet metadata. Verify that the observation is from a physical receiver and not a simulator.
4. Explicitly test Wi-Fi off, Wi-Fi idle, network connected and scanning. Record when/why RTL-SDR pauses. Do not assume BLE+Wi-Fi simultaneous operation.
5. Expand to BLE5 coded/extended and Wi-Fi beacon/NAN **one at a time**, only when exact C6 build and API prove support. Unsupported paths should generate no fake activity.
6. Compare observations using a second trusted receiver (phone or independent scanner) where possible. An absence or mismatch requires a physical/range/environment caveat; it is not proof of an attacker or missing aircraft.
7. If the probe requires a C6 firmware update, **stop at approval gate**: preserve factory/Launcher compatibility, inspect full flash/rollback recipe and obtain explicit permission for that operation.

### D. Firmware on hardware (once implementation + build are green)
1. Flash only the approved exact app image to the **known** serial port, after verifying its binary hash; do not overwrite bootloader, partition/NVS or coprocessor by default.
2. Start monitoring without issuing a radio-control operation. Capture `source_unavailable` and `scan_paused` cases first (disconnect source; intentionally start a Wi-Fi scan).
3. Observe an owned known Remote ID broadcaster with a **valid** raw capture and compare all decoded claims field-by-field against an independent reference implementation.
4. Exercise zero-message screen, repeated open/close, quick navigation, fast tabs, timeout recovery, source loss/recovery, invalid location clearing, and map offline/missing data-pack behavior. Verify `TEST DATA` label with replay build.
5. Verify unchanged FM/RDS, ADS-B, LoRa, Wi-Fi/HTTPS catalog, USB drop counters, SD mount/record, Dead Air and shared map/label controls.
6. Record a second 720×720 display result independently; it cannot inherit Tab5 acceptance.
7. For optional video and acoustic experiments, label and log each accessory and tested format. No performance promises from part numbers or simulation.

### E. Closeout
- Run exact targeted host tests again on final commit.
- Save log files, command exits, screenshots and binary SHA before stating PASS.
- Add a dated finding/report to REA-Results `inbox/` if a new measured experiment materially changes the research; keep REA-generated indexes immutable.
- Update `PROJECT_STATUS.md` only after verified evidence; mark pending work in `Roadmap.md` not in historical `phasing.md`.
- A draft PR stays draft until the owner decides how/when to integrate with current feature branches.

## Suggested objective criteria (proposed, not yet measured)

- Decoded packets with absent/invalid lat/lon never appear on OrcMaps.
- No RF-only or acoustic-only event creates an aircraft identity or a verified location.
- A paused scanner immediately shows `PAUSED` plus reason, never an idle green scanner or false zero-aircraft coverage.
- No reader-facing page presents simulated observations as live.
- Reconnect, storage failure, quick leave/re-enter and invalid packet cases preserve memory/queue bounds and keep baseline radio behavior.
- Video view explicitly states whether its receiver stopped scanning and restores that state when video view closes.
- All numbers shown in the UI have a known source and unit (RSSI, frequency, packet rate, drop count, elapsed time, altitude reference). Unsupported measurements are omitted, not guessed.
