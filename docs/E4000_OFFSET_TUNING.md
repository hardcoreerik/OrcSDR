# E4000 hardware center correction

OrcSDR requests offset tuning before each start only when the attached driver
advertises `ESP_RTL_SDR_CAP_OFFSET_TUNING`. There is no product-name check and
no new software mixer. The driver tunes the E4000 below the requested center,
widens its analog filters and corrects frequency in the RTL2832U before IQ
delivery. Existing application FM channel processing still receives its usual
requested driver center; the hardware offset is not added to that processing.

Default is ON for capable devices. Reconnect and rate changes follow the same
start path. The driver's default remains OFF for other clients. The driver
suspends correction near its lower RF limit when the physical LO would leave
the supported range; requested ON with applied zero makes this observable.

Authenticated serial controls:

```text
RTL_STOP
RTL_DRIVER OFFSET OFF
RTL_TUNE FM 96100000
RTL_DRIVER STATUS
RTL_STOP
RTL_DRIVER OFFSET ON
RTL_TUNE FM 96100000
RTL_DRIVER STATUS
```

Changing OFFSET while the radio is running returns BUSY. Stop first. A
successful change affects this session's subsequent starts; it is not saved
to NVS. Other dongles return UNSUPPORTED without changing the preference.
`RTL_DRIVER STATUS` adds `RTL_OFFSET_STATUS` with capability, requested state,
applied offset, physical programmed LO, effective IQ center and API result.
An idle state reports no applied LO or center. Correction is hardware;
`additional_offset_mixer=0` describes this feature, not all existing DSP.

Validation must compare OFF/ON/OFF IQ at a matched rate and RF setup, preserve
raw bytes and source/build/image hashes, check displayed and recorded center,
FM/RDS and AM reception, start/stop, rate changes and hotplug. Use
`tools/tab5_ft8_shot.py` for verified firmware PNG screenshots. Do not flash
COM17. No hardware acceptance is claimed by this source change.

