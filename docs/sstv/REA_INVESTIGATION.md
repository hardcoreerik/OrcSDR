# SSTV reverse-engineering workbench — REA + source audits

Status: **ready-to-run investigation workflow; REA itself is not installed in the ChatGPT execution sandbox.** No result in this file claims that REA has decompiled a binary. Upstream tool: [morluto/rea](https://github.com/morluto/rea), MIT; pinned tested interface in this workbench: `rea-agents@6.4.0`.

## Why REA here?

REA is a *native binary analysis agent front end*, not a wav/SSTV waveform decoder. Use it when investigating a built binary's algorithm and call paths: the tone estimator, VIS parity state machine, sync drift correction, and line timing. Ghidra (or Hopper) provides disassembly/decompilation; REA exposes evidence to Codex or a CLI. The same investigation should be backed by executable PCM fixture tests.

Do **not** use decompiled output to replace published SSTV protocol constants, or assume copying a decoder reproduces its behavior. When source is available, inspect source first; decompile to verify compiler/runtime behavior or study a legitimately obtained target binary.

## Public reference implementation already located

[F4HWN UV-K1 / UV-K5 V3 Labs SSTV overlay](https://github.com/armel/uv-k1-k5v3-firmware-custom/tree/main/App/apps/sstv):
- `sstv_app.c` is Apache-2.0 licensed and documents a receiver on ~9.6 kHz ADC PCM using a two-pole bandpass, smoothed **P=y1*(y0+y2), Q=y1²**, an amplitude-insensitive tone ratio, 30 ms VIS bit vote accumulation, and period tracking. These are the principles to compare—not code to paste unchanged.
- `test/model.py` has a model of the transmitted waveforms, channel and receiver; `test/modes.py` contains independently documented 14-mode schedules; `test/emu.py` reportedly cross-checks emulator output.
- Its receiver predicts **sync end** inside ±10 ms, limits continuous missed syncs, and tracks clock rate using bounded corrections rather than assuming exact sender timing.
- A discrepancy found in OrcSDR during this audit: Robot 36 had a **137,500 ns** pixel period even though the 88 ms Y scan requires **275,000 ns** for 320 pixels. Robot 72, Scottie S1/DX and Martin M2 had other clock errors. These were fixed in `sstv_modes.cpp` and a repeatable assertion test now protects all 14 modes.
- A future DSP change should benchmark normalized autocorrelation vs zero-crossing against FM/SSB recordings with weak signals and oscillator offset before selecting one.

Additional reference: [slowrx](https://github.com/windytan/slowrx), especially `modespec.c`, `fsk.c`, `sync.c` and `video.c`. Its notes include shifted-tone detection and adaptive slant correction.

## Install REA for the host investigation (not on the Tab5)

1. Use a **supported native host** (Ubuntu 24.04+ or modern macOS; Windows Ghidra support is experimental and needs checking against current REA docs). Ensure Node 22.19+, Node 24.11+, or Node 26+, npm, C++17 and Python 3.
2. Review upstream [REA installation documentation](https://rea.tools/get-started/). Install the **explicitly pinned** version (avoid surprise upgrades while gathering evidence):

   ```sh
   npm install --global rea-agents@6.4.0
   rea --version
   ```

3. For native inspection, install **Ghidra 12.1.x (verified 12.1.4)** and **JDK 21+**, then configure paths to the actual installed versions:

   ```sh
   export GHIDRA_INSTALL_DIR=/absolute/path/to/ghidra_12.1.4_PUBLIC
   export JAVA_HOME=/absolute/path/to/jdk-21
   rea doctor --provider ghidra --json
   rea setup
   ```

   Select **Codex** in REA's interactive agent setup. Review the plan before approving. Setup backs up existing client configuration. Restart the Codex session so the new MCP registration is loaded. No Ghidra, Codex configuration or developer machine is modified by the committed OrcSDR scripts.

4. Run the repeatable evidence workbench from the **OrcSDR checkout root**:

   ```sh
   python3 tools/sstv_rea_workbench.py build
   python3 tools/sstv_rea_workbench.py doctor
   python3 tools/sstv_rea_workbench.py inspect
   ```

   The build produces `sstv-rea-evidence/sstv-wav-replay`: a real host executable built from OrcSDR's current `sstv_receiver.cpp`, `sstv_modes.cpp`, `sstv_auto_detect.cpp`, and `sstv_wav_replay.cpp` with DWARF debug symbols. The inspect step runs **`rea analyze` on that binary** with Ghidra and saves command lines, evidence JSON, stderr, file hashes and tool versions to the `sstv-rea-evidence/` folder where you ran the command.

5. To investigate another **authorized native binary**:

   ```sh
   python3 tools/sstv_rea_workbench.py inspect --target /absolute/path/to/decoder
   rea search /absolute/path/to/decoder "sync" --provider ghidra --json
   rea function /absolute/path/to/decoder main --provider ghidra --json
   ```

   Do not inspect copyrighted proprietary firmware or distribute derived binaries without permission. F4HWN and slowrx publish their sources; **source review is the simpler and more reliable starting point**.

6. To inspect a particular OrcSDR function, find its symbols and use REA's CLI or Codex MCP session, e.g.:

   ```sh
   nm -C sstv-rea-evidence/sstv-wav-replay | grep 'orcsdr::sstv::'
   rea search sstv-rea-evidence/sstv-wav-replay 'sstv' --provider ghidra --json
   ```

   Do not assume REA returns unmangled C++ names automatically; use the observed symbol list and addresses.

## What to prove, not merely infer

| Hypothesis | Evidence to collect | Acceptance criterion |
| --- | --- | --- |
| VIS onset/parity | raw PCM and VIS state transitions | Correct VIS or explicit error across amplitude and tuning offsets |
| PD120 | image rows against F4HWN/test model and independently recorded WAV | 640×496 frame geometry, 248 sync periods, progressive updates, no duplicate/missing rows |
| Clock drift | +1%/-1.5% sender clock fixtures | No accumulated slant or reasonable loss reporting; measure before claiming support |
| Tone demodulation | P/Q F4HWN reference vs current 12k crossing estimator | Quantified luminance error, CPU cycles and failure modes |
| Missed sync | injected dropout and noise | bounded holdover, explicit lock loss, no invented clean image |
| Capture database | interrupted/complete image BMP and CSV | correct partial flags, content-provenance, file not overwritten after restart |

The committed `tools/sstv_validate_timings.py` compares 14 frame/sync/pixel timings independently of the C++ table, and is included in GitHub Actions. Use the test first:

```sh
python3 tools/sstv_validate_timings.py
g++ -std=c++17 -O2 -Iapps/orcsdr-tab5/ui \
  apps/orcsdr-tab5/tests/sstv_receiver_smoke.cpp \
  apps/orcsdr-tab5/ui/sstv_receiver.cpp \
  apps/orcsdr-tab5/ui/sstv_modes.cpp apps/orcsdr-tab5/ui/sstv_auto_detect.cpp -o /tmp/sstv-smoke
/tmp/sstv-smoke
```

For recorded WAVs (no fixture supplied by this commit):

```sh
g++ -std=c++17 -O2 -Iapps/orcsdr-tab5/ui \
  apps/orcsdr-tab5/tests/sstv_wav_replay.cpp \
  apps/orcsdr-tab5/ui/sstv_receiver.cpp \
  apps/orcsdr-tab5/ui/sstv_modes.cpp apps/orcsdr-tab5/ui/sstv_auto_detect.cpp -o /tmp/sstv-wav-replay
/tmp/sstv-wav-replay /path/to/received_pd120_48k_mono_pcm16.wav 95 50
```

**Each workbench subcommand saves logs in `sstv-rea-evidence/` relative to the directory it was launched from** (or the explicit `--out` directory). REA inspection output is evidence for a later development decision—not a decoder verification test.

## Known local execution limitation for this ChatGPT sandbox

The current temporary container reports **Node 22.16.0** (below REA's required 22.19), **no Ghidra installation**, and DNS resolution for npm fails. Therefore this ChatGPT session cannot truthfully claim REA binary analysis succeeded. The scripts and branch workflow are prepared so it can be run on a connected supported developer workstation without modifying the ESP32 firmware or making untested assumptions.

## Verified CI result — 2026-10-12

The isolated [REA/Ghidra native binary investigation](https://github.com/hardcoreerik/OrcSDR/actions/runs/38303294261) **completed successfully** on branch commit `1fab1f0a73a64b328a0c80412a9c538907d4db26`.

- REA: **6.4.0**, Ghidra: **12.1.4**, Node: **24.11.1**.
- Analysis target: Linux `x86_64` ELF host WAV-replay decoder compiled with `g++ -O2 -g`; not the ESP32-P4 firmware.
- REA produced native overview and full Ghidra function dossiers for `orcsdr::sstv::service(unsigned long)` and `orcsdr::sstv::offer_audio_48k(short const*, unsigned long)`.
- The host overview identified 108 procedures; the decoder service dossier included 876 assembled instructions covering 4,691 bytes of code (non-contiguous basic blocks); the audio tap had 45 instructions covering 194 bytes.
- A static opcode count from REA's observed `assembly` arrays found **11 `DIVSD` plus 3 `DIVSS`** in the decoder service and **zero float divisions** in the audio tap.
- The audio producer dossier had no observed native callees, consistent with its lightweight SPSC design.
- The decoder service has 5 observed callee entries, including C library allocation, formatting and `vis_spec`. These are associated with branches in the larger function and do not imply that heap allocation runs per sample.
- Full Evidence JSON, compiler command, SHA256 provenance and diagnostics are stored in artifact `sstv-rea-ghidra-evidence` attached to that run.

**Interpretation:** the audio producer already compiles to a tight scalar loop. In contrast, the decoder still computes pixel/line position with floating-point divisions in its sample-processing path, a plausible P4 hot spot. Static instruction counts are **not** a benchmark; the optimized x86 instruction mix should not be misrepresented as ESP32-P4 instruction counts.

### Next bounded experiment

Replace per-sample floating-point pixel-position division with integer Q16/Q32 incremental scan timestamps **only after** assembling real PD120 and Robot/Scottie fixture comparisons. Keep the pixel start, end, and line correction mathematically equivalent; commit a test that verifies exact row content, VIS detection, sync lock, and slant on both implementations. Use runtime cycle counts on actual P4 hardware to decide whether the change is worthwhile.

A separate independent protocol audit already fixed Robot 36/72 pixel dwells, Martin M2/Scottie S1/DX timings, and Scottie's extra 9 ms post-VIS synchronization. Host CI validates synthesized progressive Robot36, PD120 and Scottie S1 reception. These are real **source-derived protocol corrections**; REA does not claim them as findings inferred solely from the binary.
