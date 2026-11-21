# OrcSDR Cardputer Zero staged ACARS handoff (2026-11-17)

## Provenance and ownership

- Repository: `hardcoreerik/OrcSDR`.
- Fetched remote integration branch: `codex/cardputer-zero-ui-audio-controls`.
- Fetched base SHA: **`73736f9e784dd8fcc20c935620740a00dcbca301`**.
- Local logical work lane: `sandbox/cardputer-zero-acars-dashboard`.
- Strict delivery path: `apps/orcsdr-cardputer-zero/staged-dashboards/acars/`.
- Working environment: isolated `/mnt/data/orcsdr-acars-work` staging directory.
  Ordinary `git clone`/`git ls-remote` were blocked by the sandbox's external
  DNS/network policy; no existing checkout was cleaned, switched or reset.
- Source investigation verified canonical `acars_core.*`, `acars_selftest.*`,
  Linux `live_capture.*`, `iq_queue.hpp`, compact renderer/control model, and
  ACARS native tests through the linked GitHub repository interface.
- Coordinator's unpushed Home/FM/selector/palette changes were **not changed**.
- Delivery commit SHA: recorded separately in publication report/final response,
  not written back into a new self-referential commit.

## Delivered file inventory

- `CMakeLists.txt`: standalone tests/preview with existing shared decoder source.
- `acars_dashboard.hpp/.cpp`: decoder/UI SPSC, validated rate/tune/generation,
  bounded history, counters, input/state/lifecycle.
- `acars_renderer.hpp/.cpp`: partial redraw, CRC/ETB semantics, safe payload
  display, RGB565 buffer and dirty rectangles.
- `preview.cpp`: DEMO, empty and selected-detail PPM screenshots.
- `tests/acars_dashboard_test.cpp`: deterministic RF, CRC, correction, stale
  metadata, queue/discontinuity, history and renderer assertions.
- `README.md`: use/build/controls/known limits.
- `INTEGRATION.md`: exact coordinator ownership/hook requirements.
- `HANDOFF.md`: this evidence and remaining acceptance boundary.
- `RESEARCH.md`: source provenance and licensing.

## Checks performed in this sandbox

Host: Linux x86_64 (Debian), GCC/G++ 14.2, CMake 3.25+, `Debug` build,
headless, no physical RTL-SDR or Cardputer display. **Important fidelity
boundary:** the clone was inaccessible; local CMake tests compiled a temporary
hand-transcribed **test mirror** of inspected `acars_core` and self-test source
and a temporary compact renderer glyph/control header *outside* the delivery
folder. No mirrors are included in any delivery commit. The standalone project
references the actual repository `.cpp`/`.hpp` files in a real checkout;
compile/test on the coordinator's checkout is still required for definitive
shared-source compatibility.

Commands:

```sh
cmake -S apps/orcsdr-cardputer-zero/staged-dashboards/acars -B build/staged-acars -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/staged-acars --parallel 2
ctest --test-dir build/staged-acars --output-on-failure
build/staged-acars/orc_zero_acars_preview --demo --screenshot build/staged-acars/acars-demo.ppm
build/staged-acars/orc_zero_acars_preview --demo --empty --screenshot build/staged-acars/acars-empty.ppm
build/staged-acars/orc_zero_acars_preview --demo --detail --screenshot build/staged-acars/acars-detail.ppm
```

Last local `ctest`: **4/4 passing** (`staged_zero_acars_regressions`,
`staged_zero_acars_preview`, `staged_zero_acars_empty`,
`staged_zero_acars_detail`). Reviewed output PPMs and independently checked
320×170 dimensions; DEMO indication is prominent, text contained, and list /
empty / detail views are readable in enlarged host image inspection.
`git diff --check` cannot operate on this independent sandbox directory
without an actual Git checkout; whitespace validation uses a local file scan.

## What the test evidence establishes

- Existing 240k CU8 -> +60k frequency shift -> FIR /10 -> 24k
  demodulator/CRC interfaces exercised by generated RF under multiple chunk
  sizes (not raw 240k passed to 24k decoder). Mocked here only by the explicitly
  documented local core mirror; the real checkout needs rerun.
- Real callback path distinguishes validated blocks from arbitrary radio
  input. CRC validation, one-bit repair, two-bit repair and triple-bit failure
  tested using the canonical API shape; unknown candidates are not messages.
- Ring capacity/eviction/selection/scroll, nonprintable payload escaping,
  ETB/ETX display, detail ownership and no full-screen-clear growth on
  ordinary row updates are checked.
- Retune/discontinuity/unsupported rate/wrong center/stale generation fail
  rather than decoding out of context; history persists across receiver
  generations, while partial RF state resets.
- DEMO is hard-separated from LIVE stats; no synthetic blocks are presented
  to a live operator as observed over the air.

## Fixed bounds and memory

Locally measured C++ `sizeof`: `Dashboard` **19,576 B**; `Renderer`
**114,120 B** (includes the 108,800 B RGB565 frame and previous snapshot);
`Snapshot` **5,040 B**, `Record` **304 B**, canonical `Message`
**284 B**. These are layout sizeof measurements on the local x86_64 build,
not Cardputer heap/stack measurements. 16 history records, 16 SPSC events,
5 visible rows, 250 bytes of unmodified raw text per message (per shared
`Message`), up to 1000 escaped characters transiently while rendering.
Newest event discarded on full SPSC; oldest history evicted when full.

## Remaining gates, owners and risk

**Required coordinator integration:** wire production selector, CMake library,
receiver handoff/tune-readback and generation, keyboard/frequency dialog,
global settings/palette ownership and RGB565 dirty-rectangle LCD flush. The
single physical receiver must never be double-opened. This is a staged module,
not a landed production selector entry or a flashed device.

**Physical acceptance (NOT checked):** functional Cardputer display and input,
actual 240 kS/s RTL-SDR readback, gain application, USB-loss/queue observation,
airband reception with antenna and genuine CRC-verified ACARS, frequency
labels/retunes/reconnect under load, LCD flicker, resource pressure, CPU and
heap/stack, noisy RF false positives. Audio deliberately absent. No proof of
real-device performance is asserted.

Risk level: **MEDIUM** for staging/integration (single-radio handoff and
unverified canonical checkout build); **HIGH** for live hardware acceptance
until the coordinator runs RF tests on the real device.
