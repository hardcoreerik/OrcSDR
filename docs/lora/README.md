# LoRa / Meshtastic receive path

The Tab5 LoRa dashboard is a receive-only SDR instrument. It captures three
seconds of raw interleaved unsigned 8-bit IQ at 960 kS/s to the SD card as an
`ORCIQ01` file. Decoding runs on the connected PC so LoRa chirp/FEC processing
does not reduce the Tab5 scope frame rate.

## Capture and decode

1. Copy `apps/orcsdr-tab5/assets/lora_dashboard_384x470.jpg` to
   `/orcsdr/lora_dashboard_384x470.jpg` on the Tab5 SD card.
2. Enter **LORA**, select frequency, bandwidth, and spreading factor, then tap
   **IQ CAP**. The capture is written under `/orcsdr/*.orciq`.
3. Stop the radio and retrieve the file with `tools/copy_from_tab5_sd.ps1`.
4. Install the decoder once and run it:

   ```powershell
   py -3.11 -m pip install -r tools/requirements-lora.txt
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

Current boundary: standard channel AES-CTR packets are supported. Meshtastic
PKI direct messages (X25519/AES-CCM) and compressed text require additional key
material or codecs and are intentionally reported as non-text payloads.
