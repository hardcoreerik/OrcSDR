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
