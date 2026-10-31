# WEFAX PlutoSDR conducted bench harness

Experimental only. The WEFAX production firmware is not yet wired up. The harness supplies a deterministic test waveform, a host WAV replay decoder and an offline test runner.

## Bench layout

Use the *already-established* SSTV setup: Pluto TX1 -> 30 dB inline RF attenuator -> same coax -> tinySA Ultra for fresh receiver-end measurement, then stop/mute the source and move the coax to the RTL-SDR connected to the Tab5. Never connect Pluto TX1 directly to the dongle, or put an antenna on the Pluto transmitter during this test.

Prior evidence: docs/evidence/maps/2026-10-26-v5-sstv-conducted.md. Historical settings: 145.500 MHz, measured -71.5 dBm at the end of the attenuated chain for one specific transmitter configuration, Pluto URI ip:192.168.1.228. These **are historical facts, not current identity or calibration**.

WEFAX test signal is USB, not SSTV NFM. A positive-frequency 1500-2300 Hz complex baseband signal above the Pluto dial frequency produces the audio range expected by the WEFAX host decoder when received as USB. The existing Pluto's extended VHF range must be rechecked before attempting 145.500 MHz.

## Host-side tests (no RF)

From an OrcSDR checkout on the dedicated WEFAX branch:

```bash
python3 -m pip install numpy
python3 tools/wefax_pluto_lab.py selftest --out-dir .local/wefax/reference
python3 tools/wefax_pluto_lab.py generate --out-dir .local/wefax/reference
bash tools/wefax_pluto_host_tests.sh
```

The default generated audio has 5 s of IOC selection video, 30 s of phasing, 10 rows at 120 LPM (5 s), and 5 s of stop video: 45 seconds. Output includes 48 kHz PCM WAV, original grayscale PGM, a JSON file with hashes and start offset. IOC 576 is 1810 pixels per row; IOC 288 is 905.

The host runner compiles the actual `wefax_core.cpp` and the new `wefax_wav_replay.cpp`, tests two format combinations, compares reconstructed pixel images, and verifies that RF operation is refused without an explicit conducted-chain confirmation. A test log is always written as `wefax_pluto_host_test.log` **in the folder where the command is run**. The waveform generator appends `wefax_pluto_lab.log` in the same folder.

For explicit replay of a captured or generated mono signed-16 PCM WAV (12 kHz or 48 kHz):

```bash
g++ -std=c++17 -O2 -Iapps/orcsdr-tab5/ui tools/wefax_wav_replay.cpp apps/orcsdr-tab5/ui/wefax_core.cpp -o wefax-replay
./wefax-replay .local/wefax/reference/wefax_reference_48k.wav decoded.pgm 35 10 576 120 > wefax_replay.log 2>&1
```

The 35 s offset is valid **only for the exact synthesized recording**, not a captured RF WAV whose recording start time may differ.

## Real hardware test sequence

1. Use the local guarded SSTV Pluto utility at `F:/Ai/PlutoSDR/sstv/orc_sstv` to confirm source identity and muting, and recalibrate power at the tinySA connected beyond the physical pad. Do not infer live hardware state from old documentation.
2. Open OrcSDR's existing SSTV screen, set the receiver to **USB** at the test RF frequency, bias tee off, then record received PCM with the established authenticated `SSTV REC START 55` command. This is a *recording bridge*, not proof that the SSTV decoder handles WEFAX.
3. Only after real equipment, cable and current input-power verification may the operator explicitly invoke the `transmit` command in `wefax_pluto_lab.py` with the live Pluto URI, expected serial, pad confirmation and newly measured receiver-end level. There is **no automatic TX** or RF activity in host tests. The command fails closed if identity or key powerdown readback checks are unavailable, and attempts a verified TX LO shutdown when finished.
4. Stop, save and read back the actual PCM recording. Use `wefax-replay` with the *observed* image start offset; score it against the original source PGM. Record raw audio, SHA-256, decoder row count, RSSI/power and all source/receiver faults.

**Limitations:** The new pyadi-IIO transmitter uses non-cyclic chunk writes. The Pluto DMA underflow counters are not yet validated by this harness, so completing a TX loop is **not** evidence of gap-free RF. The resulting transmission must be treated as a preliminary RF experiment. Migrate to the existing hardened local SSTV Pluto streaming backend after verifying its input API, identity guards, DMA readback and LO mute behavior. No physical RF test was conducted in this chat. No ESP32-P4 flash, UI change, release or hardware acceptance is claimed.

Relevant upstream references: WMO-No. 386 radiofacsimile; Analog Devices TX LO powerdown documentation; pyadi-IIO buffer semantics. See `docs/wefax/WEFAX_PHASE1.md` for decoder limitations.
