# OrcSDR NXDN48 / NXDN96 receive-only foundation

**Status: C++17 host core and synthetic IQ acceptance implemented; genuine-air, Golay vocoder frame extraction, trunking, licensed AMBE+2 audio and Tab5 integration NOT accepted.**

Native standalone host component. 48 kS/s CS16 IQ → separate 2400/4800 symbols/s 4FSK frontends → 192-dibit air frames → PN9 LICH and CRC-qualified SACCH/RAN. The FSW+LICH frame callback is *candidate evidence only*; it does not create verified talkgroups, stations, source/destination or clear voice. The voice candidate callback contains raw 72-bit codewords, not proven 49-bit/Golay AMBE payloads.

Reuses the existing private REA research branch `research/nxdn48-nxdn96-foundation-20261119` and the real-air fixture hashes documented in `docs/nxdn/REAL_IQ_VALIDATION.md`. It does not copy third-party NXDN decoder source or insert new SDR/tuner ownership.

## Build and test

From the OrcSDR repository root (output logs remain in the same directory):

```sh
cmake -S components/orcsdr_nxdn -B build/nxdn -DCMAKE_BUILD_TYPE=Release 2>&1 | tee ./nxdn-configure.log
cmake --build build/nxdn --parallel 2 2>&1 | tee ./nxdn-build.log
ctest --test-dir build/nxdn --output-on-failure -V 2>&1 | tee ./nxdn-test.log
cmake -S components/orcsdr_nxdn -B build/nxdn-sanitized -DCMAKE_BUILD_TYPE=Debug -DORC_NXDN_SANITIZERS=ON 2>&1 | tee ./nxdn-asan-configure.log
cmake --build build/nxdn-sanitized --parallel 2 2>&1 | tee ./nxdn-asan-build.log
ctest --test-dir build/nxdn-sanitized --output-on-failure -V 2>&1 | tee ./nxdn-asan-test.log
```

Replay exactly 48-kS/s CS16 interleaved signed LE IQ:

```sh
build/nxdn/orcsdr_nxdn_replay 48 /path/to/nxdn48_rdch_voice_447100_16oct_48k.cs16 --frames > ./nxdn48-events.jsonl 2> ./nxdn48.log
build/nxdn/orcsdr_nxdn_replay 96 /path/to/nxdn96_rcch_cc_453775_19oct_48k.cs16 --frames > ./nxdn96-events.jsonl 2> ./nxdn96.log
python3 tests/nxdn_replay_acceptance/check_replay.py --fixtures /path/to/NXDN --replay build/nxdn/orcsdr_nxdn_replay 2>&1 | tee ./nxdn-real-air-check.log
```

`sacch_reassembly.*` orders CRC-valid SACCH fragments into **opaque** 72-bit strings (no Layer 3 field layout, no CRC claim); `orcsdr_nxdn_replay ... --opaque` prints them (keep that output private).

No USB, firmware `main.cpp`, P25/DMR/D-STAR/YSF protocol code, dashboard renderer or TX modified. 