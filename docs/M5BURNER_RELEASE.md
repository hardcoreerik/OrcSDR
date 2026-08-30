# OrcSDR M5Burner release

![OrcSDR on a M5Stack Tab5 with RTL-SDR Blog V4](images/OrcSDR-Main.png)

M5Burner publishes the Tab5 **P4** application only. It does not make the
Tab5 a single-chip device: the on-board C6 must already run ESP-Hosted 3.0.6.
Never publish a P4 build that has not passed the Tab5 hardware release gate.

## Build the upload bundle

From a clean checkout at the release tag:

```powershell
.\tools\release\build-m5burner.ps1
```

For a private M5Burner trial before merge, pass a distinct candidate label such
as `-Version v0.2.0-alpha.7-candidate.1`. Upload it privately, use Share Code
for the test device, and never reuse that candidate label as a public release.

The command runs the native ESP-IDF build, merges the configured P4 flash
regions, and writes `dist/OrcSDR-Tab5-<tag>/`. It creates the P4 image,
`SHA256SUMS.txt`, an upload-field manifest, and the cover image. It does not
flash hardware, include C6 firmware, or upload anything.

Verify the hash before attaching the image to a GitHub prerelease or uploading
it to M5Burner:

```powershell
.\tools\release\test-m5burner-bundle.ps1 -BundlePath .\dist\OrcSDR-Tab5-<tag> -Version <tag>
```

Then complete the physical install and first-boot gate in
[`M5BURNER_HARDWARE_GATE.md`](M5BURNER_HARDWARE_GATE.md). A package pass is
not a hardware pass.

## Publish in M5Burner

Sign in, then open **USER CUSTOM → Publish**. Upload the generated P4 `.bin`
and cover. Use `m5burner-upload.json` as the source for the listing fields.
Set the listing public only after the GitHub prerelease has the same image and
SHA-256. Share Code is useful for alpha testers before public discovery.

Recommended listing:

| Field | Value |
| --- | --- |
| Name | OrcSDR |
| Version | release tag without `v` |
| Device | M5Stack Tab5 |
| GitHub | https://github.com/hardcoreerik/OrcSDR |
| Firmware | generated P4 merged image |
| Cover | `OrcSDR-Main.png` |

## Upgrade policy

For normal OrcSDR upgrades, **do not erase**: NVS holds user preferences and
saved Wi-Fi profiles. Erase is only a troubleshooting recovery step.

On first boot, OrcSDR compares the P4 host and C6 slave versions. A mismatch
opens the Connectivity page with the required C6 action. Radio reception stays
available; Wi-Fi is intentionally disabled until the C6 is updated manually
through M5Burner to ESP-Hosted 3.0.6.

Do not attempt P4-initiated C6 updating in this release. It needs a separate,
hardware-proven recovery design before it can replace M5Burner.
