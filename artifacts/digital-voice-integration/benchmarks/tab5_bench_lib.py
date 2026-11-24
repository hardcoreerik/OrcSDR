"""Alignment-proof serial helper for benchmarks: every command collects ALL lines for a fixed window, so replies can never be mis-paired."""
import re, sys, time
from pathlib import Path
sys.path.insert(0, "F:/Ai/OrcSDR/.claude/worktrees/dmr-digital-voice-review-dde01d/tools")
from help_media import Tab5
def connect():
    c = Tab5("COM17", Path("F:/Ai/OrcSDR/.orclink/ui-doc.key")); c.authenticate(create_key=False); return c
def run(c, cmd, window=1.2):
    if not cmd.startswith("RTL_DV STATUS") and not cmd.startswith("RTL_MEM STATUS"):
        c.authenticate(create_key=False)          # the serial session's authorisation lapses; refresh before every control command
    c.serial.reset_input_buffer(); c.send(cmd)
    end = time.monotonic() + window; lines = []
    while time.monotonic() < end:
        raw = c.serial.readline()
        if raw: lines.append(raw.decode("utf-8", "replace").strip())
    return lines
def kv(line): return {k: v for k, v in re.findall(r'(\w+)=("[^"]*"|\S+)', line)}
def find(lines, prefix):
    for l in lines:
        if l.startswith(prefix): return l
    return ""
def dv(c):
    L = run(c, "RTL_DV STATUS", 1.0); return find(L, "RTL_DV_UI"), kv(find(L, "RTL_DV_STATUS"))
def mem(c):
    L = run(c, "RTL_MEM STATUS", 1.0); return kv(find(L, "RTL_MEM psram")) or kv(find(L, "RTL_MEM "))
