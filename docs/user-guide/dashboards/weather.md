# Weather dashboard

The Weather dashboard is OrcSDR's offline-first weather and RF-weather workspace. It uses five tabs: **NOW**, **FORECAST**, **MAP**, **RF WEATHER**, and **REPORTS**.

Opening Weather is deliberately passive: it does **not** retune or stop the receiver. That lets you inspect cached/local Weather information while another receiver mode continues. Tuner changes happen only after an explicit Weather RF action.

## NOAA Weather Radio

The RF WEATHER tab can listen to or scan the seven U.S. NOAA Weather Radio channels:

- 162.400 MHz
- 162.425 MHz
- 162.450 MHz
- 162.475 MHz
- 162.500 MHz
- 162.525 MHz
- 162.550 MHz

**LISTEN** takes foreground receiver ownership and uses OrcSDR's existing WX/NFM demodulation and audio path. **SCAN** checks those channels sequentially with the shared scan engine and retains the strongest relative signal result. It is one receiver moving between channels; the UI never represents all seven as simultaneously monitored.

Signal values are receiver-relative, not calibrated dBm. Reception depends on the RTL-SDR, antenna, location, and local transmitter coverage.

## Sources and freshness

Weather observations carry a source and age. The model distinguishes direct RF, local sensor, locally derived, cached, and online data. Missing values remain unavailable rather than being rendered as zero.

The Internet policy defaults to **DISABLED**. Foundation does not require an Internet connection to open or use the Weather dashboard. The existing signed `noaa_weather` catalog, receiver location, OrcMaps/offline map, SD storage, and local time services are reused rather than duplicated.

## Reports

The REPORTS tab can save a Weather snapshot to the SD card under `/orcsdr/weather/`. Report bundles contain structured session metadata, observations/RF events where present, alert data, a human-readable report, and SHA-256 manifest entries.

## What Foundation does not claim

NOAA SAME decoding, personal weather-sensor decoding, radiosonde/Balloon Hunter, satellite pass acquisition, and LRPT imagery are later Weather Hunter/Satellite phases. Foundation does not claim those features, and it does not claim hardware or RF verification until a dated on-device run records that evidence.
