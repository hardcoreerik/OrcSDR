# OrcSDR current project status

Current documentation snapshot: **2026-11-04**, based on the `main` source and dated validation reports.

Latest published versioned release at this review: **[`v0.5.0-beta.1`](https://github.com/hardcoreerik/OrcSDR/releases/tag/v0.5.0-beta.1)** (published 2026-10-26 PDT). See [the release notes](docs/releases/v0.5.0-beta.1.md). Source, release asset, nightly and installed hardware have distinct acceptance boundaries; new `main` changes are not automatically proved by a prior tagged package. See [downloads](docs/user-guide/downloads.md) and [version policy](docs/VERSIONING.md).

Current source includes the Stage-1 optimized production DSP path (formerly the development branch
`claude/dsp-multirate`, PR #114). Stage-2 D/D2/D3 frontends remain **lab-only research**, compiled only with
`ORCSDR_DSP_LAB=1`, and are not WFM audio integrations. The dated
[DSP closeout](docs/validation/dsp-stage1-stage2-closeout-2026-09-27.md) records their evidence.

Current `main` pins `esp_rtl_sdr` at `43474869e061a78c209091f9b3a65c498ae64fb4`, the commit of the published driver release
**`v0.9.4`** (the driver reports `0.9.4`). It contains the PC-measured V4L/V4/V3c gain, AGC, bias and
tuner-bandwidth controls, the V4L HF upconverter route, an optional direct HF route that OrcSDR uses on CB
for the V4L and V4, and the live tuner-bandwidth fixes documented in
[`docs/testing/v3c-tuner-bandwidth-transition-2026-09-28.md`](docs/testing/v3c-tuner-bandwidth-transition-2026-09-28.md).

This is the authoritative current capability and evidence summary. The [native ESP32-P4 decoder inventory](docs/NATIVE_DECODER_INVENTORY.md) distinguishes protocol decoders from demodulators, supporting measurements and development-branch work. The
[2026-11-04 maintainer field-acceptance reconciliation](docs/MAINTAINER_FIELD_ACCEPTANCE_2026-11-04.md)
adds firsthand reception/operation reports alongside dated instrumented evidence. Release
notes and validation reports are immutable, dated evidence; they do not
override this document for current state. Future work belongs in
[`Roadmap.md`](Roadmap.md).
For current-facing status wording, see [status terminology](docs/STATUS_TERMINOLOGY.md)
and the [2026-11-04 documentation audit](docs/DOCUMENTATION_STATUS_AUDIT_2026-11-04.md).

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
| `esp-rtl-sdr` | published release `v0.9.4` (reports `0.9.4`), immutable pin `43474869e061a78c209091f9b3a65c498ae64fb4` in the manifest and lock file |
| USB implementation | Current `esp-rtl-sdr` path; the legacy USB source is compiled out but remains in source. |
| Radio policy | Receive-only. Transmission is Not Implemented. |

## Receiver evidence

**Receiver support policy (maintainer classification, 2026-11-04):** Blog V4, Blog V3c, Blog V4 Lite (V4L), and Nooelec NESDR SMArt V5 are **fully supported in OrcSDR**. This means the receiver profiles and core application integration are accepted; it does **not** turn untested dashboard behavior, RF accuracy, frequency-edge cases, or historical release tests into verified results. The dated evidence and caveats below remain valid.

| Receiver | OrcSDR support | Evidence / remaining scope |
|---|---|---|
| RTL-SDR Blog V4 | **Fully supported / RF-Verified** | Primary historical test baseline; integrated HF upconverter path. |
| RTL-SDR Blog V3c | **Fully supported / RF-Verified** | Detection, initialization, IQ, FM/RDS, retune, hotplug, boot and tuner-bandwidth tests were recorded on physical V3c hardware. Gain/sensitivity comparisons are separate calibration questions, not a receiver-support blocker. |
| RTL-SDR Blog V4 Lite (V4L) | **Fully supported / Hardware-Verified** | Physical FM streaming, hot-swap, cold-boot and live tuner-bandwidth acceptance recorded; historical start-up frequency anomalies remain documented below. [40 m FT8 reception](docs/user-guide/dashboards/ft8.md) was also accepted. |
| Nooelec NESDR SMArt V5 | **Fully supported / RF-Verified** | Physical streaming, band-select and replug checks recorded; owner accepted [40 m FT8 at 7.074 MHz](docs/user-guide/dashboards/ft8.md) on 2026-10-09. General HF calibration/AM performance and external-upconverter setups are separate evidence gates. |
| Earlier RTL-SDR Blog V3 variants | **Experimental / individually verify** | A V3 family profile exists, but V3c qualification does not automatically qualify all earlier hardware revisions. |
| Nooelec NESDR SMArTee XTR / E4000 | **Driver fully implemented / OrcSDR works within tuner range** | The independent `esp-rtl-sdr` driver identifies, initializes, streams and tunes E4000 on supported frequencies; actual use in OrcSDR is maintainer-confirmed. Out-of-range dashboards (for example, broadcast AM, which E4000 cannot tune natively) may open, but do not initiate an invalid tune. OrcSDR does not currently configure an external converter for that band. Remaining work is consistent dashboard frequency/capability messaging and extended RF/band-edge calibration, **not** basic driver bring-up. See [field acceptance](docs/MAINTAINER_FIELD_ACCEPTANCE_2026-11-04.md). |
| Other receivers | **Unsupported / Not Verified** | Generic RTL2832 compatibility is not claimed. |

## Capability and evidence matrix

| Capability | Status | Evidence boundary and limitations |
|---|---|---|
| FM receive, stereo, RDS, presets | **RF-Verified / Regression-Tested** | Verified on Blog V4 and V3C. Results remain bounded to the tested dongle, antenna, band, and setup. |
| AM broadcast dashboard and 119-channel scan | **Hardware-Verified / Implemented** | Native AM receiver and scanning controls were exercised on Tab5 in the RC4 evidence. Sensitivity, gain calibration and broad-station reception remain distinct RF quality checks. |
| NOAA Weather Radio | **Implemented / Available** | NFM weather-channel tuning and listening are available. Current-release off-air acceptance is not independently recorded here. |
| CB | **Implemented / Runtime-Verified** | Native AM/USB/LSB CB reception and 40-channel scanner controls exist. Operator and RF acceptance for the complete scanner remains open; do not confuse that with an absent dashboard. |
| Shortwave | **Implemented / RF received (maintainer-confirmed)** | Shortwave Explorer supports AM, USB and LSB and the maintainer confirms real signals have been received and listened to. Band-plan AUTO, filters, clarifier, memories and logging are implemented; see [Shortwave changes](docs/shortwave/CHANGELOG.md). No blanket sensitivity or every-band/dongle calibration claim. |
| Airband | **Implemented / Live RF voice received** | Dedicated aviation AM workspace with 25/8.33 kHz tuning and scanner is operational. The maintainer has listened to actual arrival/departure traffic; [2026-09-30 Airband evidence](docs/airband/README.md) documents Tab5 voice and comparison with LiveATC at 119.600 MHz. General squelch/antenna/scan performance varies by environment. |
| Marine | **Implemented / Limited** | Shared NFM receiver routing exists; a dedicated marine decoder/dashboard and broad RF validation are not claimed. |
| Satellite | **Implemented / Limited** | Shared tuning/receiver route near 137.5 MHz exists; no dedicated satellite decoder/dashboard is claimed. |
| P25 Phase I | **RF-Verified / Hardware-Verified / Regression-Tested** | Documented control, clear voice, encrypted-call detection/muting, and follow behavior. System compatibility remains evidence-bounded. |
| P25 Phase II | **Partially implemented / Under development** | Grant transport, burst sync, retention, DUID classification and hardware observation exist; complete payload decoding and AMBE+2 voice/audio are not implemented. Phase I acceptance does not imply Phase II voice support. |
| ADS-B 1090 | **RF-Verified / Hardware-Verified / Regression-Tested** | Live CRC-valid traffic was independently track-compared and FAA-enriched. It is live-only and reception depends on antenna, location, and traffic. |
| LoRa/Meshtastic receive | **Implemented / Real-world packet decoding confirmed** | Native LongFast receive has decoded unencrypted Meshtastic messages, telemetry, node info and GPS/location fields on real traffic, per maintainer. Encrypted frames are detected and displayed as **ENCRYPTED FRAME**, with no plaintext fabricated or claimed. Receiver remains passive; profile, coverage and packet-loss limits apply. See [LoRa receive](docs/lora/README.md). |
| POCSAG | **RF-Verified at 1200 baud / Regression-Tested** | `TEST` for CAPCODE 1234560 was decoded on Tab5 from an in-house 433.920 MHz source and independently on a Flipper Zero. 512/2400 are host-tested only. Identity/message state is RAM-only; persistent searchable archive is Not Implemented. |
| FT8 receive | **RF-Verified / Regression-Tested** | Successful 40 m reception was accepted on V4, V4L, V3c and Nooelec V5 on 2026-10-09; this does not certify all HF propagation conditions. See [reception evidence](docs/user-guide/dashboards/ft8.md). |
| SSTV receive | **Implemented / 14 supported modes operational (maintainer-confirmed)** | Maintainer confirms the supported SSTV mode set works. All 14 have host regression coverage. [2026-10-26 conducted RF tests](docs/evidence/maps/2026-10-26-v4l-sstv-conducted.md) specifically passed Robot 36, PD120 and Scottie S1 image/SD output on V3c/V4/V4L/Nooelec V5; do not imply all 14 modes had that exact per-dongle test. |
| ACARS VHF | **Implemented / Live RF decode confirmed** | Working VHF ACARS receiver and archived messages. The initial 2026-10-15 two-block result is historical; later [2026-10-29 ACARS records](docs/acars/CHANGELOG.md) include substantial live traffic and aircraft-specific interpretation. Unknown payload layouts remain raw, not fabricated interpretations. |
| VDL Mode 2 (VDL2) receive | **Implemented / Live RF decode confirmed** | The native VDL2 receiver decoded on-device CRC-valid AVLC frames and ACARS; a later 136.975 MHz session recorded **213 verified frames and 135 ACARS messages**. Position and climb/altitude data have been parsed and compared against external aircraft tracking; airline-specific payload coverage remains partial. See [ACARS development evidence](docs/acars/CHANGELOG.md) and [maintainer reconciliation](docs/MAINTAINER_FIELD_ACCEPTANCE_2026-11-04.md). |
| Dead Air + Unit ENV III | **Implemented / Sensor hardware working (maintainer-confirmed)** | Real receiver observations, event thresholds/logs and M5Stack Unit ENV III temperature/humidity/pressure measurements on Tab5. Invalid/stale/unplugged data is suppressed. These measurements do not establish paranormal causes. See [Dead Air changes](docs/dead-air/CHANGELOG.md). |
| VEIL-26 esoteric analysis | **Implemented / Deterministic symbolic interpretation** | Opt-in analysis derives letters/words/numerology/rune displays from measured frequency and signal level using a documented reproducible recipe. Symbols are **INTERPRETED**, not newly measured signals or paranormal detections. VEIL-26-specific hardware acceptance remains evidence-bounded; see [VEIL-26](docs/dead-air/VEIL26.md). |
| RF Lab and RF Visualizer | **Implemented / Regression-Tested** | Integrated spectrum, receiver controls, self-check, recording and measurement tools. Raw/relative readings are not substitutes for calibrated RF instruments. |
| Live tuner bandwidth (RF Lab NEXT BW) | **Implemented / Hardware-Verified / Regression-Tested** | On the owner Tab5 at 96.1 MHz the AUTO, 200k, 300k, 500k, 1.0M, 1.8M, 2.4M, AUTO cycle kept the carrier within 2.5 kHz on V3c, V4L and V4 in the pre-release runs (V3c cold boot; V4L, V4 swapped in; all after unplug/replug); on the installed package the V3c stayed within 1.8 kHz (hot-swap and cold boot) and the V4L within 3 kHz after a hot-swap and within 3.7 kHz on a second cold boot (one cold boot read up to 7.8 kHz at 200k/300k and did not repeat; unexplained); in the pre-release runs final AUTO matched the boot passband within 0.8 dB (one V3c run 3.6 dB, repeated within 0.4 dB); on the installed package the V3c AUTO level differed from boot by 0.3 to 2.1 dB (cold boot) and up to 4.4 dB (hot swap), and the V4 stayed within 1.2 kHz but one 1.8M step was rejected once by the USB bus (six repeats worked; #122). The tuner widths are the PC driver's control values, not measured analog passbands. The V4 was not measured before the fix; other stations, long soak, and RDS/pilot after a retune on the V3c are Not Verified. Evidence: [`docs/testing/v3c-tuner-bandwidth-transition-2026-09-28.md`](docs/testing/v3c-tuner-bandwidth-transition-2026-09-28.md). FM DSP filter bandwidth is a separate, app-side control. |
| Stage-1 production DSP optimization (formerly PR #114) | **Hardware-Verified / Regression-Tested** | Same saved live IQ gave bit-identical FM, NFM/WX, AM, CB AM/LSB/USB output/state; release-build 2.40 MS/s WFM load was about 45% versus 68% before. Measured on the development branch; part of `v0.3.0-beta.1`, whose package acceptance is the release hardware gate. |
| Higher-rate IQ acquisition (formerly PR #114) | **Hardware-Verified / Bounded rates** | The driver delivered 2.40/2.56/2.88/3.20 MS/s in bounded transport windows. This is not audio or every spectrum/analysis workload acceptance. |
| 3.20 MS/s WFM audio | **Not supported** | D2 and D3 exclusive-live frontends bypassed production audio and showed backlog/loss. Normal WFM stays at 2.40 MS/s. |
| 3.20 MS/s NFM/WX audio | **Not validated** | No mode-specific production audio integration or RF acceptance; this does not establish impossibility. |
| Wi-Fi analysis | **Implemented / Hardware-Verified** | Real AP surveys through ESP-Hosted 3.0.6 were exercised. Wi-Fi scan/connect and catalog I/O intentionally pause an active radio session; this is an operational limitation, not an experimental label on the survey. |
| Signed data catalog | **Implemented / Hardware-Verified** | The signed `data-catalog-v1` flow and FAA reinstall/recovery have physical evidence. Individual proposed packs are only available if published and accepted. |
| LAN web console | **Implemented / Available (trusted LAN)** | HTTP telemetry, audio, spectrum, tuning and dashboard controls are implemented. No TLS or authentication: use only on a trusted LAN; do not expose it to the public Internet. |
| Android TV client | **Implemented / Companion** | LAN client UI is implemented; the Tab5 remains the receiver. Full device/network compatibility testing is separate. |
| Global Settings and documentation capture | **Implemented / Hardware-Verified** | Current screens are integrated under the display owner; exact capture coverage remains release/evidence specific. |

## Automated validation

| Workflow/check | Current coverage |
|---|---|
| P25 core | Optimized and ASan/UBSan host tests plus data-catalog validation. |
| Radio scan core | Optimized and ASan/UBSan radio-session/scan tests. |
| User guide | Help-media validation, documentation validation, and strict MkDocs build. |
| Native Tab5 firmware | Path-filtered SSTV integration compile and gated nightly ESP-IDF builds; not an every-PR hardware/compile acceptance contract. |
| Documentation Truth | Deterministic dependency/doc coherence, CI claims, architecture measurement, local links, screen/dashboard enums, resolved stale claims, and history/prompt hygiene. |

Native ESP-IDF Tab5 firmware builds are performed by the path-filtered
[`sstv-tab5-build`](.github/workflows/sstv-tab5-build.yml) workflow and by
[`firmware-nightly`](.github/workflows/firmware-nightly.yml) when its change/build gate fires.
These are not universal compile gates for every pull request or source change.
POCSAG core/store scripts are not currently guaranteed by the named validation
workflows. Automated checks cannot by themselves prove live RF, antenna suitability, physical
screen/touch behavior, release-package installation or long-duration soak.

## Current evidence boundaries and open limitations

- The physical Tab5 evidence is for the owner unit, ESP32-P4 revision 1.3. Other
  Tab5/display revisions are Not Verified.
- Current `main` is source-reviewed at the snapshot above. Hardware evidence applies to the exact
  packages and builds named in each row and in the dated reports, not automatically to later commits.
- Historical release-specific receiver-recovery and frequency-edge cases, current M5Burner search visibility,
  broad earlier-V3 hardware coverage, calibrated Nooelec HF/AM reception, E4000 cross-dashboard
  frequency-limit messaging and full-band RF characterization, and long current-main soak still have
  bounded or incomplete committed public evidence. E4000 already operates on supported frequencies. These are separate
  from the accepted full receiver support status of V4, V3c, V4L, and Nooelec V5.
- The maintainer confirms real Shortwave listening and intelligible Airband arrival/departure
  voice. These do not imply every signal, antenna, band or build was independently tested.
  Live Meshtastic packet/VDL2 decoding, SSTV mode support and ENV III operation are detailed
  in the [field acceptance record](docs/MAINTAINER_FIELD_ACCEPTANCE_2026-11-04.md).
  Marine and Satellite currently use shared receiver routes. P25 Phase II voice/audio is not implemented.
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
