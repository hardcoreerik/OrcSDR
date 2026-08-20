"""Make ESP-Hosted 2.12.6 drop a TX packet when its SDIO buffer is unavailable.

The upstream write task already has a null-buffer error path.  Its preceding
assert turns that recoverable condition into a device reboot under memory
pressure.
"""

from __future__ import annotations

import os
import sys


MARKER = "/* ORCSDR-TAB5: SDIO TX allocation may fail under load. */"
OLD = """\t\t\tsendbuf = sdio_buffer_alloc(MEMSET_REQUIRED);\n\t\t\tassert(sendbuf);\n\t\t\tfree_func = sdio_buffer_free;\n"""
NEW = """\t\t\tsendbuf = sdio_buffer_alloc(MEMSET_REQUIRED);\n\t\t\t/* ORCSDR-TAB5: SDIO TX allocation may fail under load. */\n\t\t\tfree_func = sdio_buffer_free;\n"""


def patch_file(path: str) -> int:
    if not os.path.isfile(path):
        print(f"[patch_esp_hosted] ERROR - not found: {path}")
        return 1
    with open(path, encoding="utf-8") as source:
        text = source.read()
    if MARKER in text:
        print(f"[patch_esp_hosted] OK (already patched) [{path}]")
        return 0
    if text.count(OLD) != 1:
        print("[patch_esp_hosted] ERROR - TX allocation anchor missing; source left unchanged")
        return 1
    with open(path, "w", encoding="utf-8") as target:
        target.write(text.replace(OLD, NEW, 1))
    print(f"[patch_esp_hosted] PATCHED - {path}")
    return 0


if len(sys.argv) != 2:
    raise SystemExit("usage: patch_esp_hosted.py <sdio_drv.c>")
raise SystemExit(patch_file(sys.argv[1]))
