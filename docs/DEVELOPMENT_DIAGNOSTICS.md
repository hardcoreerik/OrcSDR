# OrcSDR development diagnostics

Apply this standard to every firmware application and board/profile. Source,
build, flashing, serial evidence and physical acceptance are separate claims.

## Every hardware development run

1. Record profile, source commit/dirty state, SDK, exact ELF SHA-256, config,
   peripheral firmware, power source, antennas and workload/duration.
2. Use one serial owner. Open without resetting or clearing unread input. Close
   the owner before flashing, screenshots or a different serial tool.
3. Save `RTL_HEALTH`, `RTL_HEALTH BUILD`, `RTL_MEM STATUS` and relevant receiver,
   audio, Wi-Fi and storage status before/after. Compare drops/underruns within
   the same boot. Record orientation and physical symptoms separately.
4. Preserve exact ELF/map/sdkconfig/partition table before rebuilding. Compare
   device `RTL_HEALTH_BUILD elf_sha256` with the saved ELF hash. A build directory
   is not proof that its firmware is installed.

Both native applications share `components/orcsdr_diagnostics`:

| Command | Evidence and cost |
| --- | --- |
| `RTL_HEALTH` or `RTL_HEALTH STATUS` | Uptime, numeric/named reset reason, task count, caller stack minimum and internal/PSRAM/DMA memory. No allocations or task scans. |
| `RTL_HEALTH BUILD` | Project/version, SDK, full ELF SHA-256 and configured dump destination. Does not claim a dump exists. |
| `RTL_HEALTH HEAP_CHECK` | Authenticated full heap integrity scan, with STARTED/result markers. May stall work; only use during controlled diagnosis. |

Legacy `main_stack_hwm` remains compatible but measures the **calling task**, like
`caller_stack_hwm`, not necessarily app_main. ESP-IDF watermarks are bytes.
Minimum free/largest internal and DMA blocks matter even with plentiful PSRAM.
Health commands do not emit credentials, SSIDs, location or packet contents.

## Capture and decode

From the repository root, with Python 3.11+ and pyserial:

```powershell
python tools/development_capture.py --port COM32 --baud 921600 --build-dir .local/build/lilygo-orcsdr --seconds 300
```

The tool preserves build artifacts/hash/source identity and raw serial bytes in
ignored `.local/diagnostics/<UTC>/`. It sets DTR/RTS false before opening and never
clears input, authenticates, retunes or transmits RF. It sends read-only BUILD and
HEALTH queries every 30 seconds, after delimiting any partial console line. The 32 MiB default bound stops capture
explicitly rather than silently rotating away a panic; use `--max-mib` to change
it. Interrupted, disconnected and size-limited captures are marked incomplete.
Restart capture after USB re-enumerates; there is no automatic reconnect. The
tool is not a symbol decoder and must not compete with another port owner.

For live address decoding, use the existing IDF monitor and its logging toggle
(Ctrl-T then Ctrl-L), in an exported IDF shell:

```powershell
idf.py -C apps/orcsdr-p4-hosyond-console -B .local/build/lilygo-orcsdr -p COM32 monitor --no-reset --timestamps
idf.py -C apps/orcsdr-p4-hosyond-console -B .local/build/lilygo-orcsdr -p COM32 coredump-info
```

Core dump retrieval can reset a device depending on transport/tool. Capture live
evidence first. Use matching saved symbols, never a different rebuilt ELF.
COM32 is this LilyGO; Tab5 commonly uses COM17. Ports are not profile constants.
For a saved ELF-format dump, decode entirely offline with the archived symbols:

```powershell
python -m esp_coredump --chip esp32p4 info_corefile --core .local/diagnostics/RUN/crash.elf --core-format elf .local/diagnostics/RUN/orcsdr_p4_hosyond_console.elf
```

## Crash capture and deeper checks

Both native app defaults enable ELF flash core dumps, SHA-256, a separate 4096-byte
dump stack and up to 32 tasks. Tab5 already had this baseline. The shared P4 table
appends 256 KiB at `0x710000`, retaining existing NVS/PHY/application offsets and
sizes. Existing P4 installations need the new partition table **and** application.
Never erase NVS or move existing regions to enable diagnostics. Check actual
layout/free flash for each new profile before flashing.

Dumps can exceed their partition or omit tasks beyond the cap. Power failures,
PSRAM stacks/memory and hardware faults can limit capture. Do not promise a dump
for every reset. Controlled crash qualification is a separate hardware test; do
not force a panic on a working user's board merely to exercise it. Health queries
never retrieve/upload/erase dumps.

For targeted corruption diagnosis, append `config/sdkconfig.development` after
app and board defaults in a **separate** build/sdkconfig. It enables light heap
poisoning, normal compiler stack guards and FreeRTOS canaries. Existing sdkconfig
values override defaults: verify resolved settings. Measure CPU, memory,
receiver/audio continuity and redraw impact before adopting heavier checks.
Never disable watchdogs or raise timeouts to conceal a defect; preserve each
board's deliberate brownout policy.

Use existing P4 `RTL_CPU` only for targeted diagnosis (it occupies the serial
handler for two seconds). Use `tools/screenshot.py` native PNG/hash capture for UI
issues, recording-display host regressions for bounded redraws, and applicable
native ASan/UBSan tests. GDB/JTAG, heap tracing and comprehensive poisoning are
targeted escalation tools, not always-on radio workloads. Avoid unbounded logging
in hot tasks.

## After a failure

Preserve logs/dump and exact symbols before replacing firmware. Record last
operation/symptom; distinguish reset, blank screen, USB disappearance and receiver
failure. Query reset reason/uptime after reconnection. If evidence was absent,
report cause unknown. Add a focused regression for the reproduced defect, then
repeat physical visual/audio acceptance separately. Every new target documents
its crash destination, retrieval method and unqualified cases.

Raw logs, sdkconfig, dumps and screenshots can contain credentials, keys, private
messages or location. Keep them under ignored `.local/`, review excerpts and never
automatically publish/upload raw evidence.

Focused host checks: `python3 tests/development_diagnostics_test.py` (host GCC;
use WSL on Windows). This checks the actual shared C command handler, authentication,
all three configured dump destinations, capture size bounds, archived symbol
identity and both native apps' configuration/partition contracts. It does not
qualify a forced panic, dump retrieval, or another board's physical behavior.

References: [ESP-IDF fatal errors](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32p4/api-guides/fatal-errors.html),
[core dumps](https://github.com/espressif/esp-idf/blob/v5.5.4/docs/en/api-guides/core_dump.rst),
[heap debugging](https://github.com/espressif/esp-idf/blob/v5.5.4/docs/en/api-reference/system/heap_debug.rst).
