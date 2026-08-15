# Radio configuration files

OrcSDR works without Wi-Fi. Put a microSD card in the Tab5 and use these
plain-text files under `/orcsdr/`. They are created on first boot and can be
edited on a computer while the Tab5 is powered off.

After editing `P25.cfg`, open **P25 → Program** and tap **RELOAD P25.CFG**.
Invalid files never replace the active profile; OrcSDR keeps the last valid
profile and reports the error on the Program screen. The previous saved file
is retained as `P25.cfg.bak`.

## `/orcsdr/P25.cfg`

```ini
# One key=value per line. Lines beginning with # or ; are comments.
version=1
system_name=My P25 System
control_channel_hz=453812500
control_channel_hz=453925000
last_control_channel_hz=453812500
auto_follow=true
encryption_skip=true
hold_talkgroup=0
talkgroup=20001,Dispatch
talkgroup=20391,Firecom 1
```

Use one `control_channel_hz` line per known control channel (up to eight) and
one `talkgroup=ID,Label` line per label (up to eight). Frequencies must be in
the 450–470 MHz range supported by the current P25 dashboard. The P25 decoder
learns compatible trunking band-plan data from the received control channel;
this file does not provide a transmit capability or bypass encryption.

## `/orcsdr/FM.cfg`

```ini
version=1
startup_frequency_hz=96100000
preset_hz=96100000
preset_hz=101700000
```

FM presets can be entered here or found with **FM → Settings → Scan Presets**.
The scan result is saved back to `FM.cfg`. Frequencies must be within the
87.5–108 MHz broadcast band. The previous saved file is retained as
`FM.cfg.bak`.

Wi-Fi database packs are intentionally not required for either file. They are
a later optional update feature, after public-source licensing and manifest
validation are established.
