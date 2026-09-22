# Tab5 SD Write Performance

## Scope

This investigation measured the Tab5 microSD write path without changing the
SD wiring, ESP-Hosted firmware, or radio sample rate. The benchmark creates only
`/sd/orcsdr/sdbench.bin`, flushes and closes it, and removes it after every run.
It never formats the card or writes raw sectors.

Tested hardware and software:

- Branch: `codex/shortwave-dashboard-completion`
- Source base: `4e4174e3162e88d97d86073de082ea632193857d`
- ESP-IDF: 5.5.4
- Tab5 SD: SD32G SDHC, native 4-bit, 40.00 MHz, 512-byte sectors
- SD pins: CLK 43, CMD 44, D0 39, D1 40, D2 41, D3 42; LDO4
- ESP-Hosted C6 SDIO clock: unchanged at 10 MHz
- RTL-SDR: Blog V4 (`blog_v4_r828d`) with MLA-30+

## Root cause

**Primary root cause:** High-volume PSRAM buffers were not cache aligned for the
ESP32-P4 SDMMC DMA path. ESP-IDF 5.5.4 consequently used its inefficient
unaligned fallback, copying and writing one 512-byte sector at a time. The
runtime cache alignment was 128 bytes on the tested device. A cache-aligned
diagnostic buffer immediately raised throughput into the 4-5 MiB/s range.

**Contributing storage design issue:** OrcSDR forced writable stdio streams to
`_IONBF`, preventing controlled aligned stdio buffering and providing no
performance benefit in measured tests. Removing `_IONBF` alone left every
tested write size near 0.30 MiB/s.

**Supporting configuration:** `CONFIG_FATFS_VFS_FSTAT_BLKSIZE=4096`.

The production correction is deliberately small:

- remove forced `_IONBF`;
- give writable storage files a 32 KiB PSRAM stdio buffer allocated with
  `MALLOC_CAP_CACHE_ALIGNED`;
- cache-align the existing audio and IQ capture buffers;
- set `CONFIG_FATFS_VFS_FSTAT_BLKSIZE=4096`.

No producer/consumer queue was added. Audio and IQ are already captured into
PSRAM and exported only after acquisition stops, so a second queue would add
memory and synchronization without removing a live producer stall.

If the optional stdio buffer allocation or `setvbuf()` fails, file access stays
available through the normal libc buffering path. `RTL_SD_BENCH` reports the
actual buffer size, runtime alignment, pointer and size alignment, PSRAM
placement, and `setvbuf()` result once per benchmark run.

## Benchmark results

Each value is durable throughput measured after `fflush` and `fclose`. The 32
KiB and 64 KiB values are three-run averages; other sizes use one run.

| Write size | Baseline | `_IONBF` removed only | Final production | Improvement |
| --- | ---: | ---: | ---: | ---: |
| 4 KiB | 0.300 MiB/s | 0.298 MiB/s | 3.871 MiB/s | 12.9x |
| 16 KiB | 0.296 MiB/s | 0.301 MiB/s | 4.211 MiB/s | 14.2x |
| 32 KiB | 0.297 MiB/s | 0.299 MiB/s | 4.382 MiB/s | 14.8x |
| 64 KiB | 0.297 MiB/s | 0.300 MiB/s | 4.541 MiB/s | 15.3x |
| 128 KiB | 0.296 MiB/s | 0.302 MiB/s | 4.158 MiB/s | 14.0x |

32 KiB was selected because its final durable average is within 3.5% of 64 KiB
while consuming half the per-file buffer memory. The final 4 KiB and 16 KiB
results also prove that the shared stdio buffer aggregates small application
writes effectively.

At 2.4 MSPS, unsigned 8-bit interleaved IQ requires approximately 4.58 MiB/s.
The old path provided only about 6.5% of that rate. The corrected path is close
to the raw-IQ requirement, but continuous raw-IQ recording is not accepted by
this benchmark: filesystem variance, headers, competing tasks, and safety
margin still require a dedicated sustained-capture test.

Diagnostic interpretation:

- below 1 MiB/s: failure or likely unaligned fallback;
- 1-3 MiB/s: warning; investigate;
- 3 MiB/s or higher: healthy for normal OrcSDR storage;
- approximately 4 MiB/s or higher: expected current Tab5 behavior.

These are diagnostic expectations, not hard runtime gates. Bounded capture to
PSRAM followed by WAV or IQ export is supported. Continuous 2.4-MSPS raw IQ
directly to SD is not qualified by this work.

## Repeatable commands

Focused host checks:

```powershell
wsl.exe bash -lc "set -e; cd /mnt/f/Ai/OrcSDR/.codex-worktrees/shortwave-dashboard-completion; g++ -std=c++17 -Iapps/orcsdr-tab5/ui tests/sd_benchmark_plan_tests.cpp -o /tmp/sd_benchmark_plan_tests; /tmp/sd_benchmark_plan_tests"
& .\apps\orcsdr-tab5\tools\run-shortwave-ui-regression.ps1
```

Build:

```powershell
& .\apps\orcsdr-tab5\tools\build-tab5-idf.ps1
```

Authenticated on-device benchmark:

```powershell
& .\apps\orcsdr-tab5\tools\run-tab5-ui-regression.ps1 `
  -Port COM17 -SdBenchmark -SdBenchmarkMiB 32 `
  -PairingKeyPath <path-to-local-pairing-key> `
  -LogPath .\artifacts\sd-benchmark\production-buffered.log
```

Authenticated file-semantics check:

```powershell
& .\apps\orcsdr-tab5\tools\run-tab5-ui-regression.ps1 `
  -Port COM17 -SdSelfCheck `
  -PairingKeyPath <path-to-local-pairing-key>
```

## Final candidate evidence

- Application: `apps/orcsdr-tab5/build-native-hosted3/orcsdr_tab5.bin`
- Size: 2,479,664 bytes
- SHA-256: `8F9D1503F6CC090F5BE80AAFDC2FE24D7E9EA6030DDB8978F770556281B6D41A`
- Flash: COM17, ESP32-P4 revision 1.3; bootloader, partition table, and application hashes verified
- File semantics: create, write, flush, close, read, rename, and remove passed
- Radio serial state: streaming, V4 profile, 30.000 MHz tuner route,
  2,399,850 effective samples/s, zero USB overruns and zero consumer drops
- Recording serial state: 242,155 mono PCM samples at 48 kHz (5.04 seconds)
  written to `/orcsdr/rec_003_SHORTWAVE_30000000.wav`
- Raw-IQ export: 4,800,000 bytes captured in PSRAM with zero drops and written
  to `/orcsdr/iq_002_p25_30000000.orciq`; the `p25` filename token is a
  pre-existing generic diagnostic-export naming quirk
- ESP-Hosted/Wi-Fi: host and coprocessor 3.0.6 matched at the unchanged 10 MHz
  SDIO clock. The first saved-profile association failed with reason 2; one
  controlled retry connected at `192.168.1.75`, reproducing the known
  intermittent connection issue without making it part of this storage fix.

These are separate claims. Build, flash verification, SD benchmark, radio
telemetry, file semantics, bounded WAV/IQ export, and Wi-Fi retry passed.
Audible reception, physical UI, catalog/database writes, serial SD-file
transfer, RF Lab/session export, and sustained continuous 2.4-MSPS raw-IQ
recording were not tested for this final candidate.
