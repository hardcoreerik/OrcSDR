# Security Policy

## Supported versions

Security fixes are developed on `main`. Please reproduce issues against the
latest commit on `main` or the latest published OrcSDR release before reporting
them.

## Reporting a vulnerability

Please report suspected vulnerabilities privately through
[GitHub Security Advisories](https://github.com/hardcoreerik/OrcSDR/security/advisories/new).

Include the affected commit or release, hardware configuration, reproduction
steps, impact, and any relevant serial output. Do not include Wi-Fi passwords,
Companion pairing credentials, private location data, or recordings containing
sensitive content.

Please do not open a public issue for an unpatched vulnerability. We will
acknowledge a report, assess its impact, and coordinate a fix and disclosure
timeline with the reporter.

## Scope

This policy covers OrcSDR firmware, the Tab5 application, the RTL-SDR USB host
component, supplied scripts, and data/update handling in this repository.
Third-party hardware, upstream dependencies, and externally hosted services
should also be reported to their respective maintainers where appropriate.

## Security boundaries

OrcSDR is an actively developed receive-only radio project. Treat device
storage, saved network profiles, receiver-location settings, diagnostics, and
recordings as sensitive local data. Never commit credentials or private capture
data to this repository.
