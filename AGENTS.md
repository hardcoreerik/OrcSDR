# OrcSDR development requirements

Before firmware development, builds/flashes, or hardware debugging, read
[docs/DEVELOPMENT_DIAGNOSTICS.md](docs/DEVELOPMENT_DIAGNOSTICS.md).
Apply it to every board profile: retain exact build identity, monitor without
resetting, record health before/after a hardware test, and preserve crash evidence
before replacing firmware. Keep logs, dumps and credentials private. Distinguish
host checks, firmware builds, flashing, serial evidence and physical acceptance.

Preserve existing work and protected branches. Scope changes and tests narrowly;
never merge, publish, rewrite history or erase device data without authorization.
