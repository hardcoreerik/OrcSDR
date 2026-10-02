# OrcSDR current project status

Current source snapshot: **2026-10-02**, on branch `claude/beta2-prep`
(release preparation for `v0.3.0-beta.2`).

Current release candidate: **`v0.3.0-beta.2`**. It is published only after the exact-tag hardware gate in
[`docs/M5BURNER_HARDWARE_GATE.md`](docs/M5BURNER_HARDWARE_GATE.md) passes; the release page records the
result. The previous release, `v0.2.0-beta7`, had its exact M5Burner package installed and booted on the
owner Tab5 before publication. That evidence does not automatically prove later `main` commits or other
hardware. Version numbers follow [`docs/VERSIONING.md`](docs/VERSIONING.md); release notes are in
[`docs/releases/v0.3.0-beta.2.md`](docs/releases/v0.3.0-beta.2.md).

This release carries the Stage-1 optimized production DSP path (formerly the development branch
`claude/dsp-multirate`, PR #114). Stage-2 D/D2/D3 frontends are **Experimental**, compiled only with
`ORCSDR_DSP_LAB=1`, and are not WFM audio integrations. The dated
[DSP closeout](docs/validation/dsp-stage1-stage2-closeout-2026-09-27.md) records their evidence.

The release pins `esp_rtl_sdr` at `31a159df4f006a837d5041029bc6d1bc63520234`, the commit of the published driver release
**`v0.9.3`** (the driver reports `0.9.3`). It contains the PC-measured V4L/V4/V3c gain, AGC, bias and
tuner-bandwidth controls, the V4L HF upconverter route, an optional direct HF route that OrcSDR uses on CB
for the V4L and V4, and the live tuner-bandwidth fixes documented in
[`docs/testing/v3c-tuner-bandwidth-transition-2026-09-28.md`](docs/testing/v3c-tuner-bandwidth-transition-2026-09-28.md).

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
| `esp-rtl-sdr` | published release `v0.9.3` (reports `0.9.3`), immutable pin `31a159df4f006a837d5041029bc6d1bc63520234` in the manifest and lock file |
| USB implementation | Current `esp-rtl-sdr` path; the legacy USB source is compiled out but remains in source. |
| Radio policy | Receive-only. Transmission is Not Implemented. |

## Receiver evidence

| Receiver | Status | Boundary |
|---|---|---|
| RTL-SDR Blog V4 | **RF-Verified / tested baseline** | Primary release receiver. |
| RTL-SDR Blog V3C | **RF-Verified / Experimental** | One V3C passed RC4 detection, initialization, streaming, FM/RDS, retune, hotplug, and USB/battery boot, and on 2026-09-28 the live tuner-bandwidth cycle (see below). Gain and sensitivity comparisons remain provisional. |
| Earlier RTL-SDR Blog V3 variants | **Implemented / Experimental** | Profile exists; the V3C result is not a blanket earlier-V3 compatibility claim. |
| Nooelec NESDR SMArt V5 | **Implemented / Experimental** | Detection and streaming are provisional; repeatable RF reception is Not Verified. |
| RTL-SDR Blog V4L | **Hardware-Verified / Experimental** | One V4L was recognised and streamed FM at 96.1 MHz on the owner Tab5 and passed the live tuner-bandwidth cycle (hot-swapped in from a V3c session, and after unplug/replug) on 2026-09-28; on the installed v0.3.0-beta.1 package two V4L cold boots with the dongle attached kept the carrier within 3.7 kHz, except that the first read -6.2 and -7.8 kHz at 200k and 300k (start-up -3.4 kHz); the second cold boot did not repeat it and the cause of the first is not established. Its other behaviour is bounded to the driver's dated evidence and is not otherwise claimed here. |
| Other receivers | **Unsupported / Not Verified** | Generic RTL2832 compatibility is not claimed. |

## Capability and evidence matrix

| Capability | Status | Evidence boundary and limitations |
|---|---|---|
| FM receive, stereo, RDS, presets | **RF-Verified / Regression-Tested** | Verified on Blog V4 and V3C. Results remain bounded to the tested dongle, antenna, band, and setup. |
| AM broadcast dashboard and 119-channel scan | **Hardware-Verified / Experimental** | The exact RC4 package exercised the dashboard and scan. General reception quality and gain calibration are not established. |
| NOAA Weather Radio | **Implemented** | Current-release RF acceptance is Not Verified. |
| CB | **Implemented / Runtime-Verified** | The earlier channel panel was flashed and exercised. The band-wide scanner dashboard is **Implemented / Regression-Tested** (host scanner tests) and still needs Tab5 hardware and RF acceptance. |
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
| Live tuner bandwidth (RF Lab NEXT BW) | **Hardware-Verified / Regression-Tested / Experimental** | On the owner Tab5 at 96.1 MHz the AUTO, 200k, 300k, 500k, 1.0M, 1.8M, 2.4M, AUTO cycle kept the carrier within 2.5 kHz on V3c, V4L and V4 in the pre-release runs (V3c cold boot; V4L, V4 swapped in; all after unplug/replug); on the installed package the V3c stayed within 1.8 kHz (hot-swap and cold boot) and the V4L within 3 kHz after a hot-swap and within 3.7 kHz on a second cold boot (one cold boot read up to 7.8 kHz at 200k/300k and did not repeat; unexplained); in the pre-release runs final AUTO matched the boot passband within 0.8 dB (one V3c run 3.6 dB, repeated within 0.4 dB); on the installed package the V3c AUTO level differed from boot by 0.3 to 2.1 dB (cold boot) and up to 4.4 dB (hot swap), and the V4 stayed within 1.2 kHz but one 1.8M step was rejected once by the USB bus (six repeats worked; #122). The tuner widths are the PC driver's control values, not measured analog passbands. The V4 was not measured before the fix; other stations, long soak, and RDS/pilot after a retune on the V3c are Not Verified. Evidence: [`docs/testing/v3c-tuner-bandwidth-transition-2026-09-28.md`](docs/testing/v3c-tuner-bandwidth-transition-2026-09-28.md). FM DSP filter bandwidth is a separate, app-side control. |
| Stage-1 production DSP optimization (formerly PR #114) | **Hardware-Verified / Regression-Tested** | Same saved live IQ gave bit-identical FM, NFM/WX, AM, CB AM/LSB/USB output/state; release-build 2.40 MS/s WFM load was about 45% versus 68% before. Measured on the development branch; part of `v0.3.0-beta.1`, whose package acceptance is the release hardware gate. |
| Higher-rate IQ acquisition (formerly PR #114) | **Hardware-Verified / Experimental** | The driver delivered 2.40/2.56/2.88/3.20 MS/s in bounded transport windows. This is not audio or every spectrum/analysis workload acceptance. |
| 3.20 MS/s WFM audio | **Unsupported / Experimental research** | D2 and D3 exclusive-live frontends bypassed production audio and showed backlog/loss. Normal WFM stays at 2.40 MS/s. |
| 3.20 MS/s NFM/WX audio | **Not Verified / Experimental** | No mode-specific production audio integration or RF acceptance; this does not establish impossibility. |
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
- Current `main` is source-reviewed at the snapshot above. Hardware evidence applies to the exact
  packages and builds named in each row and in the dated reports, not automatically to later commits.
- Post-RC4 receiver-recovery behavior, current M5Burner search visibility, V4L
  behaviour beyond the tuner-bandwidth cycle, broad earlier-V3 support, repeatable Nooelec RF, and long current-main soak are
  Not Verified from committed public evidence.
- Shortwave, Airband, Marine, Satellite, and CB do not have broad current-release
  RF acceptance. P25 Phase II voice/audio is Not Implemented.
- Wi-Fi credentials and signing material are private and are not documentation
  or CI inputs.
- In this release, `rtl_default_sample_rate()` selects 2.40 MS/s for WFM,
  NFM/Weather, AM, CB, Shortwave and Browse; P25/LoRa/POCSAG default to
  960 kS/s and ADS-B to 2.048 MS/s. These device acquisition rates are not
  the 240 kS/s internal WFM MPX or 48 kHz output-audio rates. RF Lab has an
  existing explicit custom-rate control; it does not confer audio support.
- The 2026-09-27 normal-firmware smoke of the DSP work (then PR #114) found an unresolved startup
  audio anomaly: FM was
  initially silent even after unplugging an audio cable, although a built-in
  tone played; reboot restored audible 96.1 MHz FM, stereo lock and KZEL RDS.
  The operator then confirmed audio in Weather/NFM, AM and CB AM/LSB/USB
  (without CB traffic), plus FM stop/restart and retune recovery. The cause
  of the initial silence/reboot dependence is Not Verified. Hosted Wi-Fi
  initialization also failed this run despite a matching C6 version. Neither
  result should be silently promoted to release acceptance.
- The operator reports degraded sound/DSP behavior on RF Lab LIVE during FM.
  A clean RF Lab window showed 64% DSP load, but no new drops or backlog;
  ordinary FM audio was clear after cycling AM/CB/FM, and the issue did not
  recur on returning to RF Lab. This is not yet a diagnosed or fixed RF Lab
  regression.

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
| Version numbers and release process | [`docs/VERSIONING.md`](docs/VERSIONING.md) |
| Exact-version evidence | [`docs/releases/`](docs/releases/v0.3.0-beta.1.md) and dated validation reports |
