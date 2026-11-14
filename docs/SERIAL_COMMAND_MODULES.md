# Serial command modules

Serial commands do not belong in `process_command()` in `apps/orcsdr-tab5/ui/main.cpp`. Each family of commands
lives in its own module, `apps/orcsdr-tab5/ui/serial_<family>.{hpp,cpp}`, in namespace `orcsdr::serial_<family>`.
`main.cpp` keeps only a hook table and one call line per family. A new dashboard, decoder or tool that adds serial
commands follows this page.

## The pattern

- **Signature (exactly this line):** `bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {`.
  It returns `true` when it handled the line and `false` to let the dispatcher carry on.
- **No device headers.** The module includes no ESP-IDF, Arduino, M5 or UI header and holds no state (no statics, no
  heap, no `std::function`). It builds on a Linux host with `g++ -std=c++17 -Wall -Wextra -Werror`.
- **Hooks are plain function pointers and constant data** in `struct Hooks`, set in `main.cpp` as
  `static constexpr orcsdr::serial_<family>::Hooks kSerial<Family>Hooks{...};` directly above `process_command`. Hook
  bodies stay thin: a call into an existing function or a read of existing state. They land in flash, not RAM.
- **One call line** at the position of the family's first route:
  `if (orcsdr::serial_<family>::handle_command(command, authenticated, kSerial<Family>Hooks)) return;`
- **Same bytes on the wire.** `Serial` is an `OrcConsole`: `printf` is one write from a 2048-byte buffer, `println(text)`
  writes the text and then `'\n'` as a second write, `print` is one write. Modules use `say(hooks, fmt, ...)` (format,
  then one `hooks.write`), `hooks.write_line` and `hooks.write` to match. Print a `uint32_t` with `%u` only through
  `static_cast<unsigned>(...)`: the device compiler treats `uint32_t` as `unsigned long` and fails the firmware build,
  which the host test cannot see.
- **Authentication stays in the module**, in the same place as before (refusal first, then no side effects).

## What guards it

| Guard | What it catches |
|---|---|
| `tools/serial_command_routes.py --check` | A route added, removed, reordered, retyped or given a different login rule. Register the namespace in `FAMILIES`; run `--write` or `--golden` **only** when the routing change is intended |
| `tests/data/serial_command_routes.tsv`, `tests/data/serial_golden_unauthenticated.tsv` | The recorded routes and the refusal each login-gated command gives |
| `tools/tab5_serial_golden.py --self-check` (CI) and `--port COMx` (device) | The refusals really come back from a unit. Opens the port without resetting it |
| `tests/serial_<family>_tests.cpp` | Every reply, refusal, bad-argument form and near-miss line, with fake hooks that record each write |
| `tools/test-tab5-cadence-history.sh` and `.github/workflows/tab5-cadence-history-tests.yml` | The host tests in CI. Add the new test to both (script line and workflow `paths`) |
| `tools/tab5_function_frames.py <elf> --max-bytes 1024 "::process_command(char*)"` | `process_command()` growing a big stack frame. As it shrinks, GCC may inline large helpers into it and reserve their locals for every command: on 2026-11-14 that took the main task from 6,336 to 2,296 bytes free. Run it after each firmware build that moves routes; mark the culprit `__attribute__((noinline))` |

Also required for every change: a `CHANGELOG.md` entry, and a **firmware build** (`apps/orcsdr-tab5/tools/build-tab5-idf.ps1
-WithoutC6`) before calling it done.

## Adding a command for a new dashboard

1. Put the dashboard's own serial commands in `serial_<dashboard>.{hpp,cpp}` with a host test, as above.
2. To make `RTL_UI OPEN <NAME>` open it: add the name and a `Target` value in `serial_ui_nav.{hpp,cpp}`, a `case` in the
   `open` hook in `main.cpp`, and extend `tests/serial_ui_nav_tests.cpp`. Update the `RTL_UI OPEN` help text in
   `serial_help.cpp` and both golden help files under `tests/data/` if you want it listed.
3. Add the new commands to `docs/API_SERIAL_CLI.md`.
4. Do not change the behaviour of existing screens while doing this; ask first.

## What this does and does not give you

- **Gives:** commands that are testable without a device, a dispatcher that cannot silently change, and a place to add
  new commands without growing `main.cpp`.
- **Does not:** touch drawing, receiver state, task lifetime or buffers. Those are still in `main.cpp` and are the
  later stages of the refactor (spectrum compute/render split, receiver runtime, buffer release).
- **Still in `process_command()`:** the capture and listen routes, IQ and recording, pairing and session routes, SD
  transfer, reset, the OrcDial and TCP routes and a few UI routes. They wait on separate decisions; do not move them as
  a side effect of another change.
