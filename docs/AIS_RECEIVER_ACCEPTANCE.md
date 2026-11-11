# AIS RF receiver acceptance and integration contract

**Development branch:** chatgpt/ais-dashboard-20261106
**Status (2026-11-10):** integrated into the Tab5 firmware; synthetic IQ replayed over rtl_tcp decodes on the device (see [evidence](evidence/ais/2026-11-10-tab5-replay.md)). Still **not** live-RF acceptance: no real ship has been received. The "Next integration" list below is historical; items 1, 2 and part of 4 are now done.
**Scope:** Original receive-only C++17 implementation; research inputs remain in separate RSA/REA branches.

## Production seam

- Input is interleaved CU8 IQ, **240,000 complex samples/s**, with radio tuned to **162,000,000 Hz**. One existing radio stream supplies AIS1 (161.975 MHz) and AIS2 (162.025 MHz).
- ais_receiver.hpp / ais_receiver.cpp: mixer/channelizer, per-channel FIR and /5 decimator (48 kS/s), discriminator, five fixed 9600-symbol/s sample-phase hypotheses, NRZI, HDLC bit destuffing, reflected 0x8408 CRC-16/X25. Callback receives CRC-valid payloads with the correct MSB-first AIS field byte order after LSB-first wire byte reconstruction.
- ais_receive_pipeline.hpp / .cpp: packet callback -> ais::parse() -> bounded ais::StationStore. It does NOT own the RTL-SDR or tune the radio. It checks center_hz, sample_rate, nonzero source generation and monotonic block time; it resets DSP history when generation changes or a reported gap exceeds 250 ms.
- Use one DSP-task owner; never expose the mutable station store directly to UI. A future app adapter must publish bounded immutable ais_ui::Snapshot copies through appropriate synchronization. The protocol parser has only partial per-field coverage for 28 message types.
- Allocate the receiver/pipeline with an appropriate lifetime (PSRAM for large objects on ESP32-P4). Do not put a whole Pipeline on a small FreeRTOS task stack. No allocations, network, filesystem or M5GFX operations inside the packet callback.
- Pass actual radio-session generation and tune info, not invented constants. On a known dropped IQ block or retune, call discontinuity().
- Type 27 long-range channels **156.775 / 156.825 MHz** require a separate future tuning mode, not this 162 MHz IQ window.

## DSP and diagnostic caveats

Independent +/-25 kHz NCOs (48-sample oscillator period at 240 kS/s), 63-tap windowed-sinc channel lowpass (10.5 kHz cutoff), /5 decimator, atan2 phase discriminator, slow DC correction and 3-sample smoothing. Five static symbol-phase hypotheses handle initial burst alignment. Concurrent hypothesis outputs are deduplicated within an 80 ms window.

NOT implemented: true dynamic symbol-clock recovery, adaptive equalization, weak-signal sensitivity tuning, gain selection, UI ownership, transport/NMEA output, alternate IQ rates, or real hardware acceptance.

Candidate-frame and error counters are **per timing hypothesis**, not unique on-air events. Deduplicated crc_ok counts should not be divided by summed candidate counts as a packet success rate. A validated RF CRC is not the same as NMEA XOR checksum.

The parser reports header_only for most types. A CRC-valid frame is not proof of complete message interpretation. AIS messages with non-multiple-of-eight meaningful bit lengths need downstream explicit padding handling, not silently accepted or rejected.

## Host test reproduction

From OrcSDR repository root on Linux/WSL with a C++17 compiler, create a local log directory:

    mkdir -p ais-test-runs
    g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror \
      -fsanitize=address,undefined -fno-omit-frame-pointer \
      -Iapps/orcsdr-tab5/ui \
      apps/orcsdr-tab5/ui/ais_receiver.cpp \
      apps/orcsdr-tab5/ui/ais_receive_pipeline.cpp \
      apps/orcsdr-tab5/ui/ais_protocol.cpp \
      apps/orcsdr-tab5/ui/ais_station_store.cpp \
      tests/ais_receiver_tests.cpp -o ais-test-runs/ais-receiver-tests
    ./ais-test-runs/ais-receiver-tests 2>&1 | tee ais-test-runs/ais-receiver-tests.log

The test exercises an independently known FCS/X25 check value (123456789 -> 0x906E), HDLC flag/stuffing/corrupt FCS handling, Gaussian-shaped synthetic IQ, both channels including simultaneous transmissions, arbitrary input block splits, inverted symbol polarity, +/-800 Hz synthesized carrier offsets, 1008-bit maximum-length HDLC frame, seeded random noise, source generation and IQ gap resets, and end-to-end packet->parser->station store.

Expected terminal line: AIS dual-channel GMSK/HDLC/FCS host IQ tests passed (synthetic only).

Host-only NO-SIGNAL microbenchmark (not RF sensitivity or ESP32-P4 validation):

    g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror \
      -Iapps/orcsdr-tab5/ui \
      apps/orcsdr-tab5/ui/ais_receiver.cpp \
      tests/ais_receiver_bench.cpp -o ais-test-runs/ais-receiver-bench
    ./ais-test-runs/ais-receiver-bench 2>&1 | tee ais-test-runs/ais-receiver-bench.log

Build test assertions with assertions enabled: do not use -DNDEBUG for test runs; the C++ assertions also contain test calls. The separate host benchmark has no RF sensitivity claims.

## Next integration / acceptance gates

1. **Radio session:** add AIS to existing band/owner navigation/dispatch when safe; allocate Pipeline for the owning DSP lifetime and forward radio-owned 162 MHz CU8 blocks at 240 kS/s with the true session generation. No second USB owner or unnoticed tuning changes.
2. **UI:** publish immutable, bounded snapshots of validated packets and station state. Extend UI identity fields so Type 28 Source IDs never appear as MMSIs.
3. **Independent real IQ parity:** release RTL-SDR V4 from competing processes, capture actual AIS1/AIS2 traffic with saved sample format, frequency/rate/gain/antenna/time, and SHA-256. Compare **identical IQ** against AIS-catcher and gr-ais, recording decoder versions and exact packet mismatches.
4. **Tab5:** build full ESP-IDF firmware; measure CPU, PSRAM/heap/stack, per-block processing times, USB/queue IQ drops, UI responsiveness and sustained 15-minute reception. A desktop no-signal benchmark is not MCU evidence.
5. **Competitor benchmark:** controlled sensitivity/SNR, adjacent-channel blockers, frequency/clock offsets, simultaneous traffic, packet collisions, overload behavior and reproducible reception parity. Do not claim to be best until measured.
6. **Protocol completion:** independent field-by-field verification for all 28 types, message 21 extension, message 28 geometry, message 27 alternate tuning, DAC/FI and variable-bit padding.
7. **OrcMaps:** consume its future standalone generic overlays through a pinned external ESP-IDF dependency, without modifying OrcMaps here.

No main merge or release without explicit authorization and review of the hardware/RF evidence.
