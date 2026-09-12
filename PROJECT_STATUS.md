# OrcSDR current project status

Current source snapshot: **2026-09-12**, `main` at
**`c5b3423fbdeb129ebd63d8ffd5c1bced9c913c04`**.

Current published firmware: **`v0.2.0-beta.6-multidongle-rc4`**. Its exact
M5Burner package was installed and booted on the owner Tab5 before publication.
That release evidence does not automatically prove later `main` commits or
other hardware.

This is the authoritative current capability and evidence summary. Release
notes and validation reports are immutable, dated evidence; they do not
override this document for current state. Future work belongs in
[`Roadmap.md`](Roadmap.md).

## Evidence vocabulary

| Status | Meaning |
|---|---|
| **Planned** | No integrated implementation exists. |
| **Implemented** | An integrated source path exists. |
| **Build-Verified** | The relevant target compiled or a deterministic validator passed. |
| **Runtime-Verified** | The feature executed successfully in software or on target. |
| **Hardware-Verified** | It executed on specifically identified physical hardware. |
| **RF-Verified** | It was confirmed with a suitable real RF signal/source and recorded evidence. |
| **Regression-Tested** | A repeatable automated or scripted regression exists. |
| **Community-Verified** | It was independently demonstrated on externally owned hardware. |
| **Experimental** | Reliability/support modifier that may accompany an evidence level. |
| **Unsupported** | Deliberately outside the compatibility contract. |
| **Not Implemented** | Known missing behavior. |
| **Historical Evidence** | Valid dated evidence, not a current-version claim. |

## Current platform and dependencies

| Item | Current state |
|---|---|
| Firmware policy | Native ESP-IDF only; PlatformIO files are historical and unsupported. |
| ESP-IDF | 5.5.4 |
| ESP-Hosted host/C6 | 3.0.6 / 3.0.6 over Tab5 SDIO at the qualified 10 MHz clock |
| M5Unified / M5GFX | 0.2.20 / 0.2.27 |
| `esp-rtl-sdr` | 0.8.0-rc2, immutable pin `b175dfea6782faa97e512d4a2408767c75977527` in the manifest and lock file |
| USB implementation | Current `esp-rtl-sdr` path; the legacy USB source is compiled out but remains in source. |
| Radio policy | Receive-only. Transmission is Not Implemented. |

## Receiver evidence

| Receiver | Status | Boundary |
|---|---|---|
| RTL-SDR Blog V4 | **RF-Verified / tested baseline** | Primary release receiver. |
| RTL-SDR Blog V3C | **RF-Verified / Experimental** | One V3C passed RC4 detection, initialization, streaming, FM/RDS, retune, hotplug, and USB/battery boot. Gain and sensitivity comparisons remain provisional. |
| Earlier RTL-SDR Blog V3 variants | **Implemented / Experimental** | Profile exists; the V3C result is not a blanket earlier-V3 compatibility claim. |
| Nooelec NESDR SMArt V5 | **Implemented / Experimental** | Detection and streaming are provisional; repeatable RF reception is Not Verified. |
| RTL-SDR Blog V4L | **Not Verified** | No explicit profile acceptance evidence was found. |
| Other receivers | **Unsupported / Not Verified** | Generic RTL2832 compatibility is not claimed. |

## Capability and evidence matrix

| Capability | Status | Evidence boundary and limitations |
|---|---|---|
| FM receive, stereo, RDS, presets | **RF-Verified / Regression-Tested** | Verified on Blog V4 and V3C. Results remain bounded to the tested dongle, antenna, band, and setup. |
| AM broadcast dashboard and 119-channel scan | **Hardware-Verified / Experimental** | The exact RC4 package exercised the dashboard and scan. General reception quality and gain calibration are not established. |
| NOAA Weather Radio | **Implemented** | Current-release RF acceptance is Not Verified. |
| CB | **Implemented / Runtime-Verified** | Flashed and exercised; operator/RF acceptance is still pending. |
| Shortwave | **Implemented / Experimental** | Routes to the generic Browse/NFM workspace. A complete calibrated HF AM/SSB experience is Not Implemented. |
| Airband | **Implemented / Experimental** | Routes to generic Browse near 121.5 MHz using NFM. Proper AM aviation voice is Not Implemented. |
| Marine | **Implemented / Experimental** | Generic NFM routing exists; no dedicated dashboard or current RF acceptance is recorded. |
| Satellite | **Implemented / Experimental** | Generic Browse routing near 137.5 MHz exists. No dedicated satellite decoder/dashboard is implemented. |
| P25 Phase I | **RF-Verified / Hardware-Verified / Regression-Tested** | Documented control, clear voice, encrypted-call detection/muting, and follow behavior. System compatibility remains evidence-bounded. |
| P25 Phase II | **Implemented / Runtime-Verified / Regression-Tested / Experimental** | Grant transport, burst sync, complete-burst retention, DUID classification, and hardware observation exist. Payload decode and AMBE+2 voice/audio are Not Implemented. |
| ADS-B 1090 | **RF-Verified / Hardware-Verified / Regression-Tested** | Live CRC-valid traffic was independently track-compared and FAA-enriched. It is live-only and reception depends on antenna, location, and traffic. |
| LoRa/Meshtastic receive | **Hardware-Verified / Experimental** | Native receive and dashboard paths exist and traffic has been observed. Reliability, missed-packet rate, backlog behavior, and antenna coverage remain bounded experiments. |
| POCSAG | **RF-Verified at 1200 baud / Regression-Tested** | `TEST` for CAPCODE 1234560 was decoded on Tab5 from an in-house 433.920 MHz source and independently on a Flipper Zero. 512/2400 are host-tested only. Identity/message state is RAM-only; persistent searchable archive is Not Implemented. |
| RF Lab and RF Visualizer | **Implemented / Regression-Tested** | Integrated screens and self-check/regression tooling exist; they do not prove RF calibration. |
| Wi-Fi analysis | **Implemented / Hardware-Verified / Experimental** | ESP-Hosted 3.0.6 and access-point survey work on the owner Tab5. Scan, connect, power-off, and catalog I/O deliberately pause and then resume radio reception. |
| Signed data catalog | **Hardware-Verified / Experimental** | Public `data-catalog-v1` exists; FAA catalog reinstall and radio recovery are recorded. This does not mean every proposed pack is published or accepted. |
| LAN web console | **Implemented / Experimental** | Opt-in HTTP telemetry, audio, spectrum, tuning, volume/mute, span/step, and dashboard actions. No TLS or authentication; trusted LAN only. |
| Android TV client | **Implemented / Experimental** | LAN client only; the Tab5 remains the radio. |
| Global Settings and documentation capture | **Implemented / Hardware-Verified** | Current screens are integrated under the display owner; exact capture coverage remains release/evidence specific. |

## Automated validation

| Workflow/check | Current coverage |
|---|---|
| P25 core | Optimized and ASan/UBSan host tests plus data-catalog validation. |
| Radio scan core | Optimized and ASan/UBSan radio-session/scan tests. |
| User guide | Help-media validation, documentation validation, and strict MkDocs build. |
| Documentation Truth | Deterministic dependency/doc coherence, CI claims, architecture measurement, local links, screen/dashboard enums, resolved stale claims, and history/prompt hygiene. |

Current CI does **not** compile the native Tab5 consumer firmware. POCSAG core
and store scripts are not currently wired into GitHub Actions. CI also does not
prove hardware operation, RF reception, antenna suitability, display/touch
behavior, release-package installation, or long-duration soak behavior.

## Current evidence boundaries and open limitations

- The physical Tab5 evidence is for the owner unit, ESP32-P4 revision 1.3. Other
  Tab5/display revisions are Not Verified.
- Current `main` is source-reviewed at the snapshot above; RC4 hardware evidence
  applies to the immutable RC4 package, not automatically to post-release commits.
- Post-RC4 receiver-recovery behavior, current M5Burner search visibility, V4L,
  broad earlier-V3 support, repeatable Nooelec RF, and long current-main soak are
  Not Verified from committed public evidence.
- Shortwave, Airband, Marine, Satellite, and CB do not have broad current-release
  RF acceptance. P25 Phase II voice/audio is Not Implemented.
- Wi-Fi credentials and signing material are private and are not documentation
  or CI inputs.

## Authoritative document map

| Purpose | Document |
|---|---|
| Public summary | [`README.md`](README.md) |
| Current evidence | This file |
| Software ownership | [`architecture.md`](architecture.md) |
| User operation and safety | [`docs/user-guide/`](docs/user-guide/index.md) |
| Security | [`SECURITY.md`](SECURITY.md) |
| Developer contracts | [`docs/API_ESP_RTL_SDR.md`](docs/API_ESP_RTL_SDR.md), [`docs/TAB5_BUILD_POLICY.md`](docs/TAB5_BUILD_POLICY.md), [`docs/RADIO_CONFIGURATION.md`](docs/RADIO_CONFIGURATION.md) |
| Future work | [`Roadmap.md`](Roadmap.md) |
| Exact-version evidence | [`docs/releases/`](docs/releases/v0.2.0-beta.6-multidongle-rc4.md) and dated validation reports |
