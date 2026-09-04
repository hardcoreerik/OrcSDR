# M5Burner hardware acceptance gate

This is the release gate for every public OrcSDR beta. Build or package
validation alone does not authorize a GitHub release or public listing.

1. Build the bundle and record its `M5BURNER_BUNDLE_OK` line, source commit,
   C6 provenance, and hashes.
2. Privately publish and install **OrcSDR** through its Share Code without
   erase. On a current pair, confirm Firmware & Updates reports `current` and
   offers no update.
3. When the C6 image, update code, or delivery path changes, use an authorized
   recovery-capable device with a reachable older C6. Confirm the UI update,
   capture `RTL_WIFI_C6_UPDATE` serial progress, and after restart verify
   `host=3.0.6 coprocessor=3.0.6 match=1`. If those inputs are unchanged, record
   the prior hardware evidence and verify the exact package contains the same
   C6 source revision and SHA-256 instead of forcing another downgrade.
4. Confirm Home, FM audio, RTL-SDR, Wi-Fi scan, saved-profile connection, RF24,
   and the UI regression command. The dedicated `-C6Update` regression mode
   exercises the same authenticated Settings action.
5. Run `install-orcsdr.ps1` against a matching pair. Its `-UpdateC6` bridge
   route remains a recovery-only path and must preserve NVS and stop with
   recovery guidance when Hosted cannot form a link.

Attach photos/screenshots and serial evidence to the tag record. Only then
create the GitHub prerelease with the verified artifacts and change its
M5Burner listing from private to public.
