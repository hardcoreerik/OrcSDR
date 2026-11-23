# UI-DV0 — Digital Voice scanner-first prototype

This is a **preproduction, unregistered** M5GFX C++ dashboard shell committed for review, not a production Digital Voice receiver.

- Six shared views: SCAN, LIVE, LAST HEARD, SYSTEMS, SIGNAL, SETUP; one receiver, protocol-specific fact fields.
- Discovery-first and analysis-first density modes; source-qualified replay/demo data.
- Receive-only, no Pluto/TX and no alternative RTL-SDR owner.
- The scanner is **not connected** to an active Digital Voice receiver/scan adapter yet; UI scan actions are capability-gated.
- Existing P25 native receive/follow remains independent, not replaced.
- The current host test uses a command-recording graphics stand-in and cannot verify pixels.

See [Scanner-first product/UI workflow](SCANNER_FIRST_WORKFLOW.md), [Design options](DESIGN_OPTIONS.md), and [Manifest](SCREENSHOT_MANIFEST.json). Pixel-verified device screenshots are still pending. Six generated image concepts in the originating review are not M5GFX framebuffer proof and are not included as approval artifacts.
