# LoRa

The LoRa dashboard is an experimental receive and analysis surface.

- **Overview** shows channel settings, spectrum, capture controls, and recent traffic.
- **Nodes** lists received sender records; missing fields remain unknown.
- **Traffic** retains bounded receive events and packet details.
- **Map** plots positions only when a packet contains usable coordinates.
- **RF Health** reports receiver rate, drops, capture/log state, and decoder readiness.

LoRa packet capture and Meshtastic interpretation depend on the selected spreading factor, bandwidth, channel, and keys. OrcSDR does not transmit from these pages.
