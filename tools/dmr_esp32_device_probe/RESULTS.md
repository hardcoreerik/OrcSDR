# D1-H memory and performance qualification report

**Status:** Host SPSC tests verified; actual D1 host-linked and ESP32-P4 runtime
measurements **NOT MEASURED** in this sandbox. Record branch/commit when
reproducing this run and attach logs to a follow-up report. Never substitute
PC timings for P4 timings or synthetic waveform detections for on-air decode.

## Verified host safety tests — 2026-11-11

| Test | Result | Scope |
|---|---|---|
| GCC 14.2.0 C++17 Release, `-Wall -Wextra -Wpedantic -Werror` | PASS | SPSC queue with fake receiver |
| `ctest` | **1/1 PASS** | Seven deterministic groups |
| GCC C++17 ASan/UBSan | **1/1 PASS** | Same queue, fake receiver |
| Concurrent producer/consumer | PASS | 30,000 attempts with payload-integrity checks |
| Sustained sample accounting | PASS | 120,000 x 240 = **28,800,000** simulated IQ samples (10 min at 48k) |
| Queue overload | PASS | All-or-nothing drop, counted gap, decoder discontinuity before resumption |
| Reset/retune | PASS | Epoch changes and intentionally invalid worker processing |
| Wrong rate / invalid / copy isolation | PASS | Reject rate mismatch, source mutation cannot alter queued IQ |
| Full native D0/D1 linked benchmark | **NOT RUN** | Decoder sources not mounted locally |
| Standalone ESP-IDF target build | **NOT RUN** | ESP-IDF 5.5.4 toolchain unavailable |
| ESP32-P4 physical profiling | **NOT MEASURED** | No connected Tab5 |
| Live Tab5 USB / DSP / P25 / microSD interference | **NOT MEASURED** | No firmware integration authorized |

Also checked C++17 syntax for the optional native host runner, synthetic pattern
generator, and standalone ESP-IDF task using **temporary API-compatible
stubs**. This is a compilation smoke test only; it is **not** an ESP-IDF
toolchain build, and cannot establish linkage against current D1. A separate
synthetic generator check verified both BS sync and Golay bit placement
with a D0 API stub. The host runner CLI was smoke-run against the stub
receiver in normal and overload modes; its CPU numbers are **not evidence**
of actual decoder throughput.

The 10-minute test measures queue correctness under a fake instantaneous
consumer. It is *not* ten minutes of timed real-time CPU throughput.

## Capacity and known architectural costs

| Item | Source | Size / timing |
|---|---|---|
| Input complex samples | `d1::ComplexSample` CF32 | **8 bytes** per complex I/Q, verify target `sizeof` |
| Per queue block | 240 complex samples | **1,920 IQ bytes**, 5 ms |
| Eight-block queue payload | 8 x 240 x 8 | **15,360 bytes**, max 40 ms + metadata/atomics |
| `offer` maximum | 960 samples | 20 ms / 7,680 bytes, all-or-nothing |
| Copy throughput at 48k | 48,000 x 8 | **384,000 bytes/s** into queue, excluding channelizer |
| 4FSK frontend object | `sizeof(d1::Frontend)` | **NOT MEASURED**, generated on target |
| 1,024-sample frontend pre-roll history | `filtered_history_` | **4,096 bytes** within frontend object |
| D1 protocol object/FEC scratch | `sizeof(d1::Decoder)` + stack | **NOT MEASURED**, generated on target |
| D1 bridge receiver object | `sizeof(integration::Receiver)` | **NOT MEASURED**, generated on target |
| Bridge sample index map | 256 x uint64 | **2,048 bytes** within bridge object |
| Combined attachment | `sizeof(Attachment)` | **NOT MEASURED**, generated on target |
| Synthetic stimulus | 2,880 CF32 samples | **23,040 bytes** allocated before run |
| Dedicated worker task stack | ESP-IDF task config | **20,480 bytes reserved**; actual high-water **NOT MEASURED** |
| Host CPU/latency processing | Actual D1 host executable | **NOT RUN** |
| P4 CPU / max / p99 latency | `esp_timer_get_time()` around `drain_one` | **NOT MEASURED** |
| P4 heap internal peak | `heap_caps_get_minimum_free_size(INTERNAL)` | **NOT MEASURED** |
| P4 PSRAM peak | `heap_caps_get_minimum_free_size(SPIRAM)` | **NOT MEASURED** |
| Tab5 wideband -> 48k DDC | Existing production radio service | **No approved DMR 48k tap identified**; cost NOT MEASURED |

Object values exclude incidental logging, FreeRTOS task metadata, monitor
stack, DSP processing stack locals and other firmware facilities. The probe
reports actual target `sizeof` and minimum heap/stack watermarks when run.
Do not double-count `Frontend`/`Decoder` when adding `sizeof(Receiver)`;
`Receiver` already embeds both.

## Device acceptance template (complete only after physical test)

| Field | Current |
|---|---|
| Target / board / serial / PSRAM manufacturer | NOT MEASURED |
| ESP-IDF and git commit | NOT MEASURED |
| 30 / 60 / 120-second runs | NOT MEASURED |
| Channelized complex IQ rate / producer miss | NOT MEASURED |
| Accepted / processed / lost samples | NOT MEASURED |
| Queue depth max / residual | NOT MEASURED |
| Worker block average / max / p99 bucket | NOT MEASURED |
| Worker execution fraction (total processing / elapsed) | NOT MEASURED |
| Frontend locks / full-preroll / protocol events | NOT MEASURED |
| D1 valid identities / integrity gate | NOT MEASURED |
| Internal heap before/after/low watermark | NOT MEASURED |
| PSRAM before/after/low watermark | NOT MEASURED |
| Worker stack high-water bytes | NOT MEASURED |
| Retune epoch, injected missing IQ, overload recovery | NOT MEASURED |
| Live RTL-SDR 48k channel subscription | NOT IMPLEMENTED |
| Concurrent USB capture drops / audio / P25 / microSD | NOT MEASURED |

**Do not declare the sustained 48 kS/s gate satisfied until target hardware
shows no unbounded queue growth, no unintended loss, and acceptable free
internal memory under the actual live radio workload.** The standalone
synthetic image is a preliminary CPU/memory bound, not that final gate.
