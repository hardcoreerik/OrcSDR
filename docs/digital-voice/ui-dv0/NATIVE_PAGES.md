# UI-DV1 — native M5GFX Digital Voice pages and scanner-first workflow

**Branch:** `feature/dmr-digital-voice-foundation` · **Status:** native renderer + host behavioral contracts; **NOT yet wired to the firmware's main app/navigation and RF event services.** The dashboard is intentionally marked `available=false` in the registry so a dead-end screen cannot be selected as a functioning scanner.

## Pages (one shared dashboard, not one app per decoder)

| Tab | Main intent | Content and permitted actions |
|---|---|---|
| SCAN | Discover first, always | Current tuned/scan frequency, receiver and integrity evidence, scan progress, up to four protocol-qualified or candidate hits. START/STOP, HOLD/RESUME, SKIP, SAVE controls **only when app-supplied capability flags permit**. Source of these capabilities is the existing scan/radio owner, not this renderer. |
| LIVE | Understand a call | Source/destination only on validated identity; DMR CC and independently verified TS, P25 NAC, D-STAR verified callsigns, YSF integrity-checked FICH. NXDN48/96: validated RAN (including 0), RDCH/RCCH/RTCH, independently justified conventional/Type-C/Type-D topology and channel number. Unsupported voice stays unavailable. |
| LAST HEARD | Review evidence | Bounded 8-entry history, scroll/page through all entries, selected-event details, provenance, no invented UTC, aliases or IDs; only validated RAN shown. Storage not fabricated. |
| SYSTEMS | Identify sites without guessing | Up to four observed system candidates, protocol and channel frequency, verified NXDN Type-C/Type-D field only if supplied by an integrity-qualified decoder. Directory names, frequency plan and site geography remain unlinked. Save action disabled until storage owner supports it. |
| SIGNAL | Explain reliability | Lock/sync, integrity frames, corrected/failed FEC/CRC, IQ gaps, relative (uncalibrated) level; NXDN CAC CRC count only if a trusted service explicitly advertises measured CAC metrics. No fabricated spectrum/waterfall or SNR. |
| SETUP | Configuration and readiness | Receiver-owner status, scan-plan capability, protocol readiness, a real UI-only Discovery/Analysis density toggle; unsupported controls disabled. |

All layouts use 1280×720 geometry, body y>=104, OrcSDR palette, existing M5GFX `lgfx::LovyanGFX` calls, the shared-header reservation, and a single UI drawing owner. The advanced density has the same scan start/hold/skip controls. No receiver/USB/SD/audio callbacks run from M5GFX painting.

## Integration adapter (already built and tested on host)

- `digital_voice_dashboard_surface.{hpp,cpp}` uses `screens::Id::digital_voice` and `screens::owns()`; draws only when this screen owns the framebuffer and modal/volume overlay is closed. Entry calls a shared-header hook supplied by the app.
- The application remains responsible for the `ScreenController` transition, painting the existing `dashboard_audio_control` header, and passing valid immutable snapshots. The screen adapter owns the UI state and routes tab/history/density locally.
- `Surface::touch()` returns scan/hold/skip/save **requests** to the caller. It never retunes, starts scanning, changes USB ownership or writes NVS/SD. Validate actions once more at the receiver command boundary.
- This commit adds the native firmware sources to `apps/orcsdr-tab5/main/CMakeLists.txt` and adds a disabled registry descriptor. **It does not add main.cpp switches, duplicate decoder callbacks, a synthetic signal provider or live driver attachment.** Enabling the dashboard requires a later acceptance-gated application integration pass.

## NXDN evidence boundaries

The current NXDN foundation in `components/orcsdr_nxdn` is host-side synthetic-validated, with real-IQ acceptance pending. The dashboard does not assert Type-C/Type-D CAC CRC, group/source identities or AMBE+2 voice. `NxdnFacts` fields are presentation inputs only; frame-level validity belongs to the actual decoder/event adapter and may be displayed only when integrity-qualified. An unknown slot/RAN/ID remains unknown. RAN=0 is valid if independently decoded, and RAN is not by itself a unique site ID.

## Testing and remaining gates

Run the Release + ASan/UBSan host suite in `tests/digital_voice_dashboard_preview/README.md`. These tests use a mock that records genuine M5GFX API calls; they are not real-panel renders. A final UI integration must generate exact native screenshots on Tab5, prove repeatable pixel hashes on the same hardware/firmware state, validate shared header and touch, collect heap/stack/PSRAM and frame timings, and compare RTL-SDR IQ drop counters before/after the UI load. Do not declare native visual/performance acceptance from host tests.
