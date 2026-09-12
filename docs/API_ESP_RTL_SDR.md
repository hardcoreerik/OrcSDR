# `esp_rtl_sdr` integration contract

The public driver API is maintained in
[`hardcoreerik/esp-rtl-sdr`](https://github.com/hardcoreerik/esp-rtl-sdr).
Current OrcSDR uses driver **0.8.0-rc2** at immutable commit
`b175dfea6782faa97e512d4a2408767c75977527`. The same SHA is recorded in
`apps/orcsdr-tab5/main/idf_component.yml` and
`apps/orcsdr-tab5/dependencies.lock`.

## Receiver evidence

| Receiver | Current boundary |
|---|---|
| RTL-SDR Blog V4 | RF-Verified tested baseline. |
| RTL-SDR Blog V3C | RF-Verified/Experimental on one RC4 unit for detection, startup, streaming, FM/RDS, retune, hotplug, and USB/battery boot; gain and sensitivity remain provisional. |
| Earlier Blog V3 variants | Implemented/Experimental; V3C evidence does not establish broad compatibility. |
| Nooelec NESDR SMArt V5 | Implemented/Experimental; detection/streaming provisional and repeatable RF Not Verified. |
| Blog V4L | Not Verified; no explicit acceptance evidence. |
| Other receivers | Unsupported/Not Verified unless a named profile and evidence are added. |

OrcSDR deliberately selects callback delivery, three 32-KiB USB transfers, and
USB task core 0. Borrowed IQ event data is consumed or copied during the
callback and is never retained.

Do not add a local driver copy or patch generated `managed_components`. Driver
fixes belong upstream, followed by adoption of a new immutable pin here.

Streaming gain, AUTO, RTL AGC, and bias setters are asynchronous requests.
`ESP_OK` means accepted; getters expose software shadow state. Hardware
register latching requires bus-level evidence. Historical documents may name
older driver versions only as dated Historical Evidence.
