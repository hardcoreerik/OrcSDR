# OrcSDR Linux portability boundary (proposed)

**Goal:** Add Cardputer Zero as the first experimental Linux target without replacing or destabilizing ESP32-P4 / Tab5 firmware. Eventually share radio-domain DSP, decoders, session policy and snapshots across host systems.

    OrcSDR shared radio-domain core (future extraction)
             |                           |
       ESP-IDF adapter               Linux adapter
     FreeRTOS/esp-rtl-sdr          POSIX/librtlsdr
            M5GFX                  LVGL/evdev/ALSA
             |                           |
         Tab5 / P4                 Cardputer Zero

## Receiver contract (proposal, NOT yet implemented)

A platform-neutral Receiver should expose enumerate, open, capabilities, configure, start(callback), stop, close, hotplug and error telemetry. IQ blocks need explicit center frequency, sample rate, sample format, monotonic timestamp, drop count and buffer ownership. Keep callbacks bounded and preserve OrcSDR's radio-session owner and generation semantics.

The Linux prototype intentionally uses one bounded synchronous read before moving to asynchronous continuous streaming, ring buffers and backpressure tests. It does not reuse the ESP-IDF-specific USB code or pretend that Linux and microcontroller drivers are interchangeable.

UI data should be independent snapshots. Cardputer Zero uses a purpose-built 320x170 LVGL keyboard layout; Tab5 retains M5GFX. Decoders must not call display APIs. Future Pluto/AD9361 Linux transports should remain independently maintained instead of expanding the RTL-SDR-specific USB driver.

## Gates

1. Prove native Linux enumeration, tuning and sustained loss-accounted IQ on the physical handheld.
2. Port one host-testable OrcSDR decoder, compare against saved fixtures and preserve numerical/policy behavior.
3. Build a small LVGL desktop simulator screen and verify actual physical keyboard/display before claiming UI support.
4. Add audio, SD/archive, settings, safe shutdown, recovery and ARM64 packaging.
5. Once the separation is proven, consider generic Raspberry Pi / Rockchip / x86 Linux targets without forking decoders.

**Current status:** only an independent Linux receiver diagnostic, not a shared-core extraction or a working OrcSDR dashboard. Keep this branch separate until hardware evidence is recorded.
