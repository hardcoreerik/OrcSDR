# UI-DV0: Scanner-first Digital Voice workspace

**Status:** Owner-authorized preproduction design/prototype, not a deployed scanner.
**Branch:** `feature/dmr-digital-voice-foundation`.
**Source architecture:** latest `origin/main` refactored Tab5 UI, M5Unified/M5GFX, existing single receiver/SDR owner.

## One workspace, not a dashboard per decoder

Digital Voice uses **SCAN / LIVE / LAST HEARD / SYSTEMS / SIGNAL / SETUP** as six views, not six independent receivers. SCAN is default. One user preference may switch between Discovery-first and Analysis-first information density. Amateur-radio/scanner users and commercial RF professionals are equally important. The promise is discover first without a codeplug, understand what was decoded, measure actual signal conditions, and remember verified observations offline.

## Scanner experience: highest product priority

1. **Plan:** user-selected local channel list, frequency range, or manually tuned channel, validated against supported tuner limits. Never imply a broad automatic search exists merely because a generic scan engine exists.
2. **Run:** explicit start/stop/hold; show actual channel progress, retune settling, scan current frequency, failure/abort reason and resume policy, only when the owning receiver service supplies them.
3. **Classify:** energy -> protocol candidate -> sync -> integrity-valid frame -> validated identity; evidence levels may not be silently promoted. Distinguish receiver retune, RF absence and decoder not-integrated.
4. **Hold:** stop on activity only under a tested dwell/hold policy; show hold reason and whether activity is protocol-qualified. One receiver cannot monitor separated control and voice channels simultaneously outside the acquired IQ bandwidth.
5. **Remember:** save channel or profile preview only with supported storage, keeping live observations, imported directory hypotheses and user annotations separate. Unknown frequency, time, slot, callsign and IDs remain unknown.
6. **After the call:** return to the plan through the existing receiver/scan policy, not a second tuner or USB owner. Display continuity/drop counters honestly.

### Current infrastructure and prototype truth

`apps/orcsdr-tab5/ui/scan_engine.hpp` already defines `scan::Engine`, `Plan`, `Progress`, and channel-list/frequency-range/window-sweep modes. OrcSDR also has functional P25 survey/follow controls. **Neither alone proves working multi-protocol Digital Voice auto-identification.**

The C++ `ScanStatus` in this commit is bounded presentation data only. `can_scan` is false unless a future authorized receiver adapter explicitly enables it; the START/STOP action is additionally gated on receiver ready/receiving. No scan thread, scan-plan persistence, P25 forwarding, decoder event adapter, or live USB/DSP integration is created by this patch. The normal first view clearly states `DIGITAL VOICE SCANNER NOT CONNECTED`. Host tests exercise a deterministic capability-enabled test state but this does **not** prove a real scan.

## Protocol-aware LIVE cards, common workflow

| Protocol | Show only with appropriate verified decoder evidence | Current engineering boundary |
|---|---|---|
| P25 Phase I | NAC, talkgroup, system/site and voice/follow metadata | Existing native Tab5 receive and voice; preserve P25 behavior. New shared UI adapter still pending |
| P25 Phase II | signaling/phase recognition with explicit uncertainty | Phase II voice unsupported |
| DMR | source ID, destination/talkgroup, group/private, CC, verified TS, sync/integrity | Native host decoder: actual real-IQ source `3024109`, TG `99`, CC `1`; MS-sync **time slot unknown**. Not live Tab5 integrated, no voice |
| D-STAR | verified MY/UR/RPT1/RPT2 header routing/callsigns | Hard-bit host header decoder, no real-IQ frontend/voice acceptance |
| System Fusion / YSF | integrity-checked FICH and separately supported calling metadata | Hard-bit host FICH; IQ frontend and voice unfinished |
| NXDN48 / NXDN96 | RAN, source/destination, conventional and independently qualified Type-C/Type-D | Planned; no decoder or trunking/voice capability established |

No DMR-shaped universal metadata struct should force unrelated protocol semantics. Preserve protocol-local field validity, evidence provenance and no guessed identity/location. Do not label encrypted or unknown traffic as playable clear voice. The current UI shell only implements selected protocol-fact presentations and planned/unknown states; its data adapters and fuller cards remain future scope.

## Implementation sequence and approval

1. Build **scanner receive adapter** on existing `radio_session` and `scan::Engine`: explicit scan policy, settle/hold/resume, retune epochs, user permissions, single owner, drop counts, control-state truth.
2. Attach approved DMR 48 kS/s IQ path read-only with bounded queue and P4 CPU/heap/PSRAM measurement, preserving native real-IQ conformance and P25 performance.
3. Define cross-protocol event envelope plus typed DMR, P25, D-STAR, YSF, NXDN details; evidence-gated historical logging and source-tagged candidates.
4. Extend offline Last Heard, source-qualified channel-learning, and non-destructive import/export with content-based duplicate detection. Use `sd_writer` for large SD writes.
5. Integrate protocol IQ frontends and any lawful clear-voice codec paths behind their own validated gates; NXDN Type-C and Type-D need separate acceptance.
6. Qualify on Tab5: native M5GFX capture from `UI_DOC_SHOW` / `UI_CAPTURE`, 1280x720 pixels and repeatable hashes, touch, shared header/volume tray, memory, flicker and IQ continuity.

The six generated visual designs are **AI-created concept mockups, not the byte/pixel output of this M5GFX code**. They must not be treated as native screenshots or placed in hardware-verified gallery folders. The stored manifest keeps `verified_images: []` until physical-device captures are completed. Unregistered module only: no dashboard ID, `main.cpp`, CMake firmware or decoder core modified; no PR/merge to `main`.
