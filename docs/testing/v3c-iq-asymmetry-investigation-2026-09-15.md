# V3c IQ asymmetry investigation — 2026-09-15

## Scope and identities

- OrcSDR branch: `codex/v3c-iq-asymmetry-investigation`
- OrcSDR diagnostic head: `ae6c497`
- Production driver pin/lock: `e1ca40e04f8140245d56837cd149bf901f771441`
- Driver-reported version: `0.8.0-rc2`
- Diagnostic-only driver instrumentation: `94ecd187070b39f370c5d7f0dc55a56762dd6c86`
- Firmware: `orcsdr_tab5.bin`, 2,403,872 bytes, SHA-256
  `43EA3C10ED0782A2ABABF798F076EDC907F416FE806057FC9DBC87B0A587655C`
- COM17 flash completed with bootloader, application, and partition hashes verified.
- The production driver repository, pin, and dependency lock were not changed.

Raw CU8 was captured before OrcSDR FFT, filtering, demodulation, and UI rendering.
Each accepted capture is 4,800,000 bytes at 2.4 MS/s. Source/build/flash,
serial state, raw-IQ analysis, audio, and physical UI observations remain separate
claims.

## Conclusion

This is a driver initialization defect, not an OrcSDR spectrum, FFT, audio,
or UI defect. The V3c cold normal-tuner path leaves the tuner in a different
owned register state from the already-working direct-Q-to-normal path. The
failure is present in pre-DSP CU8, with no USB overruns, consumer drops, or
short transfers, and disappears after a driver route transition while the
OrcSDR capture and analysis path remains unchanged.

This evidence does not prove the meaning of every bit in tuner register 0x05,
nor does it by itself approve a production driver change. It isolates the
first observed owned-state difference and defines the hardware gates a fix
must pass.

## Discriminating results

| Device/path | RF | Antenna and suitability | Transition | Median negative / positive | Median separation | Half-power delta | Transport |
|---|---:|---|---|---:|---:|---:|---|
| V4 normal tuner | 99.100 MHz exact | 27-inch-per-leg dipole; suitable near FM | cold/restart | -69.044 / -69.478 dBFS | 0.434 dB | -0.762 dB | 0 overruns, 0 drops |
| V3c normal tuner | 99.100 MHz exact | same dipole | cold | -79.881 / -69.316 dBFS | 10.565 dB | +9.999 dB | 0 overruns, 0 drops |
| V3c direct-Q | 10.000 MHz exact | same dipole; unsuitable for reception, transition-only | normal to direct-Q | -64.221 / -62.564 dBFS | 1.657 dB | +1.671 dB | 0 overruns, 0 drops |
| V3c normal tuner | 99.100 MHz exact | same dipole; suitable near FM | direct-Q to normal | -47.522 / -47.091 dBFS | 0.431 dB | +0.121 dB | 0 overruns, 0 drops |

The V3c cold defect reproduced repeatedly in FM and Browse modes. A normal
99.1 -> 96.1 -> 99.1 MHz tuner-only round trip did not clear it. Crossing into
direct-Q at 10 MHz and immediately returning to the normal tuner did clear it.

The 10 MHz result is route-transition evidence only; the dipole was not suitable
for a 10 MHz reception claim. The earlier V3c 1.450 MHz MLA-30+ result remains
the antenna-suitable direct-Q reception result and had user-confirmed good audio.

Additional raw-sample checks support the same boundary:

| Capture | I/Q RMS | I/Q RMS delta | Clipping |
|---|---:|---:|---:|
| V4 cold 99.1 | 19.532 / 19.561 | -0.013 dB | 0.000000% |
| V3c cold BAD 99.1 | 0.788 / 0.788 | -0.002 dB | 0.000000% |
| V3c direct-Q 10 | 2.302 / 2.301 | +0.005 dB | 0.000000% |
| V3c return GOOD 99.1 | 50.006 / 49.978 | +0.005 dB | 0.012667% |

The nearly equal I/Q RMS values rule out a simple missing I or Q byte lane.
The large cold-versus-return level change means the final correction still
needs a fixed-gain, non-clipping comparison before the register state can be
declared correct.

## First differing owned state

Both 99.1 MHz captures reported the same high-level state: `NORMAL_TUNER`,
complex I/Q input, PLL IF 3,570,000 Hz, demodulator IF 3,570,000 Hz, exact RF,
manual 0.0 dB software gain, RTL AGC off, and zero short transfers.

The diagnostic write shadow found one differing tuner register:

```text
cold BAD:           t05=e3 t06=30 t07=75 t0c=68 t17=20
direct-return GOOD: t05=83 t06=30 t07=75 t0c=68 t17=20
both demod:         d06=80 d08=4d d15=01 d19=38 d1a=11 d1b=12 db1=1a
```

The shared V4 initialization table writes tuner register 0x05 as `0xE3` late in
the cold-start tail. The V3c direct-to-normal path replays the bounded tuner
reinitialization slice and ends at `0x83`. That one write-state difference
correlates exactly with the BAD/GOOD raw spectra.

## Evidence files

- V4 control: `artifacts/v3c-iq-investigation/20260915-134524-v4_dipole_fm_99m1.*`
  - CU8 SHA-256: `a1bdf7ec48967a29728deb2dd8de1cd3970f76d60910d9256aaa48f0d2774067`
- V3c cold BAD with register probe:
  `artifacts/v3c-iq-investigation/20260915-143847-probe_cold_bad_99m1.*`
  - CU8 SHA-256: `3f969c510bb7e3557542dde9d6b320d293622479b4d63e1b92cb8ac27c0cd661`
- V3c direct-Q transition:
  `artifacts/v3c-iq-investigation/20260915-143926-probe_normal_to_direct_10m.*`
  - CU8 SHA-256: `6d46037000b4876e720facf221fbf09d66a89e49bbc8238253c6444e6cd0dbd5`
- V3c return GOOD with register probe:
  `artifacts/v3c-iq-investigation/20260915-144005-probe_direct_return_good_99m1.*`
  - CU8 SHA-256: `69b0e6cce2076aa4f7f3544a8a98592fa0024ee97298c2a4794ce15b566af54d`

Rejected attempts that hit FM/AM dashboard frequency clamps were not accepted
as transition evidence and produced no labeled final CU8 result.

The Home dashboard frequency was changed to 1.600 MHz during one earlier
attempt. That run was rejected as confounded. The accepted transition sequence
used Browse mode, explicit frequencies, the same V3c, and the same dipole with
the screen untouched. The V4 control used the same 99.1 MHz target and dipole.

## Exact verification commands

```powershell
python apps/orcsdr-tab5/tools/test_analyze_rtl_iq.py
apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1 -SelfCheck
cmake -S tests/host -B .codex-build-host
cmake --build .codex-build-host
ctest --test-dir .codex-build-host -C Debug --output-on-failure
. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
idf.py -B build-native-hosted3 build
idf.py -B build-native-hosted3 -p COM17 flash
```

Results: analyzer 3/3 passed; UI self-check passed; driver host tests 2/2
passed; native ESP32-P4 build passed; COM17 flash passed with hashes verified.

The accepted captures used these runner invocations (line wrapping added only
for readability):

```powershell
# V4 cold control at 99.1 MHz
apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1 -IqDiagnostic `
  -IqBand FM -IqFrequency 99100000 -IqTransition v4_dipole_fm_99m1 `
  -IqOutputPath artifacts/v3c-iq-investigation `
  -IqAntenna 'Dipole, 27 inches per leg (54 inches tip-to-tip)' `
  -IqAntennaSuitability 'Suitable near the FM broadcast band; slightly shorter than a 99.1 MHz half-wave dipole'

# V3c cold normal-tuner reproduction
apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1 -IqDiagnostic `
  -IqBand BROWSE -IqFrequency 99100000 -IqTransition probe_cold_bad_99m1 `
  -IqOutputPath artifacts/v3c-iq-investigation `
  -IqAntenna 'Dipole, 27 inches per leg (54 inches tip-to-tip)' `
  -IqAntennaSuitability 'Suitable near 99.1 MHz; cold normal-tuner register-state probe'

# Same running V3c, cross into direct-Q
apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1 -IqDiagnostic -IqHotTune `
  -IqBand BROWSE -IqFrequency 10000000 -IqTransition probe_normal_to_direct_10m `
  -IqOutputPath artifacts/v3c-iq-investigation `
  -IqAntenna 'Dipole, 27 inches per leg (54 inches tip-to-tip)' `
  -IqAntennaSuitability 'Not suitable for 10 MHz reception; route-transition probe only'

# Same running V3c, return immediately to the normal tuner
apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1 -IqDiagnostic -IqHotTune `
  -IqBand BROWSE -IqFrequency 99100000 -IqTransition probe_direct_return_good_99m1 `
  -IqOutputPath artifacts/v3c-iq-investigation `
  -IqAntenna 'Dipole, 27 inches per leg (54 inches tip-to-tip)' `
  -IqAntennaSuitability 'Suitable near 99.1 MHz; immediate direct-Q-to-normal register-state probe'
```

Each invocation recorded antenna provenance, driver state, health counters,
SHA-256, spectrum PNG, waterfall PNG, report, and CSV. The diagnostic driver
overlay only shadowed writes already issued by the driver; it added no tuner or
demodulator reads and no new hardware writes.

## Proposed correction and required gate

Do not copy the full V4 cold-start tuner tail into the V3c final normal-tuner
state. The smallest evidence-backed candidate is to make V3c cold normal-tuner
startup finish with the same bounded tuner-reinitialization state already used
by the proven direct-Q-to-normal path, then restore the matched 3.570 MHz demod
IF and tune normally. The intended final `0x05` state must also be reconciled
with the existing V3c manual-gain table so software gain reporting matches the
hardware write.

Before release, the candidate must pass:

1. host/profile regression;
2. native build and flash identity;
3. V3c cold 99.1 MHz raw balance without first visiting direct-Q;
4. fixed-gain V3c cold/return comparison without clipping;
5. V3c 99.1 MHz audible/RDS and physical spectrum acceptance with the dipole;
6. V3c 10 MHz direct-Q and 99.1 MHz return transitions;
7. V4 cold 99.1 MHz and LF/HF regression; and
8. power-cycle and USB detach/reattach checks.

## Final clean-candidate results

The approved correction was implemented on driver branch
`codex/v3c-cold-init-fix` from `e1ca40e04f8140245d56837cd149bf901f771441`.
Commit `9d2b33d681ab4c860d1c1b260547b00b762d93a7` preserves the captured tuner-
repeater ordering around the bounded reinitialization replay. No hardcoded
`0x83` write, gain-table change, public API change, version bump, or dependency
change was made.

Driver host tests passed 2/2, truth hygiene returned `TRUTH_HYGIENE_OK`, the
ESP-IDF 5.5.4 P4 smoke build passed, the IQ analyzer passed 3/3, the Tab5 UI
self-check passed, and the native Tab5 build passed. The flashed application
was 2,402,272 bytes with SHA-256
`97293f1000a5e88ce4f25dc50a012e3d3c673253a4c74ffb4756355541816854`.

All accepted V3c 99.100 MHz cold, power-cycle, USB-reattach, and direct-Q-return
captures used the 27-inch-per-leg dipole and measured less than 2 dB spectrum-
half separation and half-power delta with zero transport faults. The repeated
cold 22.9 dB capture clipped 0.101271%, narrowly above the strict <0.1% gate;
the returned capture clipped 0.075958%. A final cold repeat clipped 0.340042%
while measuring 0.194 dB median-half separation and -0.652 dB half-power delta.
CU8 SHA-256:
`5b3ae0ccbd0101383f3620c09a6cc47be135c55e59c62b9c0d86faaacd6b442c`.
The gain did not reintroduce asymmetry.
The 10 MHz dipole capture is transition-only because the antenna is unsuitable
for a reception claim at that frequency. Boot-to-Home and the corrected physical
spectrum were user-confirmed separately.

The V4 cold 99.100 MHz regression used the same FM-suitable dipole. It identified
`blog_v4_r828d`, reported exact frequency at 2.4 MS/s, and measured 0.859 dB
median-half separation, -1.129 dB half-power delta, 0% clipping, and zero
overruns, drops, or short transfers. CU8 SHA-256:
`9a8f0e2be6a478c37d892efdcbcba907666fcb2676878b813720105cb6b5aab4`.
The user separately confirmed clear audio, RDS "99.1 The Beat of Eugene", and
PTY "Adult Hit".

With the MLA-30+ active loop, the V4 cold 1.450 MHz capture reported exact
frequency at 2.4 MS/s through `HF_UPCONVERTER`, with zero clipping, overruns,
drops, or short transfers. CU8 SHA-256:
`8efac3ba6afae7074a90abb0ab5355ba31ea44c722bbb510a0ad333d9d67a3b7`.
The driver route suite passed exact-frequency transitions across 28.8 MHz in
both directions with continuous IQ and zero transport faults. The user
separately confirmed understandable 1.450 MHz audio and the correct on-screen
frequency/route.

The user separately confirmed normal understandable V3c audio, complete RDS
information, and a physically restored two-sided spectrum at 99.1 MHz. The
user also confirmed that the V4 99.1 MHz physical spectrum looked normal.

The strict V3c cold 22.9 dB clipping threshold remains open. True LF reception
also remains unverified because no appropriate LF antenna and signal source
were used.

### Fixed-gain clipping sweep

A follow-up cold-start sweep used the same V3c and FM-suitable 27-inch-per-leg
dipole at exact 99.100 MHz:

| Gain | Clipping | Median-half separation | Half-power delta |
| ---: | ---: | ---: | ---: |
| 0.0 dB | 0.000000% | 0.634 dB | -0.155 dB |
| 0.9 dB | 0.000000% | 0.885 dB | -0.501 dB |
| 7.7 dB | 0.000000% | 0.026 dB | -0.439 dB |
| 14.4 dB | 0.007021% | 0.303 dB | -0.351 dB |
| 22.9 dB | 0.340042% | 0.194 dB | -0.652 dB |

All points were transport-clean. The highest tested setting below the 0.1%
gate and preferred 0.02% margin was 14.4 dB. The result isolates the remaining
failure as strong-signal clipping at forced 22.9 dB, not a recurrence of the
cold-start asymmetry. No gain-table or IF/VGA change is justified by this
sweep alone.

### Clipping-aware Smart Gain follow-up

OrcSDR now measures raw CU8 endpoint clipping in the shared IQ path. FM and AM
software gain selection will not increase gain while clipping exceeds 0.1%,
and an already-selected automatic gain backs down one supported step every
500 ms until the overload clears or the lowest gain is reached. The control is
labelled `SMART` to distinguish it from the driver's hardware `TUNER AGC`.
FM, AM, and Shortwave show a red `CLIP` indication above the same limit.

The final native build passed with a 2,403,264-byte application image. On the
V3c at exact 99.100 MHz with the FM-suitable 27-inch-per-leg dipole, a focused
22.9 dB run remained transport-clean and measured only 0.003-0.005% live
clipping under the then-current reception conditions. A bounded 49.6 dB run
deliberately crossed the warning limit, rising from 1.342% to 5.309% and ending
at 4.783%, while still reporting zero overruns and drops. The harness restored
software Smart Gain after both tests. This proves the live meter and warning
input respond to real overload; the step-down policy is covered by the AM
dashboard self-check. Physical visibility of the `CLIP` label remains a
separate UI acceptance claim.
