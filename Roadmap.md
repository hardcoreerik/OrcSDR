# OrcSDR roadmap

This file lists **remaining work**, not features absent from OrcSDR. Current capability
and evidence live in [`PROJECT_STATUS.md`](PROJECT_STATUS.md); firsthand operational
results are recorded in the [2026-11-04 field acceptance](docs/MAINTAINER_FIELD_ACCEPTANCE_2026-11-04.md).
Completed milestones remain in tagged release notes and dated validation reports.
An unmeasured analog tolerance or missing soak test does **not** make an implemented
dashboard experimental.

## Near-term product gaps

### Revisit DSP rates by workload, not as a universal WFM frontend

Stage-2 multirate research is closed; its current Tab5 candidates did not
provide 3.20 MS/s WFM realtime headroom. Future, separately gated work could
use mode-specific lower channel rates (for example NFM/WX 48/96 kS/s and
lower-rate AM), a proper SSB channel/sideband filter, and exact rational
conversion only where useful. Separate high-rate acquisition for raw IQ,
spectrum, scans or multiple channels from the much lower rate of one audio
channel, and accept each workload independently.

If a concrete performance target justifies it, test fewer-tap/fused/direct-/4
coarse decimation or qualified P4 PIE/SIMD FIR code; do not rely on unqualified
HWLOOP behavior. The driver IQ copy/zero-copy idea is lower priority without
a measured bottleneck. Carry driver sequence/flags, pipeline-drop state and
retune/rate-transition markers through `RtlIqBlock` so later continuity
claims have explicit evidence. None of these are current capabilities.

### Reduce `main.cpp` ownership

Continue moving receiver lifecycle, band policy, Wi-Fi/catalog orchestration,
web commands, and screen/touch routing behind the modules that already exist.
The endpoint remains a small application-wiring file. Preserve one receiver
owner and one framebuffer owner during each extraction.

### Expand native Tab5 CI coverage

A native ESP-IDF Tab5 compile already runs in the path-filtered
[`sstv-tab5-build`](.github/workflows/sstv-tab5-build.yml) workflow and the
gated [nightly builder](.github/workflows/firmware-nightly.yml). Extend this
coverage to consistently gate all relevant firmware PR changes rather than
claim the build does not exist. Hardware, RF, flash, and physical UI acceptance
remain separate from compilation.

### Finish P25 Phase II receive

Grant transport, sync, complete-burst retention, DUID classification, and
hardware observation already exist. Future work is payload decoding and a
lawful AMBE+2 audio path, followed by bounded hardware/RF validation. Do not
label Phase II voice complete before those gates pass.

### Extend existing band-specific receivers (not first implementation)

- **Shortwave:** real AM/USB/LSB listening is maintainer-confirmed. Remaining: calibrated HF sensitivity, band edges, direct-sampling behavior and repeatable build/antenna acceptance.
- **Airband:** real arrival/departure aviation AM audio is confirmed, and a dated 119.600 MHz Tab5 reception record exists. Remaining: varied RF environments, squelch/audio characterization and scanner field-performance evidence.
- **Marine:** decide whether a dedicated workflow is justified and collect appropriate RF evidence; shared listening is already available.
- **Satellite:** define a specific decoder/receive target before building specialized UI; shared tuning already exists.
- **CB:** complete scanner/operator field acceptance with appropriate RF source and antenna; native AM/USB/LSB and scan controls are implemented.

Generic Browse routing is already implemented but does not confer any missing
special-purpose decoder. Do not present Shortwave or Airband as not yet receivable.

### Add POCSAG persistence deliberately

Current live and identity/message views are bounded RAM state. Future work may
add persistent CAPCODE identities, an SD-backed searchable archive, and editing
UI. Keep 512/2400 baud at host-tested status until live RF evidence exists.

### Broaden receiver quality evidence and E4000 user experience

**Blog V4, V3c, V4 Lite (V4L) and Nooelec NESDR SMArt V5 are fully supported in OrcSDR.**
The E4000/Nooelec XTR driver is fully implemented and **works in OrcSDR on frequencies it can tune**. The app correctly avoids tuning out-of-range bands (for example broadcast AM) even when their dashboard opens.

Remaining work: clearly show *why* an out-of-range dashboard cannot receive, conditionally disable controls that require unavailable tuner capabilities, and collect additional versioned E4000 band-edge/gain/soak evidence. Do not describe this as a missing driver or an inability to operate inside OrcSDR.
Earlier unqualified Blog V3 revisions and unknown RTL2832 devices still require their own identity and RF acceptance; no blanket family support claim.

### Improve Wi-Fi/radio coexistence

The current safe implementation pauses reception for scan/connect/power and
catalog operations. Remove that pause only if resource ownership and physical
regression evidence show concurrent operation is reliable.

### Complete data-pack coverage

The signed public `data-catalog-v1` and FAA reinstall evidence establish the
catalog mechanism, not every proposed pack. Publish additional FAA, NOAA, FCC,
map, or P25 packs only after provenance, redistribution, size, install,
rollback, and radio-recovery gates pass.

### Deepen digital decoder coverage and reliability

- **ACARS/VDL2:** live CRC-verified aircraft traffic is demonstrated; extend airline-specific payload interpretation, multipart message joining, unknown-format handling and representative RF/performance regression captures. Do not schedule VDL2 first reception as future work.
- **LoRa/Meshtastic:** real unencrypted messages, telemetry, node information and GPS have been decoded; encrypted frames are *detected* as `ENCRYPTED FRAME`. Extend packet-loss/coverage baselines and supported profiles without promising unauthorized plaintext recovery.
- **SSTV:** all 14 supported modes are operational per maintainer. Expand the per-mode/per-receiver recorded RF matrix and long-soak/storage fault acceptance rather than re-implement the existing decoder.
- **Dead Air:** real RF/event measurements, the working Tab5 Unit ENV III sensor and VEIL-26 symbolic engine are implemented. Continue device-specific sensor accuracy, multi-hour history and VEIL overlay/logging validation. The esoteric output remains **INTERPRETED**, never paranormal measurement or detection.

## Evidence still needed

- Current M5Burner search/catalog visibility and exact release-image acceptance.
- Post-RC4 receiver-recovery behavior and long-duration current-main soak.
- Other Tab5/display revisions beyond the owner ESP32-P4 revision 1.3 unit.
- Cross-band quantitative Shortwave/Airband calibration **beyond documented successful real reception**; CB scanner, Marine and Satellite evidence appropriate to their current scope.
- E4000 model-specific frequency-edge, gain/control and robustness data **beyond confirmed in-range operation**.
- Additional VDL2 application-layout/decoder coverage and SSTV mode-by-dongle recorded regression, without retracting proven functioning features.

## Follow-up cleanup

`apps/orcsdr-tab5/tools/patch_m5gfx.py` appears to retain obsolete
PlatformIO/NEONDRIVE-era assumptions. Confirm it has no native build dependency
before removing it in a separate source/tool cleanup change.
