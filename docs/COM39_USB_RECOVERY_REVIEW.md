# Tab5 USB root-port recovery review

This draft patch addresses two captured ESP-IDF 5.5.4 USB failures during receiver disconnect and enumeration recovery. The missing-node path returns `ESP_ERR_NOT_FOUND` after a node has already been removed. The power-off path can return an error when the controller has a pending event, while the hub asserts that every power-off succeeds.

The patch retains recovery for an absent node, serializes root-port commands with root-port event processing, and publishes the off state only after a successful command or a confirmed already-off controller. A controller in recovery is left for the host event task. A rejected stop returns an error so the driver's existing retry can run. USB uninstall propagates a stop error before freeing the stack.

## Review and integration boundary

The draft contains a patch against stock ESP-IDF 5.5.4 and a source-extracted regression harness. It is based on current OrcSDR main. Applying the patch is an explicit developer step. Normal OrcSDR builds do not apply it automatically yet. Build integration and validation against current main are required before this draft can merge.

The physical test used OrcSDR base `12f1cad15adf2b4171a58b8687b0d1c8b6512aa9`, driver 0.9.5 with interrupted-start fix `b3ab77e`, and a project-local copy of the patched USB component. Its ELF SHA-256 was `3cdaa1074f8f38ffb0c053994cf80cc4c3bf54868741dc418ef7c5456164066b`. The complete lab firmware compiled on ESP-IDF 5.5.4 and was flashed to COM39.

Five user-confirmed XTR unplug/reconnect cycles automatically resumed IQ near 2.40 MS/s, with zero driver overruns or drops and continuous uptime during the captures. The user reports a loose dongle connection. An earlier watchdog reset and a receiver loss outside capture remain unattributed. The physical traces include root-port power retries but do not prove that the rejected power-off branch executed. That branch is exercised by the host regression. Driver release 0.9.6 remains on hold.

## Reproduce the focused regression

Use a stock ESP-IDF 5.5.4 checkout. Copy its USB component to a disposable folder outside the repository and preserve the baseline before applying the patch. The shared SDK should remain available for comparison. Example commands use POSIX shell syntax.

```sh
cp -R "$IDF_PATH/components/usb" /tmp/orcsdr-usb-review
python tools/test_usb_root_recovery.py --baseline --source /tmp/orcsdr-usb-review --output /tmp/orcsdr-usb-check
(cd /tmp/orcsdr-usb-review && git apply --check /absolute/path/to/OrcSDR/apps/orcsdr-tab5/tools/patches/esp-idf-5.5.4-usb-root-recovery.patch)
(cd /tmp/orcsdr-usb-review && git apply /absolute/path/to/OrcSDR/apps/orcsdr-tab5/tools/patches/esp-idf-5.5.4-usb-root-recovery.patch)
python tools/test_usb_root_recovery.py --source /tmp/orcsdr-usb-review --output /tmp/orcsdr-usb-check
cc -std=c17 /tmp/orcsdr-usb-check/candidate.c -o /tmp/orcsdr-usb-check/candidate
cc -std=c17 -DBASELINE /tmp/orcsdr-usb-check/baseline.c -o /tmp/orcsdr-usb-check/baseline
/tmp/orcsdr-usb-check/candidate
/tmp/orcsdr-usb-check/baseline
```

Candidate must exit successfully. Baseline must abort at `ret == ESP_OK`. On Windows, the verified compile commands from an MSVC developer shell were `cl /nologo /std:c17 /W3 /WX candidate.c /Fe:candidate.exe` and `cl /nologo /std:c17 /W3 /DBASELINE baseline.c /Fe:baseline.exe`. The Windows compile and run check passed on the clean review worktree. POSIX commands above are provided for reproduction and have not been run in this session.

The harness extracts the actual changed functions and models a disconnect arriving just before power-off. It checks preserved hub state after rejection, event recovery, a later successful power cycle, already-off handling, and missing-node recovery. Semaphore mocks check lock ownership but do not model real task scheduling. Mutex lifetime, lock ordering, callback reentrancy, allocation cleanup and races between controller interrupts and recovery deserve particular review attention. Hardware validation with an external hub remains open.

## Source and licensing

The patch modifies Espressif's Apache-2.0 USB host code and retains its source attribution. It was developed from the local ESP-IDF 5.5.4 source. The earlier missing-node investigation consulted Espressif's report at https://github.com/espressif/esp-idf/issues/18366 and esp-usb PR https://github.com/espressif/esp-usb/pull/485. The accompanying regression harness is independently authored. Private crash dumps and serial captures are retained locally.
