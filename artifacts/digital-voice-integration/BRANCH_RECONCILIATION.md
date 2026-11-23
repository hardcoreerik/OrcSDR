# Branch reconciliation (G0)

Trial merge `origin/main` into the DV branch: 4 content conflicts, all append-only ID lists.
Files changed on both sides that auto-merged: `apps/orcsdr-tab5/main/CMakeLists.txt`.

| File | Resolution |
|---|---|
| ui/dashboard_registry.hpp | main's `ais, visualizer` keep their slots; `digital_voice` appended after (persisted IDs unchanged) |
| ui/screen_controller.hpp | `... maps, ais, digital_voice` |
| ui/screen_controller.cpp | name() cases for both |
| ui/dashboard_registry.cpp | self_check expects 22 entries (21 main + digital_voice) |

Resulting delta of merged tree vs origin/main in app code is DV-only: 3 source files added to
main/CMakeLists.txt, 1 registry entry (available=false), 1 enum value each. No main.cpp change.
Line endings: these four files are CRLF; resolution was normalized back to CRLF.
Merge commit 866a380d. DV history and main history are both preserved (no rebase).

Note: `digital_voice_dashboard_demo.cpp` is not in the firmware source list (host/demo only).
