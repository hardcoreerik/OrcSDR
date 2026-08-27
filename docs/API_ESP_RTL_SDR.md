# `esp_rtl_sdr` integration contract

The public driver API is maintained in
[`hardcoreerik/esp-rtl-sdr`](https://github.com/hardcoreerik/esp-rtl-sdr).
OrcSDR obtains the immutable `v0.7.9` release through ESP-IDF's component
manager and records the resolved commit and content hash in
`apps/orcsdr-tab5/dependencies.lock`.

OrcSDR's Tab5 integration deliberately overrides these defaults:

- `delivery_mode = ESP_RTL_SDR_DELIVERY_CALLBACK`; OrcSDR does not consume the
  synchronous read ring.
- Three 32-KiB USB transfers.
- USB task pinned to core 0.
- Borrowed IQ event data is consumed or copied during the callback and is not
  retained.

Do not add a local component override, copy the driver into this repository, or
patch generated `managed_components`. Driver defects are reported and fixed in
the driver repository, released under a new immutable tag, then adopted by
updating the manifest and lockfile.

Streaming gain, AUTO, RTL AGC, and bias setters are asynchronous requests.
`ESP_OK` means accepted, and getters expose software shadow state. Hardware
register latching requires an external USB protocol analyzer or equivalent bus
evidence. Windows USBPcap on the PC cannot observe the USB bus hosted by Tab5.
