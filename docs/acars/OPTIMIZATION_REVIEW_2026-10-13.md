# ACARS pre-integration decoder audit — 2026-10-13

**Host-tested only.** No ESP32-P4 CPU, heap, stack, live RF, or independent frame-count results have been established.

### Implemented improvements
- Symmetric FIR pairing preserves the original decimation filter coefficients and reduces per-output coefficient multiplications from 101 to 51 (first stage), and from 81 to 41 (second stage), per I and Q stream. Conditional ring wraps also replace per-output modulo. This is an arithmetic reduction, not a measured embedded speedup.
- CRC/FEC recovery now operates on a bounded 250-byte candidate copy. A CRC-valid but grammar-invalid candidate no longer mutates the live framing buffer.
- One-bit-tolerant sync, pre-key reacquisition, CRC-verified damaged end-marker recovery, 220-character text bound, ETB/ETX fidelity are inherited from the standards-derived experimental branch; these do not improve MSK weak-signal timing by themselves.

### Executed sandbox gates
Full `tests/acars_core_tests.cpp`, `tests/acars_standard_audit.cpp`, and short smoke gate all passed using g++ C++17; AddressSanitizer/UBSan also passed on full core tests. Synthetic SNR at 12 dB: 5/50 frames nominal, 0/50 -200ppm, 5/50 +200ppm; 20dB nominal 50/50 (independent but **uncalibrated** AWGN model). Front-end end-to-end synthetic CU8: 1 validated message.

### Known limitations
Single DDC/channel at 2.4MS/s, 24k channel-IQ hard-decision phase discriminator, no adaptive timing/true coherent matched filter, no measured SNR or lock-quality signal, no archive, no live hardware integration or multi-channel channelizer. Synthetic noise sensitivity remains weak.

### Next decoder-owner research
Benchmark coherent matched-filter MSK + fractional Gardner/timing PLL vs current hard decision; optimize shared wideband channelization, anti-alias rejection, prekey near/far interruption and FEC collision behavior; capture independent RF oracle counts vs acarsdec/xng. Keep REA/Ghidra analysis remote in existing proven GitHub Actions route and do not use third-party implementation code. Verify on physical ESP32-P4 before speed/receiver claims.

Run branch CI smoke gate with `bash tools/test-acars-smoke.sh` (writes `build/acars/smoke.log`).
