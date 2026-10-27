# Repeatable M5Burner packaging for OrcSDR Maps releases

Use this alongside `docs/M5BURNER_RELEASE.md`. This procedure never flashes a
device or publishes an M5Burner listing. Publication is a separate action.

## Inputs and boundaries

- Clean firmware checkout at the exact approved release tag, ESP-IDF 5.5.4 and
  its Python environment. Record tag's peeled commit, not just the tag name.
- Pinned C6 source/provenance from `tools/release/hosted-c6-release.json`.
  Embedding this image is not authorization to flash the coprocessor.
- Reviewed Natural Earth world PMTiles archive, SHA-256
  `9aea08772bacf1f024d1da90cc52aa8fcf0b0e37405415dc7c91e75e56596f0f`.
  It is a build input, not a map silently taken from a user's SD card.
- Production catalog URL and public key from the tagged build script. Never
  copy a private signing key into a package.
- Release notes in `docs/releases/<tag>.md` and existing product cover image.

The full contiguous `.bin` is installed at address **0**. Blank bytes covering
NVS reset saved settings and launch setup; the SD card is untouched. The local
ZIP writes only bootloader 0x2000, table 0x8000, app 0x10000 and world 0x650000,
so it preserves NVS. These two installation paths are intentionally different.
Neither path automatically writes C6. A separate optional in-app update handles it.

## Build and finalize

Run from the exact-tag checkout in PowerShell. Replace the sample paths/version.
Do not delete another session's temporary source checkout to fix a build failure.

```powershell
$env:ESP_IDF_VERSION = '5.5'
$env:TEMP = 'F:\AI\OrcSDR-TEMP\release-temp-v0.5.0-beta.1'
New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
powershell -NoProfile -ExecutionPolicy Bypass -File tools/release/build-m5burner.ps1 -Version v0.5.0-beta.1
```

The old builder clears `ORCSDR_WORLD_MAP`, so its output alone is incomplete for
a fresh Maps install. After it succeeds, use the IDF environment to stamp the
release identity and world input, retaining the explicit saved SDKCONFIG and
the **newly generated** release C6 image. Do not use a stale build-cache image:

```powershell
. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
Set-Location apps/orcsdr-tab5
idf.py -B build-native-hosted3 `
  -D SDKCONFIG=build-native-hosted3/sdkconfig `
  -D 'ORCSDR_BUILD_ID=v0.5.0-beta.1 b714a127 2026-10-26' `
  -D 'C6_FIRMWARE_BIN=F:/AI/OrcSDR-TEMP/maps-integration-completion/dist/OrcSDR-Tab5-v0.5.0-beta.1/c6/esp_hosted_tab5_c6.bin' `
  -D 'ORCSDR_WORLD_MAP=F:/AI/OrcSDR-TEMP/maps-integration-completion/.local/maps/world/world-overview-z4.pmtiles' build
Set-Location ../..
python tools/release/finalize-m5burner-maps.py `
  --repo . --version v0.5.0-beta.1 `
  --world .local/maps/world/world-overview-z4.pmtiles
powershell -NoProfile -ExecutionPolicy Bypass -File tools/release/test-m5burner-bundle.ps1 `
  -BundlePath dist/OrcSDR-Tab5-v0.5.0-beta.1 -Version v0.5.0-beta.1
```

The finalizer is independent of the source revision being packaged; for a tag
predating the helper, invoke a reviewed helper by absolute path with `--repo`
pointing at the clean tagged firmware checkout. Never move a published tag to
add packaging documentation. Rerunning the old builder regenerates its ZIP;
finalize once afterward. A duplicate world region is refused.

The build runs the linked miniz collision and FT8 scratch-placement checks.
The finalizer verifies exact-tag/clean source, app version, pinned world bytes,
C6 embedding/provenance, production URL, app headroom, world partition presence,
every merged region, blank full-install NVS and ZIP contents. It emits
`package-provenance.json` and hashes of the binary, ZIP, notes and provenance.
Keep console/build logs beside their invocation, outside Git.

## Outputs and publication

`dist/OrcSDR-Tab5-<tag>/` contains:

- `OrcSDR-Tab5-<tag>.bin`: M5Burner Publish firmware, address 0.
- `OrcSDR-Tab5-<tag>-local-m5burner.zip`: local USER CUSTOM package, retains NVS.
- `m5burner-upload.json`, `OrcSDR-Main.png`, `RELEASE_NOTES.txt`, `README.txt`.
- `package-provenance.json`, `SHA256SUMS.txt`, C6 provenance and build inputs.

The worldwide regional pack (~543 MB) is **not** in the firmware; users obtain
it through the production in-app map catalog or manually install its pack.

Compare the actual hashes with the saved manifest, run the bundle checker, then
upload the exact checked assets to the existing GitHub release. Do not infer
M5Burner listing publication from GitHub upload. In M5Burner, use USER CUSTOM →
Publish with the merged binary, cover image and identical release notes. Record
the resulting listing/share code separately when publication is confirmed.

Do not ship the old three-region Windows updater as if it installs the world
map. That installer preserves settings but needs a separate reviewed world-map
installation path for an upgrade from pre-Maps firmware. It is not part of the
v0.5.0-beta.1 Maps package described here.

Packaging checks establish artifact structure/provenance, not physical acceptance
of a newly rebuilt binary. Keep prior device-tested source, exact release build,
release approval and observed installation behavior as separate claims.
