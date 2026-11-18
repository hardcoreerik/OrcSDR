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

Open **Settings → System → SET CLOCK**. Choose the **UTC OFFSET (TIME ZONE)**
for your location first, then check and adjust the local date and time fields,
and tap **SAVE**. Changing the offset can recalculate untouched time fields,
so check the displayed local time after selecting it. No computer or network
connection is required. The hardware clock stores UTC; the offset converts
the local time you enter into UTC. Adjust the offset when your local
daylight-saving time changes.

For optional network synchronization:

1. Connect to Wi-Fi under **Connectivity**, open **SET CLOCK**, and tap **SYNC NTP**.
2. Wait for a successful sync result. Wi-Fi connectivity alone does not confirm
   that the clock was set.
3. Tap **CANCEL** to discard any date/time edits made before the sync, then
   reopen **SET CLOCK**. Choose your UTC offset again and tap **SAVE** without
   editing the synchronized date/time fields. This avoids overwriting the
   synchronized clock with earlier manual edits.

If synchronization fails and the clock is unset, enter the correct local date
and time manually before saving; do not save the editor's default fields as
though synchronization succeeded.

System shows a green UTC value once the hardware clock is established.
Otherwise it shows **TIME NOT SET** in orange. Home also shows UTC once set;
until then its uptime clock is labeled **TIME NOT SET**.

For an alternative USB method, connect the Tab5 to a computer with the
repository available. In the Python environment you will use for the sync
command, install `pyserial` with `python -m pip install pyserial`, then run
`python tools/sync_tab5_rtc.py COM17` from the repository root (replace `COM17`
with its actual port). This authenticates locally and copies the computer's
UTC time without a network connection during synchronization.

If the Tab5 is already paired, use its existing local key file:
`python tools/sync_tab5_rtc.py COM17 --pairing-key "path/to/existing.key"`.
The default key path is `.orclink/ui-doc.key`; a new checkout's generated key
cannot authenticate an already-paired device. On an unpaired device, the tool
creates the key file and enrolls it. Keep that file private and retain it for
future access.

## Companion web console

Companion → ENABLE starts the TV Mission Control page at `http://<tab5-ip>/` and
advertises `orcsdr.local`. Use that URL from a browser or the sideloaded
`apps/orcsdr-tv` app. This is a read/write HTTP control surface, not only a
viewer: it can tune, change volume or mute state, adjust span/step, and open
dashboards. It has no TLS or application authentication. Enable it only on a
trusted LAN, and do not expose port 80 to the Internet.
