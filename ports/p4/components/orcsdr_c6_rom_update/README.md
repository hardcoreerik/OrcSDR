# C6 ROM SDIO updater backend

This component provides a two-phase update policy and the pinned
`esp-serial-flasher` v2.1.0 SDIO adapter (source commit
`24997fe693ff17e6598d7eb1d37af312752100aa`). It contains no firmware images.

The application must first authenticate the official package/manifest and
populate `orc_c6_rom_manifest_t` with all ranges, source bytes, and SHA-256
digests. `trusted` is an assertion from that package verifier; this component
then independently hashes every source segment and bounds every segment to
the manifest flash capacity. Use image chip ID 13 for ESP32-C6.

Typical sequence:

1. Stop Hosted and establish exclusive ownership of SDIO.
2. Allocate `esp_loader_t`, `esp32_sdio_port_t`, and `esp_loader_port_ops_t` in
   caller-owned storage; initialize an `orc_c6_rom_loader_session_t` with the
   board SDIO pins, known flash capacity, and C6 EN reset callback.
3. Set `orc_c6_rom_ops_t.context` to that loader session and use the included
   `orc_c6_rom_loader_*_callback` functions for connect, segment write, and
   readback SHA. Use `orc_c6_rom_loader_sha256_callback` for source SHA.
4. Call `orc_c6_rom_prepare`. On success it reports `RELEASE_BOOT`; wait for
   the user to release the physical C6 BOOT button, then call
   `orc_c6_rom_flash` once.
5. Disconnect the loader and restore Hosted ownership in the caller, including
   every failure path.

The protocol wrapper denies unknown commands and permits only read-only ROM
preflight requests plus the currently armed segment's exact FLASH_BEGIN,
ordered FLASH_DATA packets, and matching MD5 verification. It only permits
FLASH_END if the pinned ROM path emits it after verification. Block retry
count is set to one for this use of the pinned dependency. The helper
does not erase or write eFuses. There is no automatic connection retry.

The ESP ROM loader does not expose a public flash-size setter. The adapter uses
the board profile's known flash capacity to avoid ROM register-write size
autodetection; the profile must reject unknown C6 flash variants. The caller
still owns provenance/signature verification, profile selection, user BOOT
instruction, Hosted quiescence, restart/reconnect, and user-facing recovery.
The module does not make those application-level claims.
