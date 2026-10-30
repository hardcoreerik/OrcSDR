# VDL2 Tab5 HANDOFF (2026-10-28)

**Branch** `claude/acars-data-decoding-6e3ed0`, parent baseline `c11cbaae0ad0d0462e68d195b529e30945f1bdcb`. Existing ACARS interpreter and dashboard preserved. `main.cpp` unchanged; no firmware source list or receiver settings changed.

## Changed
- `apps/orcsdr-tab5/ui/vdl2_frames.hpp`: standalone, bounded `orcsdr::vdl2::FrameDecoder` interface, LSB-first input bits and borrowed frame callback.
- `apps/orcsdr-tab5/ui/vdl2_frames.cpp`: HDLC flags and de-stuffing, AVLC CRC and address extraction, positive AOA marker and independent ACARS BCS check, safe `acars::Message` conversion, raw non-ACARS frames.
- `tests/vdl2_frames_test.cpp`: synthetic roundtrip and failure cases.
- `apps/orcsdr-tab5/ui/vdl2_burst.{hpp,cpp}`: standalone header FEC check/single-bit correction, 15-bit descrambler and reference-checked RS final parity counts / LSB-first bit packing.
- `tests/vdl2_burst_test.cpp`: synthetic header/descrambler/packing vectors.
- `docs/acars/VDL2_RESEARCH_2026-10-28.md`: detailed constant and standard/source registry, competitor/manufacturer audit, integration plan.
- Separate REA submission: `hardcoreerik/REA-Results` branch `chatgpt/vdl2-research-20261028`, `inbox/2026-10-28_vdl2-orcsdr-tab5.md`.

## Host run/test (from repository root, Linux or equivalent)
```sh
mkdir -p /tmp/orcsdr-vdl2
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I apps/orcsdr-tab5/ui \
    apps/orcsdr-tab5/ui/vdl2_frames.cpp \
    tests/vdl2_frames_test.cpp \
    -o /tmp/orcsdr-vdl2/vdl2_frames_test
/tmp/orcsdr-vdl2/vdl2_frames_test
```
Expected: `vdl2_frames_test passed (synthetic framing / CRC / ACARS)`. On 2026-10-28 it passed under GCC 14.2 with ASan/UBSan in a sandbox compiling against a local stand-in for the verified `acars::Message` header layout. **Repository-real-header build and ESP-IDF build still need to run locally/CI.** The test file uses `std::vector` to generate synthetic frames; production VDL2 module uses fixed arrays and no hot-path heap.

## Second independent host test (from repository root)
```sh
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I apps/orcsdr-tab5/ui \
    apps/orcsdr-tab5/ui/vdl2_burst.cpp tests/vdl2_burst_test.cpp \
    -o /tmp/orcsdr-vdl2/vdl2_burst_test
/tmp/orcsdr-vdl2/vdl2_burst_test
```
Expected: `vdl2_burst_test passed (synthetic header, LFSR, shortened parity and bit order)`.
Isolated GCC C++17 / ASan / UBSan host test passed in sandbox. No off-air vectors or device tests.

## Build/flash
**Do not flash this as a VDL2 firmware feature.** The physical-layer and RS correction pipelines are not implemented or linked in `apps/orcsdr-tab5/main/CMakeLists.txt`. Existing firmware build and ACARS reception remain unchanged. Compile/load this slice only as host module tests until off-air reference vectors are available and matched.

## Next ordered gates
1. Capture VDL2 bursts and collect `dumpvdl2` decoded AVLC hex/count oracle at matching frequencies and sampling rates. Note actual board/RTL-SDR rate and clock settings.
2. Implement separate streaming physical-layer and burst FEC modules, no `main.cpp` changes, with fractional timing recovery and a documented RRC-on/off comparison (ETSI TX uses a full raised-cosine alpha 0.6; xng notes that adding RX RRC hurts real-capture results), at chosen rate, D8PSK ambiguity resolution, 17+5 header, 15-bit descrambler and shortened RS(255,249).
3. Compare on-air CRC-valid AVLC/ACARS counts to dumpvdl2, benchmark Tab5 memory/CPU and USB discontinuities.
4. Bridge validated `acars::Message` into existing queue/UI and show original channel and bearer POA vs VDL2. ATN/CPDLC/ADS-C remain raw bytes, not guessed translations.
5. Build ESP-IDF and run on explicitly authorized Tab5. Check 1280x720 layouts legible on physical 5-inch screen and no full-screen redraw regressions.

## Known limitations
The tests prove byte/bit framing under synthetic inputs, not real radio decoding; ACARS inner BCS/parity conventions need off-air fixtures; a deliberately fixed 2,048-byte AVLC cap is only an initial memory limit; all PHY gain constants and RS edge cases remain unvalidated. Firmware raw-frame display and message-transport labels are not yet present.

## Important 2026-10-28 PHY correction
Follow ETSI's **transmitted** full raised-cosine alpha=0.6; do not assume there is an RRC half-filter on TX. The xng project documented a same-IQ comparison in which adding RRC on RX reduced CRC-good frames from 17 to 1. This is xng's self-reported measurement, not ours. RX filter selection must therefore be A/B evaluated against identical off-air IQ; default candidate is the anti-alias channel FIR without RX RRC. Also assemble Reed–Solomon octets from transmitted bits **LSB-first** (HDLC wire order) before applying RS(255,249) arithmetic; see xng docs/notes/VDL2.md and BENCHMARKS.md.

## One-command repeatable host tests
From repository root, run `bash tools/test-vdl2-host.sh`. The script compiles both modules against the **actual** `acars_core.hpp` from the checkout and runs synthetic tests with strict C++17 warnings and ASan/UBSan (set `ORC_VDL2_SANITIZE=0` if the local compiler cannot provide sanitizer runtimes). This command has **not** been run on the fetched GitHub checkout in this web session; the sandbox equivalent with mirrored source was executed and passed.
