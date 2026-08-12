"""Give M5Unified's Tab5 speaker worker enough stack for its DMA mixer."""

import os
Import("env")  # noqa: F821 - injected by PlatformIO

MARKER = "// ORCSDR-TAB5: speaker task stack"
OLD = """      size_t stack_size = 1280 + (_cfg.dma_buf_len * sizeof(uint32_t));
"""
NEW = """      // ORCSDR-TAB5: speaker task stack
      // The Tab5 mixer allocates one DMA block with alloca() before entering
      // M5Unified's normal mixing path.  The upstream formula leaves no
      // call-stack margin on ESP32-P4 and trips stack protection.
      size_t stack_size = 1280 + (_cfg.dma_buf_len * sizeof(uint32_t));
#if defined(CONFIG_IDF_TARGET_ESP32P4)
      if (stack_size < 8192) { stack_size = 8192; }
#endif
"""

path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"],
                    "M5Unified", "src", "utility", "Speaker_Class.cpp")
if not os.path.isfile(path):
    print(f"[patch_m5unified] SKIP - not found: {path}")
elif MARKER in open(path, encoding="utf-8").read():
    print(f"[patch_m5unified] OK (already patched) [{env['PIOENV']}]")
else:
    with open(path, encoding="utf-8") as source:
        text = source.read()
    if text.count(OLD) != 1:
        print("[patch_m5unified] ERROR - stack anchor missing; library left unchanged")
    else:
        with open(path, "w", encoding="utf-8") as target:
            target.write(text.replace(OLD, NEW, 1))
        print(f"[patch_m5unified] PATCHED - {path} [{env['PIOENV']}]")
