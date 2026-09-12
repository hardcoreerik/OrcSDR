# Feature status

These labels describe evidence, not marketing maturity. **Experimental** may
coexist with an evidence level. Historical hardware results do not automatically
apply to later source snapshots.

| Feature | Current status | Boundary |
|---|---|---|
| FM audio, presets, stereo, RDS | **RF-Verified / Regression-Tested** | Verified on RTL-SDR Blog V4 and one V3C with suitable FM setups. |
| AM broadcast and 119-channel scan | **Hardware-Verified / Experimental** | Exact RC4 package exercised the dashboard and scan; general reception quality remains Experimental. |
| NOAA Weather Radio | **Implemented** | Current-release RF acceptance is Not Verified. |
| CB | **Implemented / Runtime-Verified** | Flashed and exercised; operator/RF acceptance remains open. |
| Shortwave | **Implemented / Experimental** | Generic Browse/NFM workspace only; complete AM/SSB and calibrated HF reception are Not Implemented. |
| Airband | **Implemented / Experimental** | Generic Browse near 121.5 MHz using NFM; not a complete AM aviation voice receiver. |
| Marine | **Implemented / Experimental** | Generic NFM routing; no dedicated dashboard/current RF acceptance evidence. |
| Satellite | **Implemented / Experimental** | Generic routing near 137.5 MHz; no dedicated satellite decoder/dashboard. |
| P25 Phase I | **RF-Verified / Hardware-Verified / Regression-Tested** | Documented control, follow, clear/encrypted behavior; encrypted audio is not decoded. |
| P25 Phase II | **Implemented / Runtime-Verified / Regression-Tested / Experimental** | Grants, sync, complete bursts, DUID classification, and hardware observation exist. Payload and AMBE+2 audio are Not Implemented. |
| ADS-B 1090 | **RF-Verified / Hardware-Verified / Regression-Tested** | Live CRC-valid traffic, independent track comparison, and FAA enrichment; live-only. |
| LoRa/Meshtastic | **Hardware-Verified / Experimental** | Native receive path and observed traffic; reliability and coverage are not broadly established. |
| POCSAG | **RF-Verified at 1200 / Regression-Tested** | `TEST`, CAPCODE 1234560, and Flipper cross-check. 512/2400 are host-tested only; persistence/searchable archive is Not Implemented. |
| RF Lab / RF Visualizer | **Implemented / Regression-Tested** | Integrated self-check/regression tooling; not RF calibration proof. |
| Wi-Fi analysis | **Implemented / Hardware-Verified / Experimental** | Real AP survey through ESP-Hosted 3.0.6; Wi-Fi and catalog I/O pause active reception. |
| Receiver profiles | **Evidence varies** | Blog V4 baseline; V3C RF-Verified/Experimental; earlier V3 and Nooelec provisional; V4L and generic receivers Not Verified. |
| Global Settings | **Implemented / Hardware-Verified** | Wi-Fi, radio defaults, storage, display/audio, catalog, and Companion controls. |
| LAN console | **Implemented / Experimental** | Opt-in read/write HTTP surface; tune/audio/navigation controls, no TLS or authentication. Trusted LAN only. |
| Android TV | **Implemented / Experimental** | LAN client; Tab5 remains the receiver. |
| Signed data catalog | **Hardware-Verified / Experimental** | Public catalog and FAA reinstall evidence; not every proposed data pack is published. |
| Transmission | **Not Implemented** | OrcSDR is receive-only. |

Build, runtime, hardware, and RF evidence are different claims. See the
authoritative [project status](https://github.com/hardcoreerik/OrcSDR/blob/main/PROJECT_STATUS.md) for exact release,
receiver, CI, and remaining-evidence boundaries.
