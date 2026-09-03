# M5Burner hardware acceptance gate

This is the release gate for an exact `v0.2.0-beta.1` tag. Build or package
validation alone does not authorize a GitHub prerelease or public listing.

1. Build both bundles and record their `M5BURNER_BUNDLE_OK` lines, source
   commit, C6 provenance, and hashes.
2. Privately publish **OrcSDR Hosted 3.0.6 Bridge**. Install it through its
   Share Code without erase. Record `C6 before OTA`, then after its restart
   `C6 after OTA: 3.0.6`. Verify `host=3.0.6 coprocessor=3.0.6 match=1`.
3. Privately publish and install final **OrcSDR**, also without erase. Confirm
   Home, FM audio, RTL-SDR, Wi-Fi scan, saved-profile connection, RF24, and
   the UI regression command.
4. Run `install-orcsdr.ps1` first against the matching pair, then its explicit
   `-UpdateC6` route on an authorized recovery-safe device. It must preserve
   NVS and stop with recovery guidance when Hosted cannot form a link.

Attach photos/screenshots and serial evidence to the tag record. Only then
attach the verified artifacts to the GitHub prerelease and change both
M5Burner listings from private to public.
