# Weather dashboard

Weather is an **offline-first** five-tab dashboard. Opening it is safe while another receiver mode is active: merely entering Weather does **not** retune, stop, or take ownership of the RTL-SDR.

The five tabs are:

| Tab | Foundation behavior |
|---|---|
| **NOW** | Shows available observations honestly, RF sample age, storage/map/receiver state, and alert boundaries. Missing observations stay missing rather than becoming zero. |
| **FORECAST** | Shows offline/cached outlook state and the stored online-enrichment policy. Foundation does not include an Internet weather provider. |
| **MAP** | Reuses OrcMaps and the canonical receiver location. With an installed offline map pack it renders local context; no live radar layer is claimed in Foundation. |
| **RF WEATHER** | Provides explicit NOAA Weather Radio **LISTEN**, **SCAN 7 CH**, and **STOP RF** actions. Weather Hunter, personal sensors, radiosondes, and satellite weather are later phases. |
| **REPORTS** | Saves bounded Weather snapshots to SD after Weather RF is stopped and lists recent report history. |

## NOAA Weather Radio

The U.S. Foundation channel plan is exactly `162.400`, `162.425`, `162.450`, `162.475`, `162.500`, `162.525`, and `162.550 MHz`.

**LISTEN** is foreground reception. It intentionally gives the existing WX/NFM receiver path to Weather and remains on the selected channel until stopped or the user leaves the RF activity.

**SCAN 7 CH** visits those seven centers sequentially, records the receiver's relative dBFS observation for each, keeps the strongest result, and then stops Weather RF. The result is a discovery observation, not proof that a particular transmitter identity or alert was decoded. Press **LISTEN** to remain on the strongest channel afterward.

OrcSDR does not hard-lock gain for Weather. Receiver profiles and the existing radio path continue to control supported gain behavior.

## Sources and freshness

Weather distinguishes the source of information instead of presenting every value as if it were live:

- **RF** — directly sampled by the receiver.
- **LOCAL** — local device/sensor state.
- **ESTIMATE** — explicitly derived locally.
- **CACHE** — stored data from an earlier observation/provider operation.
- **ONLINE** — optional network enrichment.

RF observations retain an age. A stale cached sample remains a stale cached sample; the dashboard does not describe it as continuous monitoring.

## Offline maps and NOAA data pack

Weather uses the same receiver location, OrcMaps pack, and signed `noaa_weather` catalog state used elsewhere in OrcSDR. It does not create a second location or download system. Data & Maps remains the place to install or update signed packs.

## Internet policy

The stored policy has three values: **Disabled**, **Manual**, and **Automatic**. The default is **Disabled**.

Foundation only provides this policy plumbing. It does **not** implement an Internet forecast/radar provider, and cycling the policy does not make a network request. Direct RF, local map context, and SD reports remain usable without an Internet weather service.

## Reports

**SAVE SNAPSHOT** writes under `/orcsdr/weather/` and includes structured JSON, CSV, a self-contained HTML summary, RF event data, alert metadata, and a SHA-256 manifest. Large serialization buffers are allocated in PSRAM. History replacement uses staged `.part`/backup semantics.

Report save is refused while Weather RF is active so SD work does not contend with the receive/audio path. Stop Weather RF first. Raw IQ is not included by default.

## Current limitations

- NOAA SAME alert decoding is **not implemented** in Foundation. The current codebase had no existing SAME decoder to reuse, so the dashboard does not claim one.
- Weather Hunter, personal weather-station protocol decoding, radiosonde / Balloon Hunter, satellite pass prediction, and LRPT imagery are later phases.
- No Internet weather provider or live radar provider is implemented in Foundation.
- Physical Tab5 touch/audio/RF acceptance is separate from host/build evidence and must be recorded before those labels are used.
