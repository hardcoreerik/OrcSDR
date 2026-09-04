# Tab5 build and ESP-Hosted policy

> Current migration details, exact pins, and acceptance status live in
> [Tab5 ESP-Hosted 3 migration](user-guide/tab5-esp-hosted-3-migration.md).

OrcSDR's Tab5 firmware is a native ESP-IDF project. Build and flash the P4
with Espressif `idf.py`; do not use PlatformIO for development, release, or
hardware validation.

Arduino, M5Unified, and M5GFX are ESP-IDF components in the dependency graph,
not an Arduino-IDE or PlatformIO toolchain.

## Pinned Tab5 display patch

The Tab5 uses two DSI framebuffers for full-frame UI rendering. The tracked
patch at `apps/orcsdr-tab5/tools/patches/m5gfx-tab5-pageflip.patch` exposes
the M5GFX framebuffers and configures the driver for two buffers. The native
build entry point applies it idempotently after `idf.py reconfigure`, which
may fetch or replace managed components. From `apps/orcsdr-tab5`, use:

```powershell
.\tools\build-tab5-idf.ps1
```

Do not make a one-off change inside ignored `managed_components`. If the
pinned M5GFX component changes, update the tracked patch first, then build and
exercise a full-repaint visualizer view with live radio audio.

## Current Hosted pair

The P4 application pins `espressif/esp_hosted` to **3.0.6** in
`apps/orcsdr-tab5/main/idf_component.yml`. Commit the generated
`apps/orcsdr-tab5/dependencies.lock` with every dependency change.

The Tab5 C6 must run the matching **3.0.6** ESP-Hosted firmware. OrcSDR's
M5Burner package embeds a matching C6 image. With reachable Hosted transport
and an eligible older C6, Firmware & Updates offers an explicitly confirmed
in-app update. Source builds need `-C6Firmware` to embed that image; the
default helper invocation does not supply it. An unreachable C6 needs the
[documented recovery path](M5BURNER_RELEASE.md), not repeated in-app retries.
Never downgrade the C6 to accommodate an old P4 host library.

At boot the P4 logs both versions. Wi-Fi is blocked unless it reports:

```text
I OrcSDR: ESP32-C6 detected
I OrcSDR: ESP-Hosted C6 FW: 3.0.6
I OrcSDR: ESP-Hosted transport: SDIO
```

## Internal DMA budget

The P4 on-chip DMA heap is shared and small. Pin the split in
`apps/orcsdr-tab5/sdkconfig.defaults` and do not let either path steal the
other's room:

| Consumer | Where it lives | Why |
|---|---|---|
| ESP-Hosted SDIO (C6 Wi-Fi) | Reserved internal DMA (~16 KiB in flight) | SDIO mempool asserts if this heap is gone |
| I2S speaker | Reserved internal DMA (~8 KiB, 4 × 512 stereo) | `Speaker.begin()` fails without it. `spk_task` stack is patched to ≥8 KiB so stereo mixing cannot overflow. |
| RTL-SDR USB URBs | PSRAM (`CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM`) | 3 × 32 KiB would exhaust on-chip RAM |
| Hosted worker tasks | PSRAM | stacks must not eat the DMA reserve |
| malloc ≤ 8 KiB | prefer internal | `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=8192` |
| malloc > 8 KiB | PSRAM first | keeps the DMA reserve for Hosted + I2S |

`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=40960` is a dedicated pool for
DMA/internal-must allocations. It is not used by ordinary `malloc()`.
Start Hosted during splash, before the USB host, so Wi-Fi claims its
slice first. Do not raise `ALWAYSINTERNAL` back to 32 KiB — that dumps
radio and UI buffers back onto the same heap Hosted needs.

Boot logs `RTL_DRAM_BUDGET` at `boot`, `after_wifi`, `after_usb`,
`after_speaker`, and `ready`. After Wi-Fi init, `dma_largest` should stay
above ~20 KiB so I2S can still start. After speaker start, leftover
contiguous DMA of ~12 KiB is enough for Hosted on-demand SDIO. A `WARN`
line means the reserved DMA room is gone; do not add more internal-DMA
consumers until it recovers.

After STA is up, do not start httpd or mDNS from internal RAM. Those
stacks live in PSRAM (`httpd.task_caps` and
`CONFIG_MDNS_TASK_CREATE_FROM_SPIRAM`). Hosted SDIO still needs a
~1.5 KiB DMA block per in-flight packet; eating that heap after
connect panics `sdio_write_task` / `sdio_push_data_to_queue`.

Healthy boot (2026-08-17, Hosted 2.12.6, mempool off):

| Stage | dma_free | dma_largest |
|---|---|---|
| boot | ~113 KiB | ~68 KiB |
| after_wifi | ~57 KiB | ~38 KiB |
| after_usb | ~33 KiB | ~23 KiB |
| after_speaker | ~20 KiB | ~19 KiB |

## Installer and release package

Use the current [M5Burner release and recovery instructions](M5BURNER_RELEASE.md)
and [hardware acceptance gate](M5BURNER_HARDWARE_GATE.md). The current beta package
and installer target Hosted 3.0.6; the historical 2.12.6 measurements above do not
establish acceptance for that pair.

## Native build and release gate

```powershell
Set-Location F:\Ai\OrcSDR\apps\orcsdr-tab5
.\tools\build-tab5-idf.ps1
```

After an explicitly authorized P4 flash, use the serial release test to prove
the version gate, scan, and saved-profile connection without exposing the
saved password:

```powershell
.\tools\test-tab5-wifi-release.ps1 -Port COM17
```

The hardware release record must contain the native build result, the matching
boot line, scan completion, and connect completion while the normal radio path
is present.

When the release contains a visual or display change, it must also record a
live audio/DSP check while cycling the changed view. Full-repaint views must
remain tear-free; incremental waterfall/audio-spectrogram views must remain
fluid without speaker stutter.
