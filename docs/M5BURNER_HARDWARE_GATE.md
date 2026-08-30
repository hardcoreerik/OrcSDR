# M5Burner hardware acceptance gate

This is the proof process for a release built by
`tools/release/build-m5burner.ps1`. It does **not** authorize a public listing
until every required observation is recorded against the exact release tag and
SHA-256.

## 1. Offline package gate

From the clean, tagged release worktree:

```powershell
.\tools\release\build-m5burner.ps1
.\tools\release\test-m5burner-bundle.ps1 -BundlePath .\dist\OrcSDR-Tab5-<tag> -Version <tag>
```

Record the printed `M5BURNER_BUNDLE_OK` line, tag, commit, and SHA-256. A pass
proves package consistency only; it does not prove a Tab5 can install or run
the image.

## 2. Private M5Burner install

1. Upload the exact verified `.bin` and `OrcSDR-Main.png` through **USER
   CUSTOM → Publish** with device type **M5Stack Tab5**.
2. Keep the listing private and install it with its Share Code on the release
   Tab5. Do not erase for a normal upgrade.
3. Record the M5Burner listing version, the file hash, and a screenshot of the
   completed installation.

Stop if M5Burner rejects the image, selects another device type, or reports a
write failure. Do not substitute another binary or relabel a prior tag.

## 3. First-boot gate

After the M5Burner install, confirm on the Tab5:

- OrcSDR reaches Home without a boot loop or panic.
- System/About reports the expected release version.
- Connectivity shows the expected P4/C6 Hosted state. If the C6 is mismatched,
  Wi-Fi must remain disabled and the setup guidance must be shown; update the
  C6 only through its documented M5Burner path.
- A known FM station produces live spectrum and audio with the RTL-SDR Blog V4.
- RF Lab opens, reports a live source, and saves one short measurement session
  to SD when an SD card is present.

Record a photo or screenshot for Home, Connectivity, FM/RF Health, and RF Lab,
plus any relevant serial output. These observations are hardware evidence; do
not replace them with a successful build log.

## 4. Publication decision

Only after steps 1–3 pass:

1. Attach the same `.bin`, `SHA256SUMS.txt`, and `OrcSDR-Main.png` to the
   GitHub prerelease.
2. Change the M5Burner listing from private to public.
3. Publish the M5Burner Share Code and GitHub release URL together.

## Launcher-safe gate (later)

Launcher testing starts only after the M5Burner gate passes. It must prove an
install through Launcher, a successful OrcSDR boot, and a safe return to
Launcher on a recovery-safe Tab5. Do not claim Launcher support before those
three observations exist.
