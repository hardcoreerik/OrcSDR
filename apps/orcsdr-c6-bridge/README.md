# C6 3.0.6 migration bridge

This is a one-time **bootstrap** P4 application used only to update a Tab5 C6
that still runs ESP-Hosted 2.12.6. Its host dependency is intentionally pinned
to 2.12.6 so it can connect to that old C6 and transfer the embedded 3.0.6 C6
image.

It is not the OrcSDR production application and must not be flashed after the
C6 reaches 3.0.6. The permanent P4 application in `apps/orcsdr-tab5` pins
ESP-Hosted 3.0.6, M5Unified 0.2.20, M5GFX 0.2.27, and resolves ESP-IDF 5.5.4.
