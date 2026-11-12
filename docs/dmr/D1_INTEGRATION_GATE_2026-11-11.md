# D1 / F0c integration gate — three-agent handoff (2026-11-11)

**Branch:** `feature/dmr-digital-voice-foundation`  
**Baseline before the three-agent deliveries:** `d2383efac4f73572341ed904b31d7b5e48afd6dc`  
**Verified branch head at review:** `15ae8080257d2abb9460d753fd4b50e904cdb724`  
**Status:** three scoped implementations committed; **no combined decoder validation, device build, device SD validation, or real-IQ OrcSDR metadata acceptance**.

This report is a source/interface review and integration contract, not a new benchmark or independent execution of the agent tests. No production P25/firmware files were changed.

## Deliveries and boundaries

| Area | Location | Handoff | Gate status |
| --- | --- | --- | --- |
| F0c microSD SlotIO | `components/orcsdr_radio_discovery/esp_idf/**` | Opt-in VFS-backed two-slot adapter, bounded read/write, error reporting, POSIX host fault tests; physical power-loss testing absent | HOST SOURCE DELIVERED; DEVICE NOT RUN |
| D1-A channel front-end | `components/orcsdr_dmr/d1_frontend/**` | 48 kS/s complex-float IQ to 4,800 hard dibits/s, 4FSK discriminator/timing/CFO and D0-sync-assisted acquisition | SYNTHETIC HOST EVIDENCE REPORTED |
| D1-B Tier II protocol | `components/orcsdr_dmr/d1_protocol/**` | Dibit stream to framed sync/CACH/slot type/FEC/RS/CRC/LC and late-entry identity events | SYNTHETIC HOST EVIDENCE REPORTED |

The branch is diverged from `main` as of this review. Do **not** merge or rebase `main` inside these independent workstreams.

## Blocking interface mismatch — cold-start synchronization

The front-end contract explicitly **begins emitting dibits only after its acquisition SYNC word**, while `d1_protocol::Decoder::feed()` expects a *contiguous transmitted-order* stream containing the entire 132-dibit DMR burst, including 54 leading payload dibits, 24 SYNC dibits and 54 trailing payload dibits. The protocol decoder scans the full burst for the central sync. Merely piping `Frontend::Symbol::dibit` directly into `Decoder::feed()` therefore **cannot decode the initial acquired burst**.

Next implementation must address this explicitly; two sensible designs:

1. **Preferred for DX and short calls:** provide a bounded front-end acquisition pre-roll with enough retained, timed discriminator/symbol samples to recover the 54 pre-sync payload dibits, the sync dibits and—when necessary—the 12-dibit preceding base-station CACH. Present a coherent continuous dibit stream with exact sample timestamps and reset/discontinuity signals to the protocol. The existing 512-float filtered history is insufficient to assume retention of every preceding 66 dibits at nominal ten samples/s. Specify and measure the new buffer limit.
2. **Restricted first integration:** explicitly mark the acquired first burst lost, reconstruct the *known* acquisition sync only as a sync hint (not valid payload/CRC), feed subsequent hard decisions, and require the next complete transmitted burst for valid metadata. This cannot be called full late-entry or short-burst acquisition. Do not fabricate pre-sync payload or a validated identity.

Whichever path is chosen, create an actual **frontend→protocol host integration test**, not just two passing standalone synthetic suites. Validate burst alignment, CACH slot inference, two simultaneous time slots on one carrier, continuity across IQ and output chunk boundaries, invalid IQ, loss/reacquire and false positives. Explicitly test short key-up calls where waiting for the next sync can lose the transmission.

The protocol also notes a Golay-generator matrix ambiguity between machine-extracted ETSI V2.7.1 and an independently distance-8 V2.6.1 matrix. Inspect the **rendered** V2.7.1 table, not only extracted text, and cross-check against a known valid over-the-air DMR slot-type sample before production acceptance. Avoid papering over this with a hand-picked vector alone.

## Replay acceptance — use the existing private, pinned IQ corpus

The current `tests/digital_voice_replay/manifest.json` provides 48 kS/s `cf32-le` and `cs16-le` fixtures. Real-air files remain in the private REA-Results worktree; **do not commit or redistribute** them. Verify Git blob SHA-1 and byte counts first; nine real-air fixtures currently have no source-pinned SHA-256.

Start with `dmr-directmode-keyup` (upstream expectation: group 99, source 3024109, color code 1), followed by `dmr-directmode-second-ptt`. Tier III control and voice-terminator captures are useful **protocol coverage probes**, not evidence that the Tier II decoder supports Tier III trunking. Re-play the two synthetic negative-control fixtures to check false protocol identity promotion; they do not establish a long-duration false alarm rate.

For each fixture record: source blob and sample format, processed samples, sync acquired, time to lock, complete decoded bursts, valid slot type, corrected FEC, CRC/RS pass and failure counts, `identity_valid` events, source/talkgroup/slot/color code, first/last symbol timestamps, errors on every tested chunk size and no-input/noise results. **Never promote SYNC or a raw ID field into `crc_valid_decode`.** Report `NOT RUN` where there is no measured evidence.

## Independent host acceptance commands

```sh
cmake -S components/orcsdr_dmr/d1_frontend -B build/dmr-frontend -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-frontend --parallel
ctest --test-dir build/dmr-frontend --output-on-failure

cmake -S components/orcsdr_dmr/d1_protocol -B build/dmr-protocol -DCMAKE_BUILD_TYPE=Release
cmake --build build/dmr-protocol --parallel
ctest --test-dir build/dmr-protocol --output-on-failure

cmake -S components/orcsdr_radio_discovery/esp_idf -B build/discovery-sd -DCMAKE_BUILD_TYPE=Release
cmake --build build/discovery-sd --parallel
ctest --test-dir build/discovery-sd --output-on-failure
```

Repeat with each module's documented sanitizer switch and compare exact results against the agent reports. The above commands have **not** been rerun independently for this checkpoint, and the branch currently has no GitHub combined-status checks or PR-triggered workflow run for the latest commit.

## F0c hardware gate remains open

- Adapter uses only `/sdcard/orcsdr/disc1/obs1a.bin` and `obs1b.bin` and depends on an existing mounted SD VFS. Do not remount, format, truncate or migrate `/orcsdr/P25.cfg` or `/orcsdr/p25/**`.
- An `fsync` on a host or ESP-IDF VFS does **not** prove media power-cut atomicity. Physical removal, reset, sync failure, second-slot CRC damage, FAT directory durability and read-only/full-card scenarios need actual measurements.
- The `Journal::Workspace` is sizeable and must remain out of limited DSP/UI task stacks; measure actual ESP32-P4 internal RAM, PSRAM, stack headroom and SD latency after opt-in integration.
- Keep radio ownership and real-time USB capture independent of SD file I/O.

## Restart order

1. Run all three host suites independently (Release + sanitizers); record exact environment and outputs.
2. Implement and test the **continuous dibit/stream-alignment bridge**; do not touch production firmware yet.
3. Replay private direct-mode IQ, then noise and additional fixtures. Resolve any real-IQ decoder incompatibilities using standards-derived tests.
4. Separately opt in ESP-IDF adapter on an authorized build and test real microSD recovery without affecting existing P25 profiles.
5. Only after these gates, design a thin read-only M5GFX snapshot adapter respecting the **real 5-inch Tab5 1280×720 screen**, established OrcSDR **orc** branding and P25 header/tab/touch/receiver-ownership conventions. UI redesign is deliberately **deferred by the operator**.

No mainline merge, UI implementation, RF reception, voice/audio acceptance or device-power-loss guarantee is claimed by this handoff.
