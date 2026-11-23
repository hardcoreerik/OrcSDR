# Digital Voice UI-DV0: host contract tests

From OrcSDR repository root:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -Itests/digital_voice_dashboard_preview/mock -Iapps/orcsdr-tab5/ui \
  apps/orcsdr-tab5/ui/digital_voice_dashboard.cpp \
  apps/orcsdr-tab5/ui/digital_voice_dashboard_model.cpp \
  apps/orcsdr-tab5/ui/digital_voice_dashboard_demo.cpp \
  tests/digital_voice_dashboard_preview/dashboard_host_contract_tests.cpp \
  -o dv_host_contract
./dv_host_contract
```

For sanitizers add `-fsanitize=address,undefined -fno-omit-frame-pointer -g` to the compile command and run the binary.

Validated in sandbox with GCC C++17 Werror and ASan/UBSan: PASS. Cases include six views × two densities, bounds/header ownership, overlay prevention, no idle redraw/full-screen clear, safe DMR unknown-timeslot, no premature identity, replay evidence, explicit scanner-disabled no-action state and scanner-progress + action when the snapshot advertises support.

`mock/M5Unified.h` records primitives only. It is **not M5GFX pixel rendering**. No real firmware compile, Tab5 flash, screen capture, scan integration, radio reception or physical tests were performed. See `docs/digital-voice/ui-dv0/SCANNER_FIRST_WORKFLOW.md`.
