"""
tools/patch_m5gfx.py  —  PlatformIO pre-build extra_script
============================================================
Patches M5GFX common.cpp for ESP-IDF 5.4+ I2C NG driver compatibility.

Required for M5Stack Tab5 (ESP32-P4) with pioarduino 55.03.38-1 (IDF 5.5.x).
Prevents "Load access fault" crash in M5.begin() caused by repeated
i2c_del_master_bus + i2c_new_master_bus cycles corrupting driver state
during M5GFX display autodetect.

Wired in via platformio.ini (m5tab5_base and all envs that extend it):
    extra_scripts = pre:tools/patch_m5gfx.py

Idempotent: checks for MARKER string before applying.
Safe to run on every build — skips in ~1 ms when already patched.

If M5GFX updates and the anchors no longer match, this script will print
a warning and leave the file untouched (it never writes partial patches).
In that case: re-verify the IDF 5.4 NG driver issue still exists in the
new M5GFX version, update the OLD strings below to match, and rebuild.
"""

import os
Import("env")  # noqa: F821 — injected by SCons / PlatformIO

MARKER = "// NEONDRIVE-PATCH: IDF5.4-I2C-NG"

# ------------------------------------------------------------------------------
# Patch 1 — release():
#   Wrap the Wire.end() + pin-reset block in an IDF < 5.4 guard.
#   On IDF 5.4+, only the initialized flag is cleared; the live bus is kept.
# ------------------------------------------------------------------------------
_P1_OLD = (
    "        i2c_context[i2c_port].initialized = false;\n"
    "#if defined ( ARDUINO ) && __has_include (<Wire.h>) && defined ( ESP_IDF_VERSION_VAL )"
)
_P1_NEW = (
    "        i2c_context[i2c_port].initialized = false;\n"
    "#if defined (ESP_IDF_VERSION_VAL) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)) "
    + MARKER + "\n"
    "        // IDF 5.4+ new I2C master driver (NG): skip Wire.end() and pin reset.\n"
    "        // Repeated i2c_del_master_bus + i2c_new_master_bus cycles corrupt the\n"
    "        // driver state during M5GFX autodetect. Keep the bus alive; init()\n"
    "        // reuses it via the s_wire_init once-per-port guard.\n"
    "#else\n"
    "#if defined ( ARDUINO ) && __has_include (<Wire.h>) && defined ( ESP_IDF_VERSION_VAL )"
)

# Patch 1b — close the #else we opened above (added before the closing } of
#             the if(initialized) block in release())
_P1B_OLD = (
    "          pinMode(i2c_context[i2c_port].pin_sda, pin_mode_t::input_pullup);\n"
    "        }\n"
    "      }\n"
    "\n"
    "      return {};\n"
    "    }\n"
    "\n"
    "    cpp::result<void, error_t> setPins"
)
_P1B_NEW = (
    "          pinMode(i2c_context[i2c_port].pin_sda, pin_mode_t::input_pullup);\n"
    "        }\n"
    "#endif  // !(IDF >= 5.4)\n"
    "      }\n"
    "\n"
    "      return {};\n"
    "    }\n"
    "\n"
    "    cpp::result<void, error_t> setPins"
)

# ------------------------------------------------------------------------------
# Patch 2 — init():
#   Short-circuit the release()+reinit cycle on IDF 5.4+.
#   Instead of tearing down and recreating the bus, reuse the live handle.
# ------------------------------------------------------------------------------
_P2_OLD = (
    "      if (i2c_context[i2c_port].initialized)\n"
    "      {\n"
    "        release(i2c_port);\n"
    "      }"
)
_P2_NEW = (
    "      if (i2c_context[i2c_port].initialized)\n"
    "      {\n"
    "#if defined (ESP_IDF_VERSION_VAL) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0))\n"
    "        // ESP-IDF 5.4+ new I2C master driver: the Wire.end()+Wire.begin()\n"
    "        // cycle (i2c_del_master_bus + i2c_new_master_bus) corrupts driver\n"
    "        // state after several iterations during M5GFX autodetect.\n"
    "        // Fix: skip the cycle; re-apply pin config, save register state,\n"
    "        // and reuse the existing live bus handle.\n"
    "        auto dev = getDev(i2c_port);\n"
    "        set_pin((i2c_port_t)i2c_port, pin_sda, pin_scl);\n"
    "        i2c_context[i2c_port].save_reg(dev);\n"
    "        return {};\n"
    "#else\n"
    "        release(i2c_port);\n"
    "#endif\n"
    "      }"
)

# ------------------------------------------------------------------------------
# Patch 3 — init():
#   Call Wire.begin() exactly once per port (once-per-port static flag).
#   Since release() no longer calls Wire.end() on IDF 5.4+, the bus persists
#   across autodetect cycles and must not be re-initialised.
# ------------------------------------------------------------------------------
_P3_OLD = (
    " #if defined ( USE_TWOWIRE_SETPINS )\n"
    "      twowire->begin();\n"
    " #else\n"
    "      twowire->begin((int)pin_sda, (int)pin_scl);\n"
    " #endif"
)
_P3_NEW = (
    " #if defined (ESP_IDF_VERSION_VAL) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0))\n"
    "      // IDF 5.4+ NG driver: Wire.begin() must fire exactly once per port.\n"
    "      // release() no longer calls Wire.end(), so the bus persists; skip\n"
    "      // subsequent begin() calls via a static once-per-port flag.\n"
    "      { static bool s_wire_init[I2C_NUM_MAX] = {};\n"
    "        if (!s_wire_init[i2c_port]) {\n"
    "          s_wire_init[i2c_port] = true;\n"
    "  #if defined ( USE_TWOWIRE_SETPINS )\n"
    "          twowire->begin();\n"
    "  #else\n"
    "          twowire->begin((int)pin_sda, (int)pin_scl);\n"
    "  #endif\n"
    "        }\n"
    "      }\n"
    " #elif defined ( USE_TWOWIRE_SETPINS )\n"
    "      twowire->begin();\n"
    " #else\n"
    "      twowire->begin((int)pin_sda, (int)pin_scl);\n"
    " #endif"
)


def _replace_once(src, old, new, label):
    """Replace old→new exactly once. Returns (new_src, success)."""
    count = src.count(old)
    if count == 0:
        print(f"[patch_m5gfx] WARN: anchor '{label}' not found "
              f"— M5GFX version may have changed; update patch anchors.")
        return src, False
    if count > 1:
        print(f"[patch_m5gfx] WARN: anchor '{label}' found {count} times "
              f"— skipping to avoid unintended changes.")
        return src, False
    return src.replace(old, new, 1), True


def patch_file(filepath):
    if not os.path.isfile(filepath):
        print(f"[patch_m5gfx] SKIP — not found: {filepath}")
        print(f"[patch_m5gfx]   Run 'pio pkg install' first, then rebuild.")
        return

    with open(filepath, "r", encoding="utf-8") as f:
        src = f.read()

    # Short-circuit: already patched
    if MARKER in src:
        env_name = env["PIOENV"]
        print(f"[patch_m5gfx] OK (already patched) [{env_name}]")
        return

    # Apply all patches; abort the write if any anchor is missing
    patched = src
    patched, ok1  = _replace_once(patched, _P1_OLD,  _P1_NEW,  "release-open")
    patched, ok1b = _replace_once(patched, _P1B_OLD, _P1B_NEW, "release-close")
    patched, ok2  = _replace_once(patched, _P2_OLD,  _P2_NEW,  "init-already")
    patched, ok3  = _replace_once(patched, _P3_OLD,  _P3_NEW,  "init-wire-begin")

    if ok1 and ok1b and ok2 and ok3:
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(patched)
        env_name = env["PIOENV"]
        print(f"[patch_m5gfx] PATCHED — {filepath} [{env_name}]")
    else:
        print(f"[patch_m5gfx] ERROR — one or more anchors missing; file NOT modified.")
        print(f"[patch_m5gfx]   See tools/patch_m5gfx.py and CLAUDE.md for details.")


# ── Entry point ───────────────────────────────────────────────────────────────
libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
pio_env     = env["PIOENV"]
common_cpp  = os.path.join(
    libdeps_dir, pio_env, "M5GFX",
    "src", "lgfx", "v1", "platforms", "esp32", "common.cpp"
)
patch_file(common_cpp)
