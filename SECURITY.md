# OrcSDR Security Policy

OrcSDR is a receive-only, local-first SDR application for the M5Stack Tab5
(ESP32-P4) and RTL-SDR Blog V4. It does not provide a public cloud service.
An optional LAN web console can be enabled in Settings → Companion; it is
off by default and bindable only while Wi-Fi is connected. It serves the Home
visual plus local tune/volume/dashboard actions. It does not expose passwords,
pairing material, or coordinates. There is no TLS in this slice.

## Supported code

Security fixes are developed on `main`. Reports should reproduce against the
latest commit on `main` or the latest published release. Older commits and
locally modified firmware may receive guidance, but are not maintained as
separate security branches.

## Report privately

Use [GitHub Security Advisories](https://github.com/hardcoreerik/OrcSDR/security/advisories/new)
for an unpatched vulnerability. Do not open a public issue until a fix or
coordinated disclosure has been agreed.

Include:

- the affected commit or release and whether the target is a Tab5;
- connected hardware, especially an RTL-SDR, USB storage, or a serial host;
- exact reproduction steps, expected impact, and a minimal proof of concept;
- relevant sanitized serial output, crash logs, or files hashes.

Never include Wi-Fi passwords, Companion pairing material, device fingerprints,
private receiver coordinates, recordings, IQ captures, or real aircraft/location
data unless specifically requested through the private report.

We will acknowledge the report, validate the impact, and coordinate a fix and
disclosure schedule with the reporter.

## In scope

- Tab5 firmware under `apps/orcsdr-tab5`, including Settings, Wi-Fi profile
  handling, receiver-location storage, serial commands, and SD-card transfer.
- OrcSDR's version-pinned `esp_rtl_sdr` integration, including configuration,
  callback handling, and radio/UI actions. Driver defects belong in the
  [`esp-rtl-sdr`](https://github.com/hardcoreerik/esp-rtl-sdr) repository.
- Repository scripts that build, flash, transfer, validate, or install OrcSDR
  artifacts.
- OrcSDR-managed SD data: database, metadata, map-pack, recording, capture,
  log, manifest, and update artifacts.
- Future authenticated OrcSDR Companion interfaces when their implementation is
  present in this repository.

Examples include arbitrary code execution through USB or SD input, unintended
disclosure of saved network/location data, credential exposure in diagnostics,
unsafe update or file-replacement behavior, authentication bypass, and denial
of service that persists across reboot.

## Out of scope

- RF reception of unencrypted broadcasts, including ADS-B, RDS, LoRa, and
  public radio traffic. Their contents are not authenticated by OrcSDR.
- Security of the RF protocol, attached third-party dongles, an untrusted power
  source, or physical possession of an unlocked device.
- Vulnerabilities solely in upstream ESP-IDF, Arduino, M5Unified, M5GFX,
  Espressif ESP-Hosted, or the operating system. Report them upstream as well;
  include OrcSDR-specific impact in the private report.

## Local-data and update boundaries

Treat the Tab5 and its microSD card as sensitive local storage. Saved Wi-Fi
profiles, exact receiver coordinates, diagnostics, recordings, and captures
must not be committed, attached to public issues, or placed in sample assets.

Database, map-pack, and future update artifacts must be versioned, size-bounded,
integrity-checked, and installed through a staged replacement path that leaves a
known-good prior file available until the replacement opens successfully.
