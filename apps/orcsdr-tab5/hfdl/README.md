# HFDL / HF aviation — experimental receive-only dashboard and DSP

Development branch: chatgpt/hfdl-dashboard-decoder. Not merged to main.

## Implemented

- Pure fixed-memory Model / Controller split, compatible with ACARS conventions.
- 24-message ring, 8-row Snapshot, SPSC queue of eight usable message slots.
- Producer publishes atomics and queue messages; UI task alone owns the ring/snapshot. Queue overflows are measured.
- Native M5GFX drawing with honest empty states, verified-message list/detail, paging, bounded update regions and overlay guard.
- Explicit native geometry and touch hit rectangles for 1280x720 Tab5, 720x720 square, 800x400 Hosyond and 568x1232 LilyGO portrait. No scaling of a fixed 1280x720 framebuffer.
- A receive-only NCO frontend tuned to +1440 Hz, a fixed 127-tap channel FIR, decimation to 12 kS/s, measured channel power. Supported source rates are multiples of 12 kHz from 12 to 192 kHz.
- HFDL category registered as an unavailable LAB dashboard until the app owner can route it honestly, and source files added to Tab5 CMake.

## Unimplemented / explicitly NOT claimed

No preamble acquisition, symbol timing, M-PSK symbol decisions, convolutional FEC, deinterleaving, CRC verification, SPDU/MPDU/LPDU parsing, live IQ receiver handoff, on-device physical panel acceptance or verified aircraft positions. The frontend is NOT a complete HFDL decoder.

**Critical safety/correctness change:** the old implementation posted a fabricated SPDU every approximately 32 seconds. That path is removed. No frame or message is posted from process() or process_audio() until a real PHY and link parser verifies its integrity. For now, zero frames is the truthful result.

## Radio and UI ownership

Only UI owner task renders M5GFX. Worker tasks must never paint. A verified message producer may use Model::post_message() only after real frame validation. Neither UI nor Controller owns the tuner, driver, USB or SD. Call Model::reset() and Decoder::set_config() only while receiver producer is stopped; use the model in lazily allocated PSRAM-backed state when wiring to firmware. Never call Model::snapshot() from the decoder producer.

## Compile/run host tests

At the root of a checkout of this branch, on Linux with g++/clang++:

    bash tools/test-hfdl-dashboard.sh

The run writes build/hfdl-dashboard/test.log next to the generated test executable. Host coverage includes all four requested dimensions, clipping and hit targets, detail/paging, ring/queue ordering and overflow, atomic receiver status and the no-fake-SPDU rule after more than 32 seconds of samples. GitHub Actions workflow hfdl-dashboard-tests.yml runs the same test on branch changes.

Host tests do not certify panel touch/backlight timing, no-flicker acceptance or live IQ continuity. Physical screenshots and RTL-SDR replay tests are separate, pending hardware/decoder integration.

## Real decoder research and next gates

1. Implement A1/A2 PN acquisition and M1 rate/slot selection, per ICAO Annex 10 Volume III Part I Chapter 11. Cite exact parameters in REA-Results, including the independent xng implementation notes, before coding constants.
2. 1800-symbol/s timing, frequency correction, equalizer and M-PSK soft symbols; FEC/interleaver; CRC-valid SPDU as first independently replayed acceptance gate.
3. MPDU/LPDU/HFNPDU parser and application-level correlation using shared aviation infrastructure, preserving message provenance.
4. Bind real IQ to decoder via the existing radio session (not a second USB/tuner); implement app lifecycle and disabled-to-enabled menu transition only after verified live decoding.
5. Run F.1487 multipath/Doppler test profiles with identical IQ against dumphfdl and xng, record correct frames, drops, signal strength and P4 CPU/PSRAM.
6. Native M5GFX recordings and physical acceptance for 1280x720, 720x720, 800x400 and 568x1232.

Research sources: REA-Results chatgpt/hfdl-icao-specs-20261102, inbox/hfdl-icao-specs/notes.md, the user-uploaded ITU-R F.1487/P.372 and ICAO Doc 9741 preview. No purchase is required for the next decoder gate.
