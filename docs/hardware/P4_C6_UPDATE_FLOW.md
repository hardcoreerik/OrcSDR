# Profile-aware C6 update in Settings

User requirement: retain **Settings → Firmware & Updates**, adapting the
existing screen/service for this board. LilyGO uses the proven ROM-SDIO
bootloader path and a physical C6 BOOT prompt. This document is an accepted
requirement and implementation contract, not a claim the in-app updater is done.

## User flow on LilyGO

1. Show embedded target version, installed version if available, and explicit
   availability. Factory ESP-AT or an unreachable Hosted transport must not hide
   a supported ROM recovery/update action.
2. Display:

   **Hold the physical C6 BOOT button.**

   **While holding it, tap Update now. Keep holding until asked to release.**

   Clarify that this is the C6 button, not the P4 BOOT or RESET button. No external
   UART adapter is needed for this demonstrated board path. Normal USB power
   stays connected. Give immediate visible feedback on the action.
3. After Update now, quiesce/release the Hosted transport and take exclusive
   SDIO ownership in a worker. The UI/input task must remain responsive.
4. P4 pulses the C6 enable/reset through the profile's expander control, then
   connects the ROM downloader and validates chip/security/flash capabilities.
   No flash write before preflight succeeds. Do not pretend P4 can directly
   sense whether the physical BOOT button is held.
5. After the ROM/helper connection is established, prominently display
   **C6 connected — release BOOT now.** Keep that instruction observable through
   flashing if needed; establish release before the final reset/restart.
6. Show preparing / writing / verifying progress through bounded widget updates.
   Verify the complete intended written ranges before reporting success.
7. Restart/reconnect, verify C6 firmware/Hosted communication, and report the
   real result. Restore Wi-Fi using saved P4 configuration. Do not claim Wi-Fi
   success from a successful flash alone.

If connection fails: say that no firmware was written, explain the same hold-
and-tap action, allow an explicit retry/cancel where safe, and do not erase or
retry automatically. After a partial-write/verification failure, retain a clear
recovery route and repeat the ROM connection flow; do not loop silently.

## Shared implementation boundaries

- Board capability chooses **Hosted OTA**, **ROM-SDIO with physical BOOT**, or
  **unsupported**. Keep Tab5's current supported path; do not show LilyGO button
  instructions on unrelated boards or offer a C6 image to a C5 profile.
- UI, authenticated serial update command and tests use the same update
  coordinator/state machine. Preserve existing serial authorization/confirmation
  rules. Status must expose transport, phase, percentage and stable failure stage.
- Add a specific release-BOOT phase/status; do not infer it from percent=0.
- The current wifi_service update path requires Hosted readiness and calls
  esp_hosted_slave_ota_*; its readiness gate cannot be reused unchanged for ROM
  recovery. ROM availability is a separate verified capability, not a fake
  Hosted-ready flag.
- Use the matching pinned OrcSDR C6 image and official build artifacts with
  target/version/layout/hash provenance. Bootloader/table/OTA-data/application
  components must support factory ESP-AT migration, not just overwrite the old
  application address. Never embed the private snapshots used in the experiment.
- Validate target C6, image ranges, flash size and supported security mode;
  reject unsupported secure/encrypted targets and unknown layouts explicitly.
- Resolve SDMMC/Hosted ownership and callback shutdown before reusing the bus.
  Keep driver/bus code out of the Settings renderer. Do not promise uninterrupted
  radio/audio/network during update until measured with the actual coordinator.
- If later panel/touch progress events are asynchronous, publish state only;
  the UI owner draws, with no full-page clears on progress ticks.

## Acceptance

Both portrait and landscape must show readable instructions, an accessible
Update now button, release-BOOT feedback, bounded progress and useful errors.
Test missing-button/timeout with no writes; explicit retry; cancellation before
the irreversible phase; duplicate requests; corrupt/wrong-target image;
write/verify failure; restart/reconnect; factory ESP-AT and a compatible Hosted
starting state. Test bus ownership and late callbacks. Validate real in-app
button use separately from the earlier console-only proof. Preserve recovery
until physical acceptance and cross-profile tests pass.
