# Cardputer Zero Linux integration checkpoint — 2026-11-06

Scope: this public OrcSDR development branch integrates the verified portable decoder source gates, the versioned Radio Service v1 specification, and Cardputer Zero Linux/ARM64 host tests. It does not merge the separate private OrcSDR Web security repository or Mobile source into this tree.

## Current Linux functionality

The app compiles original OrcSDR ACARS and ADS-B source, tests deterministic CU8 IQ fixtures and CRC-valid payloads, enumerates Linux librtlsdr devices, supports bounded capture, and now offers bounded live CU8 producer-to-decoder operation:

    ./build/cardputer-zero/orcsdr_cardputer_zero --live-acars --device 0 --blocks 64 2>&1 | tee cardputer-zero-live-acars.log
    ./build/cardputer-zero/orcsdr_cardputer_zero --live-adsb --device 0 --blocks 128 2>&1 | tee cardputer-zero-live-adsb.log

Default ACARS tuner center is 131.490 MHz, capture rate 240000 complex samples/sec with the intended carrier at center+60 kHz. ADS-B defaults to 1090 MHz, 2048000 complex samples/sec. Real Linux tuners may reject rates or bands their drivers do not support. The producer never invokes a decoder; a bounded queue passes blocks to a consumer. Queue drops and resets are reported, but missing USB samples upstream of the application are not measurable with this primitive. Capture time is an observed host read completion time, not ADC timestamp.

The Linux metadata adapter at apps/orcsdr-cardputer-zero/src/v1_frame_adapter.hpp uses the Radio Service v1 C ABI for local CU8 frames. It preserves source-relative complex sample indexes, true frame format, known receiver generation and explicitly unknown UTC/USB loss. Its smoke test rejects a stale generation.

## Existing Web/WASM assets on Linux

The same application can serve the exact compiled OrcSDR Web files from a local directory. It does not require or create another dashboard. No private Headless source or compressed binary assets are copied into the public repository. Supply an existing build output directory containing m5.html.gz, app.js.gz, core.js.gz, core.wasm.gz, decoders.js.gz, decoder_worker.js.gz, orcdec.js.gz, orcdec.wasm.gz and splash.jpg.

    ./build/cardputer-zero/orcsdr_cardputer_zero --serve-web ../OrcSDR-Headless/main/m5 --port 8780 2>&1 | tee cardputer-zero-web.log

This is a deliberately narrow integration milestone. The HTTP server binds ONLY to 127.0.0.1 and serves static files, with the proper JS/WASM MIME types and gzip encoding. A small read-only health route reports that Radio Service v1 is NOT yet deployed. Radio API calls return HTTP 501, and write methods return 405. The browser can fetch authentic Web assets but cannot yet operate a live radio using this server. A localhost SSH port-forward can be used from another trusted computer for development. Do not change bind address to 0.0.0.0 or expose port 8780 to Wi-Fi without implementing and testing credential authentication, authorization, origin rules and encrypted transport.

## Reproduce host CI locally

From the repository root (output logs saved alongside the invocation):

    cmake -S apps/orcsdr-cardputer-zero -B build/cardputer-zero -DCMAKE_BUILD_TYPE=Release 2>&1 | tee cardputer-zero-config.log
    cmake --build build/cardputer-zero --parallel 2>&1 | tee cardputer-zero-build.log
    ctest --test-dir build/cardputer-zero --output-on-failure 2>&1 | tee cardputer-zero-tests.log

The Python test uses placeholder gzip assets and asserts original bytes survive the server unchanged; it is NOT a real WASM browser run. ARM64 CI uses Ubuntu ARM64 hosted runners, not the vendor Cardputer BSP. Agent 3's host security tests do NOT establish Linux production authentication.

## Remaining integration work

1. Turn Radio Service v1 from an ABI/schema plus frame adapter into an authoritative Linux radio session provider with receiver ownership, runtime capabilities, control results, and event streaming.
2. Build Linux HTTP adapters for the real Web routes and port-81 WebSocket streams, including decoder/event parity and audio. Keep the original front-end assets and contracts, not a fork.
3. Integrate a Linux-appropriate secure pairing and encrypted transport implementation based on the separately reviewed Web security design, with negative authorization tests.
4. Verify the real Cardputer's USB Host switch, power budget, RTL-SDR, ARM64 BSP, Ethernet/Wi-Fi and sustained loss/capture behavior on physical hardware.
5. Add optional 320x170 LVGL keyboard/status UI, system service packaging, mobile remote compatibility and full off-air tests.

No unexecuted physical test or Web dashboard behavior is counted as passed.
