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

Home's tune-anywhere routing (the GENERAL band, which replaced Browse) is a
general receiver, not a substitute for these band-specific outcomes.

### Make tuning content- and context-aware

Goal: tapping a spike on the spectrum, stepping, or typing a frequency should
leave the radio in the right mode, filter width and step, and say what the
signal probably is, without the operator needing to know the band plan. None of
this is a current capability beyond Home choosing wide FM, AM or narrow FM by
frequency.

1. **Band plan.** One table of frequency ranges with mode (AM, wide/narrow FM,
   USB, LSB, CW), default filter width, step size and a display name. Include
   the amateur conventions (LSB below 10 MHz, USB from 10 MHz up and on 60 m,
   CW at the band edges, FM on the 2 m/70 cm repeater segments), airband AM,
   broadcast AM/FM, CB, marine, FRS/GMRS, weather, ADS-B. Auto-mode is the default
   and a manual mode choice locks until the next band change (decide the lock
   behavior before building). Host-test the lookup like `band_plan`.
2. **Tap to snap.** On a spectrum tap, find the nearest peak, centre on it and
   estimate its occupied width (a ~200 kHz block is broadcast FM, a narrow carrier
   is AM/NFM, a carrier-less narrow signal is SSB) to confirm or override the
   table's choice.
3. **Context labels.** Show what the frequency is: airport and service from the
   aviation catalog, NOAA, marine and FRS/GMRS channels, ham band names. Decide
   where the label sits on Home and how it interacts with the existing mode chip.
4. **Real SSB.** USB/LSB demodulation (and CW) in the general path. Sideband
   demodulation exists only in the CB path today; this needs a proper channel
   and sideband filter (see the DSP-rates item above) and RF evidence.

### Support an external VFO (M5Dial)

Each Airband SCOPE control is a single dashboard action with a serial verb, so
a rotary controller needs only to send detent counts for the selected control.
Still to define: the transport (serial, ESP-NOW or Bluetooth), pairing/auth for
the controller, which controls it selects and cycles through, and how the
selected control is shown on screen. Extend the same action set to Home.

### Validate additional receivers

The Nooelec NESDR SMArt v5 has only a provisional driver profile with no gain
control and a frequency offset (issue 145). Needs a validated profile in the
driver (capabilities, IF/crystal), then RF acceptance against the Blog V4.

### Add POCSAG persistence deliberately

Current live and identity/message views are bounded RAM state. Future work may
add persistent CAPCODE identities, an SD-backed searchable archive, and editing
UI. Keep 512/2400 baud at host-tested status until live RF evidence exists.

### Expand receiver acceptance

Collect repeatable, versioned evidence for earlier Blog V3 variants, Nooelec
NESDR SMArt V5 RF reception, Blog V4L, and other explicitly selected devices.
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
