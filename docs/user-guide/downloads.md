# Downloads and releases

For ordinary installation, open the
[OrcSDR M5Burner web flasher](https://burner.m5stack.com/share/firmware/JXFU4H)
in **Chrome or Edge**, connect the Tab5 by USB, and click **Burn to device**.
Check the version shown on the page before flashing. The M5Burner desktop
application is another option. See [Getting started](getting-started.md) for the steps.

**M5Burner installation resets saved settings**, including Wi-Fi profiles,
location, and screen rotation. The Windows settings-preserving installer from
[GitHub Releases](https://github.com/hardcoreerik/OrcSDR/releases/latest) leaves
saved settings and existing C6 firmware untouched.

Use GitHub Releases for immutable notes, assets, hashes, and tags. Verify the
published SHA-256 before trusting firmware or optional SD data packs. The
release's hardware result is exact-version evidence, not proof of later `main`
commits, every receiver, or every Tab5 revision.

Developers building the latest `main` should follow the
[build from source quickstart](build-from-source.md). It covers ESP-IDF 5.5.4,
the usual image-embedded P4 build, and application-only flashing on a compatible
existing installation. The [ESP-Hosted migration record](tab5-esp-hosted-3-migration.md)
is advanced reference material, not the normal build guide. Legacy tags retain
their original dependency-specific instructions.

The Pages site is deployed only from merged `main`. Pull requests validate the
guide but do not publish it.
