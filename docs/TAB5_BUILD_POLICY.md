# Tab5 build and ESP-Hosted policy

OrcSDR's Tab5 firmware is a native ESP-IDF project. Build and flash the P4
with Espressif `idf.py`; do not use PlatformIO for development, release, or
hardware validation.

Arduino, M5Unified, and M5GFX are ESP-IDF components in the dependency graph,
not an Arduino-IDE or PlatformIO toolchain.

## Exact Hosted pair

The P4 application pins `espressif/esp_hosted` to **2.12.6** in
`apps/orcsdr-tab5/main/idf_component.yml`. Commit the generated
`apps/orcsdr-tab5/dependencies.lock` with every dependency change.

The native build helper validates and applies the one Tab5 integration patch
required by the upstream 2.12.6 component: Arduino owns Hosted initialization
so `WiFi.setPins()` runs before `esp_hosted_init()`. The helper refuses an
unexpected component source instead of silently patching another version.

The Tab5 C6 slave must be installed separately with the matching **2.12.6**
M5Burner ESP-Hosted package. OrcSDR's P4 image does not update the C6.
Never downgrade the C6 to accommodate an old P4 host library.

At boot the P4 logs both versions. Wi-Fi is blocked unless it reports:

```text
RTL_WIFI_HOSTED host=2.12.6 slave=2.12.6 match=1
```

## Internal DMA budget

The P4 on-chip DMA heap is shared and small. Pin the split in
`apps/orcsdr-tab5/sdkconfig.defaults` and do not let either path steal the
other's room:

| Consumer | Where it lives | Why |
|---|---|---|
| ESP-Hosted SDIO (C6 Wi-Fi) | Reserved internal DMA (~16 KiB in flight) | SDIO mempool asserts if this heap is gone |
| I2S speaker | Reserved internal DMA (~8 KiB, 4 × 512 stereo) | `Speaker.begin()` fails without it |
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

Healthy boot (2026-08-17, Hosted 2.12.6, mempool off):

| Stage | dma_free | dma_largest |
|---|---|---|
| boot | ~113 KiB | ~68 KiB |
| after_wifi | ~57 KiB | ~38 KiB |
| after_usb | ~33 KiB | ~23 KiB |
| after_speaker | ~20 KiB | ~19 KiB |

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
