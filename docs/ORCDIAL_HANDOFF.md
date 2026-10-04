# OrcDial connection testing handoff

Branch: `codex/m5dial-vfo`. This branch starts from `origin/main` commit `11cc19e`; on October 3, 2026 it was 77 commits behind current main. Port the OrcDial changes onto current main with care, especially `apps/orcsdr-tab5/ui/main.cpp` and hosted-C6 build files. The current main Airband dashboard must be preserved.

OrcDial is an optional accessory. The standard OrcSDR build includes its support; users should not need a separate Tab5 download. Normal communication is ESP-NOW through the Tab5 C6 and ESP-Hosted peer-data bridge.

## Verified evidence

- The M5Dial firmware built and flashed successfully on COM14 on September 30, 2026, with upload hashes verified. Device MAC: `e8:f6:0a:98:26:2c`.
- Successful command: `C:\Users\hardc\.platformio\penv\Scripts\python.exe -m platformio run -d orcdial -e dial -t upload --upload-port COM14`.
- A subsequent serial read reported `ORCDIAL_STATUS link=OFFLINE`. This confirms firmware execution, not pairing.
- The user accepted the illustrated dashboard selector before the latest FM layout. The newest FM layout and ADS-B/Airband icons have been flashed but have no recorded physical acceptance yet.
- No Tab5 flash was performed in this chat. COM17 was identified as the Tab5 port; verify device identity and get authorization before writing it.

## Next checks

1. Inspect and port the P4 host bridge, C6 endpoint, peer-data configuration, pairing controls, and release build integration onto current main.
2. Build the matching P4 and C6 firmware, then validate discovery, pairing, state updates, acknowledgments, disconnect/reconnect, and normal OrcSDR startup without a Dial.
3. Confirm FM frequency, step, and volume commands reach the existing FM handlers. The newest source permits backward FM step selection; this Tab5 source change has not been built or flashed.

The FM screen currently exposes Tune, Step, and Volume. Gain is omitted because the host bridge does not implement it. Seek, RDS, stereo, and relative signal data are not exposed on the Dial yet. See `ORCDIAL_CONTROL_MATRIX.md` for the broader mapping and `orcdial/PROTOCOL.md` for the packet format. Neither a successful build nor a Dial flash proves the P4/C6/Dial connection.
