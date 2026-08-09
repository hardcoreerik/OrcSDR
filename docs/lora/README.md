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

## Capture and decode

1. Copy `apps/orcsdr-tab5/assets/lora_dashboard_384x470.jpg` to
   `/orcsdr/lora_dashboard_384x470.jpg` on the Tab5 SD card.
2. Install the decoder once:

   ```powershell
   py -3.11 -m pip install -r tools/requirements-lora.txt
   ```

3. Start the live bridge, enter **LORA**, and transmit a Meshtastic text packet:

   ```powershell
   py -3.11 tools/decode_orciq.py --watch-port COM17
   ```

The bridge watches for an adaptive energy capture, safely pauses and resumes
the SDR around SHA-256-verified SD transfer, decodes the burst, and returns only
validated text to the dashboard. The display labels returned text
`HOST CRC+KEY OK` and shows the sender node ID. Use `--capture-dir <existing-dir>`
to retain transferred IQ files; otherwise the verified host copy is temporary.

**IQ CAP** remains a manual three-second capture control. A saved file can be
decoded directly with:

```powershell
py -3.11 tools/decode_orciq.py path\to\capture.orciq
```

The decoder tries clear packets and Meshtastic's public default channel. For a
private channel, put only its hex or base64 PSK in a gitignored file and add
`--psk-file path\to\channel.key`. The key is never printed. A valid text packet
prints as `MESSAGE ...`; failed CRC, sync, key, and protobuf checks are reported
as failures rather than rendered as messages.

Run the complete synthetic receive-chain check with:

```powershell
py -3.11 tools/decode_orciq.py --self-test
```

Current boundary: the RF capture is automatic, but the PHY/decryption stage is
host-assisted rather than standalone on the P4. Standard channel AES-CTR text
packets are supported. Meshtastic PKI direct messages (X25519/AES-CCM) and
compressed text require additional key material or codecs and are intentionally
reported as non-text payloads.
