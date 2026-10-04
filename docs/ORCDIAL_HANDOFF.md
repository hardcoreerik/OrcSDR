# OrcDial connection testing handoff

Branch: `codex/m5dial-vfo`. The original baseline was `11cc19e`. On October 3, 2026, `origin/main` at `be72a1f` was merged into this branch, retaining the current Airband dashboard and optional C6 update behavior alongside OrcDial support. Connection testing is still required, especially the P4/C6 peer-data bridge and hosted-C6 build integration.

OrcDial is an optional accessory. The standard OrcSDR build includes its support; users should not need a separate Tab5 download. ESP-NOW is the requested transport. The existing experimental implementation uses the Tab5 C6 and ESP-Hosted peer-data bridge, but pairing is not verified. The user requires the installed C6 firmware to remain unchanged; a C6 update must not be a prerequisite.

## Verified evidence

- The M5Dial firmware built and flashed successfully on COM14 on September 30, 2026, with upload hashes verified. Device MAC: `e8:f6:0a:98:26:2c`.
- Successful command: `C:\Users\hardc\.platformio\penv\Scripts\python.exe -m platformio run -d orcdial -e dial -t upload --upload-port COM14`.
- A subsequent serial read reported `ORCDIAL_STATUS link=OFFLINE`. This confirms firmware execution, not pairing.
- The user accepted the illustrated dashboard selector before the latest FM layout. The newest FM layout and ADS-B/Airband icons have been flashed but have no recorded physical acceptance yet.
- On October 3, 2026, with user authorization, the merged P4 app was flashed to COM17 using `idf.py -B build-native-hosted3 -p COM17 app-flash`. The device identified as ESP32-P4, MAC `30:ed:a0:e2:e6:95`; the app upload hash was verified. Bootloader, partitions, and NVS were preserved.
- The no-reset IDF monitor observed `SPLASH_BOOT done`, `ORCDIAL_BRIDGE_READY`, `hosted_match=1`, and `RTL_START ESP_OK`. These prove P4 startup and bridge registration, not the C6 ESP-NOW endpoint or pairing.
- The Dial was reflashed on COM14 with the merged branch's firmware; upload hashes were verified. Subsequent serial output remained `ORCDIAL_STATUS link=OFFLINE`.
- After merging current main on October 3, 2026, the Dial build passed with `C:\Users\hardc\.platformio\penv\Scripts\python.exe -m platformio run -d orcdial -e dial`. A missing local `intelhex` packaging dependency was restored first.
- The merged P4 build passed with `idf.py -B build-native-hosted3 build` after the standard `apps/orcsdr-tab5/tools/build-tab5-idf.ps1` configuration. It embeds the source/hash-verified cached OrcDial C6 image. The C6 image was reused, not freshly rebuilt. No hardware was flashed during the merge itself.

## Next checks

### Serial automation verification, October 4, 2026

The incremental P4 build and COM17 app-only flash passed, including upload hash verification. The Dial build and COM14 flash passed with hashes verified. `python orcdial/tests/serial_smoke.py --port COM14` passed the status, oversized/unknown command, and pairing-start checks.

`RTL_WIFI_CHANNEL` returned `primary=11 secondary=0 connected=1 ssid_hex=68617264636f726577696669 ap_primary=11`: the associated `hardcorewifi` network uses channel 11. `RTL_WIFI_C6_STATUS` reported host 3.0.6, coprocessor 2.12.6, `state=optional match=1`. The C6 was not updated. Local USB `RTL_ORCDIAL_PAIR START` opened the 60-second window; the Dial's `ORCDIAL_PAIR START` began searching. Initial status remained unpaired. A local bridge-ready flag alone does not establish that the remote ESP-NOW endpoint exists.

1. Inspect the merged P4 host bridge, C6 endpoint, peer-data configuration, pairing controls, and release build integration.
2. Preserve the installed C6 2.12.6 firmware, whose ordinary Wi-Fi connectivity is verified. Query `RTL_WIFI_CHANNEL` and use local USB `RTL_ORCDIAL_PAIR START` on the Tab5 and `ORCDIAL_PAIR START` on the Dial. Investigate the installed firmware's accessory transport interfaces before claiming ESP-NOW compatibility or an update requirement.
3. Confirm FM frequency, step, and volume commands reach the existing FM handlers. Backward FM step selection now uses an explicit previous-step action; it compiled and was flashed but has not been verified through a paired Dial.

The FM screen currently exposes Tune, Step, and Volume. Gain is omitted because the host bridge does not implement it. Seek, RDS, stereo, and relative signal data are not exposed on the Dial yet. See `ORCDIAL_CONTROL_MATRIX.md` for the broader mapping and `orcdial/PROTOCOL.md` for the packet format. Neither a successful build nor a Dial flash proves the P4/C6/Dial connection.
