#!/usr/bin/env python3
"""Stack frame of named functions in a built Tab5 firmware, read from the disassembly.

  python tools/tab5_function_frames.py <orcsdr_tab5.elf> [--max-bytes N] "process_command(char*)" ...

Needs riscv32-esp-elf-objdump on PATH (run after export.ps1). For each function whose demangled name contains one of
the given strings, every stack-pointer decrease in its body is added up: `addi sp,sp,-N`, and the large-frame form
`lui t0,..` / `addi t0,t0,..` / `add sp,sp,t0`. Reading only the prologue is not enough: GCC can place the big
adjustment after the register saves.

Why: when serial families moved out of process_command() it became small enough for GCC to inline ui_doc_enter() and
ui_doc_render() into it, which reserved about 6 KB for every serial command and cut the main task's free stack from
6,336 to 2,296 bytes (2026-11-14). With --max-bytes the tool fails when any listed function's frame is larger.
"""
import argparse
import re
import subprocess
import sys

FUNC = re.compile(r"^[0-9a-f]+ <(.+)>:$")
# Some objdump builds print the immediate form as `add sp,sp,-480` instead of `addi`: accept both, or every frame
# reads 0 and the guard passes silently (seen 2026-11-14).
ADDI_SP = re.compile(r"\baddi?\s+sp,sp,(-?\d+)\b")
LUI = re.compile(r"\blui\s+(\w+),(0x[0-9a-f]+|\d+)")
ADDI_REG = re.compile(r"\baddi?\s+(\w+),(\w+),(-?\d+)\b")
ADD_SP = re.compile(r"\b(add|sub)\s+sp,sp,(\w+)")


def to_signed32(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & 0x80000000 else value


def frames(elf, wanted):
    dump = subprocess.run(["riscv32-esp-elf-objdump", "-d", "-C", elf], capture_output=True, text=True,
                          errors="replace", check=True).stdout
    result = {}
    name = None
    regs = {}
    for line in dump.splitlines():
        m = FUNC.match(line.strip())
        if m:
            name = m.group(1) if any(w in m.group(1) for w in wanted) else None
            regs = {}
            if name:
                result[name] = 0
            continue
        if name is None:
            continue
        a = ADDI_SP.search(line)
        if a:
            if int(a.group(1)) < 0:
                result[name] += -int(a.group(1))
            continue
        lui = LUI.search(line)
        if lui:
            regs[lui.group(1)] = to_signed32(int(lui.group(2), 0) << 12)
            continue
        ar = ADDI_REG.search(line)
        if ar and ar.group(2) in regs and ar.group(1) == ar.group(2):
            regs[ar.group(1)] = to_signed32(regs[ar.group(1)] + int(ar.group(3)))
            continue
        s = ADD_SP.search(line)
        if s and s.group(2) in regs:
            delta = regs[s.group(2)] if s.group(1) == "add" else -regs[s.group(2)]
            if delta < 0:
                result[name] += -delta
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("elf")
    parser.add_argument("names", nargs="+")
    parser.add_argument("--max-bytes", type=int, default=0)
    args = parser.parse_args()
    found = frames(args.elf, args.names)
    if not found:
        print("no matching function")
        return 2
    if not any(found.values()):
        print("FAIL: every frame read 0 bytes; the disassembly format was not recognised")
        return 1
    worst = 0
    for name, size in sorted(found.items(), key=lambda kv: -kv[1]):
        print(f"{size:7d}  {name}")
        worst = max(worst, size)
    if args.max_bytes and worst > args.max_bytes:
        print(f"FAIL: {worst} bytes is over the {args.max_bytes}-byte limit")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
