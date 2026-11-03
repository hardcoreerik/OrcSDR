# apps/orcsdr-web/decoders

One source tree for OrcSDR's decoders, built three ways from the same files (`sources.sh` lists them):

| Build | Where | How |
|---|---|---|
| host | PC, Linux x86-64 (g++ 11 in the emscripten Docker image) | `apps/orcsdr-web/decoders/host/build.sh` -> `build/orcdec_replay` |
| WASM | browser, worker, node | `apps/orcsdr-web/decoders/wasm/build.sh` -> `build/wasm/orcdec.js` |
| ESP-IDF | the ESP32-P4 firmware | Uses the same underlying device decoders; this imported C ABI and its HTTP route are not wired into the current P4 target. |

- `include/orcdec.h`: the C ABI (create, feed, command, poll, attachment). Event format: `docs/decoders/EVENT_SCHEMA.md`.
- `src/`: the ABI and one adapter per decoder (POCSAG, FT8/FT4, SSTV, ADS-B, ACARS, P25, JS8).
- Decoder sources: not copied. They are the device sources in `apps/orcsdr-tab5/ui`, compiled in place (see `sources.sh`).
- `host/orcdec_replay.cpp`: replays a capture file and prints the events.

The source repository's parity harness and historical Phase 3 report are not
present in this import. Do not claim their results for this target. Build the
current WASM using `wasm/build.sh` in the pinned Emscripten container; protocol
replay, browser and device parity still require current evidence.
