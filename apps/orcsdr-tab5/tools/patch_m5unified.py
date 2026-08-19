"""Give M5Unified's Tab5 speaker worker enough stack for its DMA mixer.

The upstream stack formula is 1280 + dma_buf_len * sizeof(uint32_t).
Stereo mixing alloca()s about dma_buf_len * 8 bytes. At dma_buf_len=512
that overflows a 3.3 KiB stack (spk_task stack protection fault).

Do not shrink I2S DMA to paper over this. Keep 4 x 512 stereo (~8 KiB)
and raise the task stack to at least 8 KiB.

Usage (native IDF):
  python tools/patch_m5unified.py <Speaker_Class.cpp>
"""

from __future__ import annotations

import os
import sys

try:
    Import  # noqa: F821 - injected only by PlatformIO.
except NameError:
    env = None
else:
    Import("env")  # noqa: F821 - injected by SCons / PlatformIO

MARKER = "// ORCSDR-TAB5: speaker task stack"
OLD = """      size_t stack_size = 1280 + (_cfg.dma_buf_len * sizeof(uint32_t));
"""
NEW = """      // ORCSDR-TAB5: speaker task stack
      // Stereo mixer alloca() needs ~dma_buf_len*8. Keep I2S DMA at 4x512
      // and give the worker a real stack so ADS-B stop cannot overflow it.
      size_t stack_size = 1280 + (_cfg.dma_buf_len * sizeof(uint32_t));
#if defined(CONFIG_IDF_TARGET_ESP32P4)
      if (_cfg.stereo) {
        stack_size += _cfg.dma_buf_len * sizeof(uint32_t);
      }
      if (stack_size < 8192) { stack_size = 8192; }
#endif
"""


def patch_file(path: str) -> int:
    if not os.path.isfile(path):
        print(f"[patch_m5unified] SKIP - not found: {path}")
        return 1
    with open(path, encoding="utf-8") as source:
        text = source.read()
    if MARKER in text:
        print(f"[patch_m5unified] OK (already patched) [{path}]")
        return 0
    if text.count(OLD) != 1:
        print("[patch_m5unified] ERROR - stack anchor missing; library left unchanged")
        return 1
    with open(path, "w", encoding="utf-8") as target:
        target.write(text.replace(OLD, NEW, 1))
    print(f"[patch_m5unified] PATCHED - {path}")
    return 0


if env is None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_m5unified.py <Speaker_Class.cpp>")
    raise SystemExit(patch_file(sys.argv[1]))

path = os.path.join(
    env.subst("$PROJECT_LIBDEPS_DIR"),
    env["PIOENV"],
    "M5Unified",
    "src",
    "utility",
    "Speaker_Class.cpp",
)
patch_file(path)
