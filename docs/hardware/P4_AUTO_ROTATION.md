# Profile-aware auto-rotation

Added at the user's request: the T-Display P4 accelerometer must be usable
through an **Auto-rotate** toggle in the existing display settings. This
supersedes the original plan's exclusion of automatic IMU rotation.

- Default off for predictable behavior until the user enables it; retain manual
  rotation. Persist the toggle/manual preference using the shared settings path.
- Advertise the feature only when the selected board profile has a supported
  orientation sensor and initialization succeeds. LilyGO documents ICM20948 at
  0x68 on the secondary I2C bus20/21, distinct from the panel/touch bus7/8. Verify
  WHO_AM_I and the mounted-axis mapping on the actual board, not just its name.
- Implement a sensor provider behind the board API. The UI consumes a proposed
  orientation; it does not read I2C registers or know the sensor model.
- Use gravity filtering, angular hysteresis and stable dwell time. Avoid repeated
  rotation near a boundary, during shaking, or while face-up/down when gravity
  cannot determine an in-plane orientation. Keep the last accepted orientation.
- Route sensor, manual setting and authenticated serial requests through the
  same orientation coordinator. Keep the shared geometry authoritative for
  drawing, touch and focus. No separate sensor-only screen transform.
- Defer rotation until an active touch/drag ends; preserve entry, modal, scroll
  and receiver state through a permitted orientation change. Manual rotation
  remains available with auto-rotate disabled or the sensor unavailable.
- Lock geometry for the critical C6 flash/update workflow so the physical-button
  instructions, progress and hit targets remain stable. Apply a pending valid
  orientation after the workflow exits safely.
- Test deterministic orientation traces, jitter/dead zones, face-up/down, sensor
  loss/recovery, touch deferral and flash locking. Physically verify all supported
  angles and repeat with auto-rotate off. Serial and Settings must expose the same
  enabled/disabled/unavailable status and behavior.

This is an implementation requirement, not a claim that an IMU driver, persisted
toggle or automatic rotation is already installed in the display diagnostic.

## Portable policy and board provider foundation

The portable policy is `components/orcsdr_orientation`; its public contract is
`orcsdr_orientation_policy.h`. It accepts a profile-supplied 2x3 matrix that
maps calibrated sensor gravity into display-right/display-down coordinates.
It intentionally contains no LilyGO axis guess. It emits shared geometry angles
0/90/180/270, starts disabled, filters gravity, rejects acceleration outside
the gravity band and abrupt shakes, rejects face-up/down ambiguity, and holds a
candidate through a 12-degree boundary exclusion band and 500 ms dwell. Profile
values may override dwell and hysteresis. A manual request queues through the
same touch-active and geometry-lock gates, including while automatic mode is off
or the sensor read has failed.

`ports/p4/components/orcsdr_board_hosyond/src/icm20948_orientation.[ch]` is the
LilyGO sensor provider. The caller supplies and owns the secondary bus; the
provider owns only its 0x68 device handle. It checks WHO_AM_I=0xEA, selects
banked registers explicitly, configures the accelerometer for +/-2 g, and
returns the six-byte XYZ sample scaled to g. It does not choose board pins,
mount orientation, expose UI/settings state, or operate on other profiles.
Register definitions and scale are based on TDK InvenSense's
[ICM-20948 datasheet DS-000189 v1.6](https://invensense.tdk.com/wp-content/uploads/2024/03/DS-000189-ICM-20948-v1.6.pdf).
The LilyGO [T-Display-P4 hardware table](https://github.com/Xinyuan-LilyGO/T-Display-P4)
identifies the ICM20948 fitted to that board family. The implementation is
written from the datasheet; it does not derive from the GPL LakeShark IMU driver.

The console coordinator foundation is in
`apps/orcsdr-p4-hosyond-console/main/orientation_app.[hpp|cpp]`. It loads
`auto_rot` (default false) and `manual_deg` from `orcsdr_display` before it
starts polling; a saved valid manual angle takes precedence over the caller's
initial-angle fallback. The I2C reads run in a bounded background task and the
UI tick consumes a recent cached sample. Each returned proposal must be applied
through shared geometry and acknowledged so status reports the actual angle.
Manual proposals still work when the profile has no initialized sensor.

The console and board component CMake source lists now include the coordinator/provider;
the LilyGO profile declares the optional ICM20948 at 0x68. The UI owner polls
the coordinator after touch dispatch, gates proposals while a gesture or C6
update/reboot is active, and acknowledges geometry changes. Authenticated serial
controls are `RTL_ORIENTATION STATUS`, `RTL_ORIENTATION AUTO ON|OFF`, and
`RTL_ORIENTATION MANUAL 0|90|180|270`. Status includes availability, saved
preference, active mode, and raw XYZ telemetry for calibration.

Settings > Display & Audio exposes manual angles and the persistent Auto-rotation toggle. Automatic rotation remains opt-in. The LilyGO profile supplies measured axes; other profiles default to unverified axes and retain manual fallback.

On 2026-11-01, six samples were recorded in each user-confirmed physical pose. Mean gravity vectors were upright (0.825,0.004,-0.511), clockwise (0.021,-1.003,-0.063), upside down (-0.994,-0.039,-0.186), and top-left (-0.065,0.973,-0.153). Upright was tilted but had sufficient in-plane gravity. The accepted mapping is policy X=sensor X, policy Y=-sensor Y: physical clockwise rotation proposes view270, keeping text upright. The initial direct-Y mapping was rejected by the human direction test.

The corrected mapping is installed in checkpoint r4. The human confirmed both manual270 and automatic portrait-to-clockwise-landscape are upright. Policy tests replay all four measured poses and separately cover 500 ms dwell, 12 degree hysteresis, shake/face-up rejection, manual fallback, and touch/update gates. Software checks do not establish physical acceptance of those other cases.

Physical gates still open: opposite landscape and inverted automatic transitions, preference persistence after restart, turning while a gesture is held, firmware-update rotation locking, and receiver/audio continuity. The sampled clockwise transition must not be generalized to these gates.

Dashboard rotation invokes an optional geometry-change callback. Home, FM and Settings have sampled native rotation validation; AM, RF Lab, Wi-Fi Analysis and additional dashboards are being migrated and checked individually. A dashboard without a callback returns to Home. Cross-profile build and physical acceptance remain separate gates.
