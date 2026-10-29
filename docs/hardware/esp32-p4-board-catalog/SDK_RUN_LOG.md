# SDK and RSA run log — evidence of what was and was not executed

**Date:** 2026-10-28 (web-chat). **Host environment:** isolated Linux container, not NewcorePC Windows. `F:\AI\RSA-Sandbox` not mounted here; GitHub connector can read remote files, but it does not execute PowerShell or clone the user's private local RSA state. Network access for shell git clone is blocked by DNS, and `idf.py`, `rea` not detected. Therefore all Windows-targeted actions below are **NOT RUN**. No fake output or exit code is supplied.

| Operation | Status | Result |
|---|---|---|
| Read RSA `AGENTS.md`, README | COMPLETE via authenticated GitHub connector | Source commit checked, not local working tree |
| Read REA `AGENTS.md`, INDEX/index.json/graph | COMPLETE via authenticated GitHub connector | Existing Oct 16 Olimex target found |
| Read OrcSDR research README + local-session prompt | COMPLETE via authenticated GitHub connector | Missing 3 linked docs; branch SHA checked |
| Pin vendor GitHub HEADs | COMPLETE remote API reads | Exact commit IDs in JSON (not local imports) |
| Inspect selected Espressif/LilyGO/Elecrow source | COMPLETE remote file reads | Pinned permalinks and claims in evidence register |
| `python rsa.py doctor` | NOT RUN | Windows RSA checkout inaccessible |
| `python rsa.py list` | NOT RUN | Windows RSA checkout inaccessible |
| `python rsa.py import ...` | NOT RUN (0 imports) | No RSA target directories generated |
| `python rsa.py analyze ...` | NOT RUN (0 analyses) | No RSA metrics or fresh REA results generated |
| `idf.py set-target esp32p4` | NOT RUN | IDF toolchain absent; no `sdkconfig` generated |
| `idf.py reconfigure` | NOT RUN | No successful configuration |
| `idf.py build` | NOT RUN (0 builds) | No ELF, binaries, build logs, or exit code |
| `python rsa.py publish --dry-run` | NOT RUN | No new RSA committed outputs to publish |
| `python rsa.py publish --push` | NOT RUN | REA-Results unchanged; user has not authorized any merge |
| Actual C6/USB/DSI hardware tests | NOT RUN (0) | No powered or flashed boards |

## Local session command sequence (run only in authorized RSA checkout)

```powershell
Set-Location F:\AI\RSA-Sandbox
python rsa.py doctor
python rsa.py list
python rsa.py import github:espressif/esp-bsp@9112bd334726368d4aa4421dd01f2e4b1a739568 --name esp-bsp-p4-catalog
python rsa.py analyze esp-bsp-p4-catalog
python rsa.py import github:espressif/esp-hosted-mcu@78b1c1c43c02b0c4e898cf87f94679278b1ddaff --name esp-hosted-mcu-p4-catalog
python rsa.py analyze esp-hosted-mcu-p4-catalog
# Review imported metrics, notes, licenses and target.json receipts; copy/rebuild SDK sample under RSA as allowed.
# Local git commit within RSA is required before publish, which uses only tracked HEAD.
python rsa.py publish --dry-run
# Verify private destination, changes, secret scan and approvals before permitted publish.
```

## Required append-only entry for each future build

`TIMESTAMP | operator | source repository@SHA | source license | target slug | exact example path | esp-idf SHA/version | Python/compiler/CMake/Ninja versions | sdkconfig SHA | dependencies.lock SHA | command | exit code | stdout/stderr path | ELF SHA256 | hardware revision (if tested) | result | caveats`.

Record both vendor-IDF baseline and OrcSDR-IDF 5.5.4 results **without overwriting each other**. `NOT RUN` is distinct from `BUILD FAIL` or `UNSUPPORTED SDK`. Raw vendor firmware or source and build artifacts must remain in ignored RSA locations and never be pushed into REA-Results.