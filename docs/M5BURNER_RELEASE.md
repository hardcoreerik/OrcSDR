# OrcSDR M5Burner release

`v0.2.0-beta.1` is a two-package Tab5 release. M5Burner writes the P4 only;
the first temporary P4 package updates the internal C6 over the existing
ESP-Hosted SDIO link.

## Required order

1. Install **OrcSDR Hosted 3.0.6 Bridge** privately through M5Burner.
2. Wait for its serial proof: `C6 after OTA: 3.0.6`.
3. Install the final **OrcSDR** package through M5Burner.

Do not erase for either step. The normal flow preserves P4 NVS, saved Wi-Fi
profiles, and preferences. If the bridge cannot establish Hosted transport, it
stops with `C6_BRIDGE_FAIL`; that C6 is a manual recovery case.

## Build and inspect

From a clean checkout at the exact release tag:

```powershell
.\tools\release\build-m5burner.ps1
.\tools\release\test-m5burner-bundle.ps1 -BundlePath .\dist\OrcSDR-Tab5-<tag> -Version <tag>
.\tools\release\test-m5burner-bundle.ps1 -BundlePath .\dist\OrcSDR-Tab5-<tag>\Hosted-Bridge -Version <tag> -Bridge
```

The build pins the Espressif ESP-Hosted 3.0.6 source revision, emits C6
provenance and SHA-256, and produces both M5Burner upload bundles plus local
test zips. It never flashes hardware or uploads a listing.

## Publication gate

Use **USER CUSTOM → Publish** for both packages, keep both listings private,
and test using their Share Codes. Only after the exact-tag hardware gate in
[M5BURNER_HARDWARE_GATE.md](M5BURNER_HARDWARE_GATE.md) passes may the GitHub
prerelease be created and the two listings made public. M5Burner publishing
details are in the [official guide](https://docs.m5stack.com/en/uiflow/m5burner/publish).
