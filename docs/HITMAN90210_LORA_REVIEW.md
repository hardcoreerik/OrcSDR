# Hitman90210 fork review: LoRa / Meshtastic

Review date: 2026-09-08  
Upstream base: `376230ab9aa49c7abaf36c6c42e7b5a1dde41140`  
Fork head: `4e5434397d4f9f2c393a962209b9eb81afca3ed4`  
Merge base: upstream base; fork was 46 commits ahead and 0 behind when fetched.

The aggregate fork diff is not suitable for merging: 71 files and substantial
line-ending churn mix CI, Wi-Fi/SDIO, FM, maps, catalog, screenshots, local
setup, and LoRa work. Changes were reviewed commit-by-commit instead.

## LoRa recommendations

| Fork commit | Class | Finding | Upstream action |
|---|---|---|---|
| `e10cd59` | B | Correctly identifies the missing channel picker and implements US slot retuning, serial actions, and overlay repaint suppression, but mixes unrelated local ATC work and is US-only. | Independently implemented a regional LongFast picker and serial controls; retained overlay repaint suppression. Credit required for the picker/testing concept. |
| `45c7651` | A/B | Correctly proves automatic captures were discarded because the native decoder was never started. Starting it from dashboard painting fixes the symptom but is the wrong lifecycle boundary. | Start it from the shared LoRa receiver entry, which covers UI, serial, and auto-start. Explicitly credited in the commit. |
| `0ed1922` | B | Validated persistence is useful, but saving a derived US frequency is redundant and region-specific. | Persist validated region index plus slot; derive frequency and repair corrupt NVS values. Credit required for the persistence/testing concept. |
| `4e54343` | B | Direct numeric entry is useful for very large US plans, but the commit is line-ending polluted and retains local quick-slot choices. | Kept the smaller region/slot stepper and direct serial selection. Add a keypad only if hardware use shows it is needed. |

The fork's `lora_native_decoder.cpp/.hpp` are byte-for-byte identical to
upstream. Its LoRa contribution is lifecycle, tuning UI, persistence, and
hardware investigation—not a replacement PHY decoder.

## Other fork findings

| Commits/theme | Class | Recommendation |
|---|---|---|
| `201753e` local map labels | A | Removed the Lane County dashboard label while touching the same LoRa view. Credit required for identifying the upstream-neutrality issue. |
| `6b95b06` ring-buffer waterfalls | B/E, separate | Potentially improves overlay/display stability, but it is a broad FM/catalog/UI commit with PSRAM implications. Review independently. |
| `aa17942`, `daeffd0`, `9b50748` radio/Wi-Fi/SDIO coordination | A/B, separate | Hardware evidence is interesting and may fix real SDIO stalls. Reproduce on upstream hardware before importing; do not couple it to LoRa. |
| `13ce3ee` through `392c7e2`, `86f772c` Wi-Fi transport/retry work | B/E, separate | Useful investigation, but interdependent and high-risk around pinned ESP-Hosted 3.0.6. Needs its own branch and A/B test. |
| `b2b70c2` PowerShell 5.1 transfer fixes | A, separate | Narrowly review and upstream if current scripts still fail under Windows PowerShell 5.1. |
| `542abf4` legacy USB deletion | C | Upstream already enforces the single `esp_rtl_sdr` path. |
| `9644247`, `404c027`, `69a1166` stack/self-check fixes | B, separate | Review against current upstream; no LoRa dependency. |
| `7bf683a` through `d972d4a` screenshots and UI polish | F/D | Mostly unrelated; several commits include fork-local layout and setup assumptions. Select only reproducible defects. |
| `a67a236` through `8390a3c`, `622c776`, `eb27ea4` CI/docs | F | No LoRa dependency. Review in the CI lane; never import fork identity/link rewrites (`bae23e3`). |
| `c9b32d3`, `7692f79`, `04089a4` FM/weather work | F | Separate radio/UI features. |
| `a749617`, `e10cd59` local data generation | D/B | OurAirports tooling may be useful, but local paths/presets must not become upstream defaults. |
| merge and handoff commits `8f65e80`, `b941f6f`, `d321a8a` | F | Historical context only; do not cherry-pick. |

## Receiver and capture findings

- Automatic flow: RTL bulk IQ → shared stream callback → `lora_iq_offer()` →
  adaptive level/noise threshold → 250 ms pre-roll → four-second CU8 capture →
  decode queue → CSS/FFT, sync/header/FEC/CRC → Meshtastic envelope/AES-CTR/
  protobuf → bounded packet/node snapshots → dashboard and optional SD log.
- The detector learns from 12 level observations, triggers 9 dB above its
  adaptive floor (clamped to -78…-25 dBFS), uses 3 dB hysteresis, and rearms
  below the lower threshold. The 250 ms pre-roll exceeds the nominal 128 ms
  LongFast preamble.
- The previous three-second capture could truncate maximum-size LongFast
  packets (documented airtime can exceed 3.2 seconds), so it is now four seconds.
- Upstream's shared RF-analysis refactor initialized only an 8,192-point global
  ESP-DSP FFT table while SF12 decoding can request 32,768 points. The table is
  now 32,768 while normal analysis buffers remain capped at 8,192.
- Existing diagnostics already distinguish capture start/done, preambles,
  header failures, CRC pass/fail, encrypted packets, CFO, decode time, USB
  overruns, consumer drops, and decoder readiness. No hot-path serial spam was
  added.
- Region definitions, selected region/slot, NVS validation, and regional survey
  scheduling live in `lora_channel_control`, not `main.cpp`. The main file only
  maps shared UI/serial actions to that module and coordinates the shared tuner
  and decoder lifecycle.

## Validation gates

- Native ESP-IDF 5.5.4 / ESP-Hosted 3.0.6 build: pass.
- App size after regional selector: about 2.20 MiB, 45% partition free.
- Device self-check includes US slot 20 = 906.875 MHz, EU433 slot 4 =
  433.875 MHz, EU868 slot 1 = 869.525 MHz, off-grid rejection, and all 24 plans.
- Flash, reboot persistence, live retune, serial evidence, and end-to-end RF
  decode remain separate hardware gates and are not claimed by this review.

## Attribution

Lifecycle bug discovery and the channel-picker/persistence test concepts came
from Hitman90210's fork: <https://github.com/Hitman90210/OrcSDR>. The lifecycle
commit cites `45c7651` directly. The regional implementation is independent and
uses current Meshtastic region definitions rather than copying the fork's US-only
table.
