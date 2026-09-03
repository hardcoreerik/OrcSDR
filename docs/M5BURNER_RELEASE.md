# OrcSDR M5Burner release

`v0.2.0-beta.1` is one normal Tab5 package. It contains the P4 application
and a pinned ESP-Hosted 3.0.6 C6 image. M5Burner writes the P4 only; OrcSDR
offers a C6 update from Settings after it proves that the existing Hosted link
is reachable.

## Required order

1. Install **OrcSDR** through M5Burner without erase.
2. Open **Settings → Firmware & Updates**.
3. If the reachable C6 differs from 3.0.6, explicitly confirm **UPDATE C6 TO 3.0.6**.
4. After restart, verify `host=3.0.6 coprocessor=3.0.6 match=1`.

The normal flow preserves P4 NVS, saved Wi-Fi profiles, and preferences. A C6
that cannot establish Hosted transport is a manual recovery case; the separate
Bridge builder is retained only for support recovery and is not published.

## Build and inspect

From a clean checkout at the exact release tag:

```powershell
.\tools\release\build-m5burner.ps1
.\tools\release\test-m5burner-bundle.ps1 -BundlePath .\dist\OrcSDR-Tab5-<tag> -Version <tag>
```

The build pins the Espressif ESP-Hosted 3.0.6 source revision, emits C6
provenance and SHA-256, and produces the one M5Burner upload bundle plus a
local test zip. It never flashes hardware or uploads a listing.

## Publication record and future-release gate

`v0.2.0-beta.1` is publicly available through M5Burner and as a [GitHub
prerelease](https://github.com/hardcoreerik/OrcSDR/releases/tag/v0.2.0-beta.1).
For a future release, use **USER CUSTOM → Publish** privately first and test
with its Share Code. Only after the exact-tag hardware gate in
[M5BURNER_HARDWARE_GATE.md](M5BURNER_HARDWARE_GATE.md) passes may its GitHub
prerelease be created and its listing made public. M5Burner publishing details
are in the [official guide](https://docs.m5stack.com/en/uiflow/m5burner/publish).
