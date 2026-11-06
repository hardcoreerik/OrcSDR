# `esp_rtl_sdr` integration contract

The public driver API is maintained in
[`hardcoreerik/esp-rtl-sdr`](https://github.com/hardcoreerik/esp-rtl-sdr).
Current OrcSDR uses the driver's published release **`v0.9.5`** (it reports `0.9.5`) at immutable
commit `4bf017195502d1a413e155d5a79aee7cd086035b`. The same SHA is recorded in
`apps/orcsdr-tab5/main/idf_component.yml` and
`apps/orcsdr-tab5/dependencies.lock`.

## Receiver support and evidence

**Fully supported in OrcSDR:** RTL-SDR Blog V4, V3c, V4 Lite (V4L), and Nooelec NESDR SMArt V5. These are accepted receiver models, not experimental dongle integrations; individual dashboard/RF calibration evidence remains scoped to specific tests.

| Receiver | OrcSDR status / scope |
|---|---|
| RTL-SDR Blog V4 | **Fully supported**; historical RF-tested baseline with HF upconverter support. |
| RTL-SDR Blog V3c | **Fully supported**; hardware-tested lifecycle, streaming, control and FM paths; direct-sampling HF has model-specific control behavior. |
| RTL-SDR Blog V4 Lite (V4L) | **Fully supported**; tested lifecycle, streaming and tuner controls, with accepted [40 m FT8 evidence](user-guide/dashboards/ft8.md). |
| Nooelec NESDR SMArt V5 | **Fully supported**; tested streaming/controls and accepted [40 m FT8 evidence](user-guide/dashboards/ft8.md). |
| Earlier Blog V3 variants | Experimental until individually verified; V3c acceptance does not automatically qualify them. |
| Nooelec SMArTee XTR / E4000 | **Supported within tuner range.** Dashboards show a warning for out-of-range frequencies. Home shows the reason in the waterfall and resumes reception after a supported retune. |
| Other receivers | Unsupported / not verified without a named accepted profile. |

OrcSDR deliberately selects callback delivery, three 32-KiB USB transfers, and
USB task core 0. Borrowed IQ event data is consumed or copied during the
callback and is never retained.

Do not add a local driver copy or patch generated `managed_components`. Driver
fixes belong upstream, followed by adoption of a new immutable pin here.

Streaming gain, AUTO, RTL AGC, and bias setters are asynchronous requests.
`ESP_OK` means accepted; getters expose software shadow state. Hardware
register latching requires bus-level evidence. Historical documents may name
older driver versions only as dated Historical Evidence.
