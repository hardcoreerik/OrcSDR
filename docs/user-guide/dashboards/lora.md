# LoRa

The LoRa dashboard is an implemented live packet decoder, not just a spectrum monitor: the maintainer has received unencrypted real Meshtastic messages, node info, telemetry and GPS. Encrypted packets are identified as **ENCRYPTED FRAME** without showing invented plaintext. It is receive-only, not a Meshtastic transmitter. The **Channels** picker supports the 24 listed regional LongFast plans (SF11/BW250); 2.4 GHz, EU Lite/Narrow and licensed amateur profiles are not offered. Antenna placement, regional channel choice and signal strength affect reception and packet loss.

- **Overview** shows a LoRa-focused 500 kHz spectrum, the selected 250 kHz channel
  edges, a stable noise-relative waterfall, signal strength in dBFS, capture
  controls, and recent traffic. **Scan Band** briefly surveys up to 14
  evenly spaced channel centers, shows its three strongest readings, and restores
  the selected channel. These readings are not calibrated dBm or packet detections.
- **Nodes** lists received sender records; missing fields remain unknown.
- **Traffic** retains bounded receive events and packet details. Message rows show
  the fixed UTC receipt timestamp and repaint only when content changes. Packet
  Details adds monotonic age and updates it once per second. **TIME NOT SET** is
  shown when UTC was unavailable. The
  event list uses the full panel width for readable timestamps and messages.
  Its upper toolbar switches between messages and retained packet details,
  saves the recent log, changes the filter, or clears the in-memory events.
- **Map** plots positions only when a packet contains usable coordinates.
- **RF Health** reports receiver rate, drops, capture/log state, and decoder readiness.

LoRa packet capture and Meshtastic interpretation depend on the selected spreading factor, bandwidth, channel, and keys. OrcSDR does not transmit from these pages.
