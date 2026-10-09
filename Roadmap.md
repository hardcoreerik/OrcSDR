# OrcSDR roadmap

This file contains future work only. Current capability and evidence live in
[`PROJECT_STATUS.md`](PROJECT_STATUS.md); completed milestones remain in tagged
release notes and dated validation reports.

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

### Compile the native Tab5 firmware in CI

The repository has P25, radio-scan, user-guide, and Documentation Truth
workflows. The remaining CI gap is a reproducible native ESP-IDF Tab5 consumer
build; hardware, RF, flash, and physical UI acceptance remain separate gates.

### Finish P25 Phase II receive

Grant transport, sync, complete-burst retention, DUID classification, and
hardware observation already exist. Future work is payload decoding and a
lawful AMBE+2 audio path, followed by bounded hardware/RF validation. Do not
label Phase II voice complete before those gates pass.

### Complete band-specific receiver experiences

- Shortwave: add correct AM/SSB modes and calibrated HF/direct-sampling evidence.
- Airband: add proper AM aviation voice behavior and suitable-antenna RF evidence.
- Marine: decide whether a dedicated workflow is justified and collect RF evidence.
- Satellite: define a specific receive/decoder target before adding a dedicated UI.
- CB: complete operator and RF acceptance with a suitable antenna/source.

Generic Browse routing is already implemented and is not a substitute for these
band-specific outcomes.

### Add POCSAG persistence deliberately

Current live and identity/message views are bounded RAM state. Future work may
add persistent CAPCODE identities, an SD-backed searchable archive, and editing
UI. Keep 512/2400 baud at host-tested status until live RF evidence exists.

### Expand receiver acceptance

Collect repeatable, versioned evidence for earlier Blog V3 variants, Blog V4L,
and other explicitly selected devices (the Nooelec NESDR SMArt V5 is tested at
24 MHz and above as of v0.3.0-beta.2).
Do not generalize the single V3C result or infer compatibility from detection.

### Improve Wi-Fi/radio coexistence

The current safe implementation pauses reception for scan/connect/power and
catalog operations. Remove that pause only if resource ownership and physical
regression evidence show concurrent operation is reliable.

### Complete data-pack coverage

The signed public `data-catalog-v1` and FAA reinstall evidence establish the
catalog mechanism, not every proposed pack. Publish additional FAA, NOAA, FCC,
map, or P25 packs only after provenance, redistribution, size, install,
rollback, and radio-recovery gates pass.

## Evidence still needed

- Current M5Burner search/catalog visibility.
- Post-RC4 receiver-recovery behavior on hardware.
- Other Tab5/display revisions beyond the owner ESP32-P4 revision 1.3 unit.
- Long-duration current-main soak and repeatable receiver recovery.
- Current-release RF acceptance for Shortwave, Airband, Marine, Satellite, and CB.

## Follow-up cleanup

`apps/orcsdr-tab5/tools/patch_m5gfx.py` appears to retain obsolete
PlatformIO/NEONDRIVE-era assumptions. Confirm it has no native build dependency
before removing it in a separate source/tool cleanup change.
