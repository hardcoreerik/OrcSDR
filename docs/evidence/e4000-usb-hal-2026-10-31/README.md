# Isolated USB HAL crash-fix candidate

This lab candidate addresses the assertion identified in COM39's saved crash,
using Espressif commit
`63b468539f86f759ae1075ed277b2da65eccbdfe`: remove the requirement that CHHLTD
already be present when handling a channel error interrupt. Error classification
and the surrounding handler remain unchanged. Do not disable assertions globally.

The installed ESP-IDF 5.5.4 HAL was copied into this application's ignored
`components/hal`. Of 1,018 copied files, only `usb_dwc_hal.c` differs, by the
single assertion deletion. Shared SDK source was hash-checked unchanged.
CMake component selection and the actual compiler input both point at the
local HAL. Exact source hashes are in the provenance JSON files.

Build: `idf.py -B build-offset -D SDKCONFIG=<absolute existing sdkconfig>
reconfigure build`, using the exported ESP-IDF 5.5.4 environment. An initial
direct-CMake reconfiguration omitted generated Wi-Fi remote options and failed;
the full exported environment restored them. That failed build was not flashed.
Full configuration/build logs remain local. The existing approved M5GFX display
patch is still present. The shared SDK and COM17/COM8 were not changed.

Image: 5,443,072 bytes; SHA-256
`32daeb64cc0f898c6fcda10c1e510e759b7ec1206993e75d9c03fba250370407`.
Esptool verified written data on COM39. Previous COM39 image was read back and
matched `751d9d73f57c7c025f15645cb37acb3f91f9481e29ff92d24a4d226d5f2c9e54`;
that recovery image is retained in the ignored build folder. Driver source
remains exact revision da22090; this is an SDK HAL test, not a new tuner profile.

## Checks so far

- Earlier host harness used the actual installed decoder function: stock
  assertion reproduced, 202 patched classification/halt/completion checks passed.
- Patched image boots and enumerates the enrolled E4000.
- Stop/start and transitions through 100 MHz, 126.6 MHz and 132 MHz completed
  without an observed panic. These are control/stream tests, not reception tests.
- Two commands during startup returned NOT_STREAMING and were not counted as
  successful hot retunes. Settled repeats to 101 MHz and back to 100 MHz both
  returned ESP_OK with correct requested IQ center and physical LO metadata.
- Settled uptime increased from 37,907 to 43,097 ms; offset remained 2.04 MHz.
  Sanitized serial records are retained alongside this report.

USB reconnect observation is pending. This test has not deliberately generated
the exact early-error interrupt seen in the crash. Even a successful reconnect
and absence of panic would be bounded recovery evidence, not proof that all USB
faults are fixed. Broadcast reception and other-dongle hardware regressions
remain independent acceptance gates.

## Reproduce the isolated override

With approval for the lab build, use a clean application checkout and the exact
SDK version. The staging helper refuses an existing HAL override:

```powershell
python tools/prepare_usb_hal_lab_override.py C:/Espressif/frameworks/esp-idf-v5.5.4 apps/orcsdr-tab5
```

Keep the copied component and generated provenance local, outside release
commits. Verify `project_description.json` and `compile_commands.json` select
that component, preserve image/source/configuration hashes, and retain the
shared SDK's original hash. A portable release integration still needs a
documented SDK requirement or maintained patch procedure; this local override
alone is not a distributable release.

Follow-up status at uptime 131,693–133,493 ms still showed STREAMING at about
2.396 MS/s, 510,951,424 cumulative bytes, zero reported overruns/drops and the
expected 2.04 MHz offset. The firmware PNG command completed with a verified
image. No reboot or panic was observed in this interval. This is approximately
two minutes of bounded post-boot observation, not an endurance or fault test.
The 55-second passive reconnect window contained no serial events; it therefore
provides no disconnect/recovery evidence. Physical unplug/replug remains pending.
