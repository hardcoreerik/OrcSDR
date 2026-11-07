# Global Settings

The gear opens Settings without stopping ordinary reception.

| Category | Purpose |
|---|---|
| Connectivity | Wi-Fi power, internal/external antenna, scan, connect, and four saved profiles |
| Location & ADS-B | One receiver location and 10/25/50/100 NM radar range |
| Data & Maps | Aircraft database and offline map-pack status |
| Display & Audio | Brightness, timeout, global volume, sound default, and 180-degree rotation |
| Radio Defaults | Startup reception, last/default band, FM frequency, and graphics |
| Storage | SD health, capacity, and bounded maintenance status |
| Companion | Optional LAN web console for Android TV, plus phone/BLE placeholders |
| System | Hardware UTC status, build, uptime, power, and diagnostics |

Saved Wi-Fi passwords are masked and never returned through the Settings UI or documentation capture interface.

Maps integration builds add **LOCAL MAPS** under **Data & Maps**. The
[offline maps guide](offline-maps.md) explains **GET MAPS**, installed-map
selection and copying a downloaded pack onto the SD card yourself. Availability
of remote downloads depends on the catalog configured in that firmware.

## Set the clock

Open **Settings → System → SET CLOCK**. Set your local date and time with
its field controls, choose the **UTC OFFSET (TIME ZONE)** for your location,
and tap **SAVE**. No computer or network connection is required. The hardware
clock stores UTC; the offset converts the local time you enter into UTC.
Adjust the offset when your local daylight-saving time changes.

For optional network synchronization, connect to Wi-Fi under **Connectivity**,
then open **SET CLOCK** and tap **SYNC NTP**. Wait for the sync result; having
Wi-Fi connected alone does not confirm that the clock was set. Tap **SAVE**
to keep any UTC offset change.

System shows a green UTC value once the hardware clock is established.
Otherwise it shows **TIME NOT SET** in orange. Home also shows UTC once set;
until then its uptime clock is labeled **TIME NOT SET**.

For an alternative USB method, connect the Tab5 to a computer with Python
and the repository available, then run `python tools/sync_tab5_rtc.py COM17`
(replace `COM17` with its actual port). This authenticates locally and copies
the computer's UTC time without a network connection.

## Companion web console

Companion → ENABLE starts the TV Mission Control page at `http://<tab5-ip>/` and
advertises `orcsdr.local`. Use that URL from a browser or the sideloaded
`apps/orcsdr-tv` app. This is a read/write HTTP control surface, not only a
viewer: it can tune, change volume or mute state, adjust span/step, and open
dashboards. It has no TLS or application authentication. Enable it only on a
trusted LAN, and do not expose port 80 to the Internet.
