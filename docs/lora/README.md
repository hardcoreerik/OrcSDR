# LoRa / Meshtastic receive path

The Tab5 LoRa dashboard is a receive-only SDR instrument. In LoRa mode it keeps
a 250 ms IQ pre-roll, learns the local noise floor, and captures three seconds
when RF energy rises 9 dB above that floor. Captures are raw interleaved
unsigned 8-bit IQ at 960 kS/s in the `ORCIQ01` format. LoRa chirp/FEC and
Meshtastic decoding run on the connected PC so they do not reduce the Tab5
scope frame rate.

The US preset opens at **906.875 MHz, SF11, BW250**, matching the current
Meshtastic US LongFast default slot. Match the transmitting nodes' region,
frequency slot, modem preset, and channel key when they differ.

## Dashboard views

LoRa mode keeps RF capture running while the operator switches among three
render-only views:

- **RF** keeps the spectrum/waterfall and a compact three-packet summary.
- **PACKETS** uses the full content area for six large, readable verified packet
  rows: age, source, destination, port, decoded content, SNR, and relative signal.
- **MAP** uses the full content area for an offline local plot centered on the
  newest valid Meshtastic Position packet. The square is approximately 10 square
  miles (3.162 miles per side) and retains the last position received per node.

The map is deliberately not a street map. It plots only coordinates explicitly
decoded from RF packets, requires no network or tile storage, and does not infer a
location from signal strength. `SIG` is capture-relative dBFS, not calibrated RSSI.

## Capture and decode

1. Copy `apps/orcsdr-tab5/assets/lora_dashboard_384x470.jpg` to
   `/orcsdr/lora_dashboard_384x470.jpg` on the Tab5 SD card.
2. Install the decoder once:

   ```powershell
   py -3.11 -m pip install -r tools/requirements-lora.txt
   ```

3. Connect a Meshtastic node on COM24 for in-memory channel and direct-message
   keys, then start the live bridge:

   ```powershell
   tools\monitor_lora_to_pc.bat COM17 COM24
   ```

The bridge watches for an adaptive energy capture, verifies the IQ transfer,
decodes LoRa CRC and Meshtastic encryption, and returns validated packets to the
dashboard. Channel PSKs and per-node PKI keys stay in process memory and are never
printed or written to the evidence directory. Text, routing, NodeInfo, telemetry,
and Position port types can appear; valid Position latitude/longitude feeds MAP.
Use `--capture-dir <existing-dir>` to retain transferred IQ files; otherwise the
verified host copy is temporary.

**IQ CAP** remains a manual three-second capture control. A saved file can be
decoded directly with:

```powershell
py -3.11 tools/decode_orciq.py path\to\capture.orciq
```

Without `--mesh-port`, the decoder tries clear packets and Meshtastic's public
default channel. For a private channel, put only its hex or base64 PSK in a
gitignored file and add `--psk-file path\to\channel.key`. The key is never printed.
Failed CRC, sync, key, and protobuf checks are reported as failures rather than
rendered as verified packets.

Run the complete synthetic receive-chain check with:

```powershell
py -3.11 tools/decode_orciq.py --self-test
```

Current boundary: RF capture and display are on the Tab5, while LoRa PHY and
Meshtastic decryption remain PC-assisted. Standard channel AES-CTR and PKI direct
messages (X25519/AES-CCM) are supported when COM24 supplies the required keys in
memory. Compressed text still requires an additional codec.
