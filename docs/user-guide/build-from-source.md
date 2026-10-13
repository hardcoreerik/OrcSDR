# Build OrcSDR from source (M5Stack Tab5)

This is the normal **build and flash** guide for contributors who want the latest
code on `main`. You do **not** need to rebuild or update the Tab5's ESP32-C6
coprocessor to use OrcSDR radio reception, dashboards, or ordinary Wi-Fi.

**Just want to install OrcSDR?** You do not need ESP-IDF:
use the [release web flasher](https://theorc.dev/sdr/flash/) or, for untagged
changes, the [nightly web flasher](https://theorc.dev/sdr/nightly/).
Nightlies are development builds: check the displayed commit before flashing.
See [Getting started](getting-started.md) for hardware setup.

## Requirements

- An **M5Stack Tab5** (ESP32-P4) and a USB data cable connected to its
  **USB Serial/JTAG** port, not the RTL-SDR USB-host port.
- A **Windows** computer with PowerShell, Git, and
  [Espressif ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/get-started/windows-setup.html)
  **5.5.4** and its tools installed, including the ESP32-P4 toolchain.
- An existing OrcSDR installation with the **current compatible Tab5 partition
  layout** if you intend to use the settings-preserving application-only flash.
  For a first install or an uncertain/older partition layout, use the web
  flasher or a full release installer instead.

OrcSDR uses **native ESP-IDF** for the Tab5. Do **not** use PlatformIO or the
Arduino IDE for the Tab5 firmware (the *separate* OrcDial accessory is different).
The tracked `dependencies.lock` pins the project's managed components.

## 1. Get the latest main branch

From PowerShell:

```powershell
git clone https://github.com/hardcoreerik/OrcSDR.git
Set-Location OrcSDR
git switch main
git pull --ff-only origin main
git log -1 --oneline
```

On a checkout you already have, run `git fetch origin` and
`git pull --ff-only origin main` on your local `main`. Check the printed
commit ID when someone asks you to test a particular fix.

## 2. Build the Tab5 application

```powershell
Set-Location apps/orcsdr-tab5
.\tools\build-tab5-idf.ps1
```

By default this builds **only the Tab5/P4 application**. It does not build,
embed, or flash an ESP32-C6 update. The helper activates ESP-IDF 5.5.4,
resolves pinned components, applies the project's display/Hosted patches,
and compiles the application. It may take a while on a fresh checkout.

If your ESP-IDF framework was installed outside the default path, specify it:

```powershell
.\tools\build-tab5-idf.ps1 -IdfPath 'D:\Espressif\frameworks\esp-idf-v5.5.4'
```

**Expected output:** `build-native-hosted3/orcsdr_tab5.bin`. The name of the
build directory is historical; it does **not** mean you have to replace the
C6 firmware.

## 3. Flash the P4 application without erasing settings

Only do this on a **compatible existing OrcSDR installation**. Close serial
monitors, verify the Tab5's actual Windows COM port in Device Manager, and
do not select another connected development board.

If `idf.py` is not already on PATH in this PowerShell window, activate
ESP-IDF 5.5.4 (adjust the framework path if yours differs):

```powershell
. 'C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'
```

Then, while in `apps/orcsdr-tab5`:

```powershell
idf.py -B build-native-hosted3 -p COM7 app-flash
```

Replace **`COM7`** with your own Tab5 port. `app-flash` updates the
application partition only; it does not erase NVS, overwrite the bootloader
or partition table, or touch the C6. Wait for flash verification and reboot
the Tab5. Verify the build ID under **Settings → System → BUILD**, then test
the changed feature with the RTL-SDR connected.

**Do not** use `erase-flash`, flash a full merged image at `0x10000`, or
assume a generic `idf.py flash` is application-only. A **nightly merged
`.bin`** is instead a complete image for **address `0x0`** and may wipe
settings. Prefer the nightly website's settings-preserving upgrade path.

## Do I need to update the ESP32-C6?

**No, not for ordinary OrcSDR use.** The P4 application uses the
ESP-Hosted **3.0.6 host library**, but both the Tab5's factory C6 firmware
**2.12.6** and the OrcSDR C6 **3.0.6** have been tested for ordinary Wi-Fi
with that host. You do not need matching version numbers just to use SDR or
Wi-Fi. See the [measured compatibility record](../validation/esp-hosted-c6-compatibility-2026-09-29.md).

The separately embedded C6 *update image* is an optional tool for advanced
OrcDial/M5Dial **ESP-NOW** development and special recovery work; embedding an
image does not install it. If you specifically need that image in your Tab5
build, opt in:

```powershell
.\tools\build-tab5-idf.ps1 -IncludeC6Image
```

The image is built from the pinned source once and cached. This option **does
not** flash the C6. Any C6 firmware update must be a separate, deliberate
operation. Whether your OrcDial setup actually needs a C6 change depends on
the accessory/bridge being tested—do not change the coprocessor just because
you're compiling OrcSDR.

**Warning:** updating the C6 to 3.0.6 has been shown to break **M5 Launcher
Wi-Fi and OTA**, whereas factory 2.12.6 preserves those features. Do not
recommend upgrading the C6 as a standard install or build step.

For accessory instructions, see the [OrcDial README](../../orcdial/README.md).
For the low-level P4/C6 transport and migration history, see the
[technical record](tab5-esp-hosted-3-migration.md). Neither is a prerequisite
for the normal source build.

## Problems?

- **`export.ps1` or `idf.py` not found:** install ESP-IDF **5.5.4**, confirm
  its tools and ESP32-P4 target were installed, and supply `-IdfPath` if needed.
- **Component or patch mismatch:** start from a clean `main` checkout, check
  the pinned `dependencies.lock`, and capture the full error. Avoid changing
  ignored `managed_components/` files by hand.
- **Wrong port or flash error:** close any other serial monitor; confirm the
  connected USB Serial/JTAG port belongs to the **ESP32-P4 Tab5**.
- **First install / old partition layout:** use the
  [web flasher](https://theorc.dev/sdr/flash/) rather than `app-flash`.
- **Wi-Fi issue:** check the actual C6 version and boot logs before considering
  an update; see [Troubleshooting](troubleshooting.md).

Source build, successful flashing, and live reception are **three separate
checks**. If you report a problem, include the exact Git commit, receiver
model, reproduction steps, and relevant logs.
