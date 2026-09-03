# M5Burner hardware acceptance gate

This is the release gate for an exact `v0.2.0-beta.1` tag. Build or package
validation alone does not authorize a GitHub prerelease or public listing.

1. Build the bundle and record its `M5BURNER_BUNDLE_OK` line, source commit,
   C6 provenance, and hashes.
2. Privately publish and install **OrcSDR** through its Share Code without
   erase. On a current pair, confirm Firmware & Updates reports `current` and
   offers no update.
3. On an authorized recovery-capable device with a reachable older C6, confirm
   the UI update, capture `RTL_WIFI_C6_UPDATE` serial progress, and after its
   restart verify `host=3.0.6 coprocessor=3.0.6 match=1`.
4. Confirm Home, FM audio, RTL-SDR, Wi-Fi scan, saved-profile connection, RF24,
   and the UI regression command. The dedicated `-C6Update` regression mode
   exercises the same authenticated Settings action.
5. Run `install-orcsdr.ps1` against a matching pair. Its `-UpdateC6` bridge
   route remains a recovery-only path and must preserve NVS and stop with
   recovery guidance when Hosted cannot form a link.

Attach photos/screenshots and serial evidence to the tag record. Only then
attach the verified artifacts to the GitHub prerelease and change the M5Burner
listing from private to public.
