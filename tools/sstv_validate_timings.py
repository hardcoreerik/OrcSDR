#!/usr/bin/env python3
"""Independent wire-schedule audit of OrcSDR SSTV mode constants.

Compare the C++ receive table against published VIS/schedule facts, expressed
here as segment durations rather than copying another implementation's code.

References:
 * F4HWN (Armel), UV-K1/K5 V3 Labs SSTV mode generator, Apache-2.0:
   https://github.com/armel/uv-k1-k5v3-firmware-custom/blob/main/App/apps/sstv/test/modes.py
 * slowrx modespec.c (Oona Räisänen), ISC
 * Dayton SSTV standard mode tables (JL Barber, 2000).
"""
from decimal import Decimal
from pathlib import Path
import re
import sys

D = Decimal
ROOT = Path(__file__).resolve().parents[1]
MODES_CPP = ROOT / "apps/orcsdr-tab5/ui/sstv_modes.cpp"

# Expected: VIS, width, height, transmitted line milliseconds, sync ms,
# visible luminance scan duration ms, family. Values from the reference
# schedules, NOT from the code we are testing.
expected = {
    "robot36":    (8, 320, 240, D("150"), D("9"), D("88"), 0),
    "robot72":    (12, 320, 240, D("300"), D("9"), D("138"), 0),
    "martin_m1":  (44,320,256, D("4.862")+D("0.572")*4+D("146.432")*3, D("4.862"), D("146.432"),1),
    "martin_m2":  (40,320,256, D("4.862")+D("0.572")*4+D("73.216")*3, D("4.862"), D("73.216"),1),
    "scottie_s1": (60,320,256, D("9")+D("1.5")*3+D("138.24")*3, D("9"), D("138.24"),2),
    "scottie_s2": (56,320,256, D("9")+D("1.5")*3+D("88.064")*3, D("9"), D("88.064"),2),
    "scottie_dx": (76,320,256, D("9")+D("1.5")*3+D("345.6")*3, D("9"), D("345.6"),2),
    "pd50":      (93,320,256, D("22.08")+D("0.286")*320*4, D("20"), D("0.286")*320,3),
    "pd90":      (99,320,256, D("22.08")+D("0.532")*320*4, D("20"), D("0.532")*320,3),
    "pd120":     (95,640,496, D("22.08")+D("0.19")*640*4, D("20"), D("0.19")*640,3),
    "pd160":     (98,512,400, D("22.08")+D("0.382")*512*4, D("20"), D("0.382")*512,3),
    "pd180":     (96,640,496, D("22.08")+D("0.286")*640*4, D("20"), D("0.286")*640,3),
    "pd240":     (97,640,496, D("22.08")+D("0.382")*640*4, D("20"), D("0.382")*640,3),
    "pd290":     (94,800,616, D("22.08")+D("0.286")*800*4, D("20"), D("0.286")*800,3),
}

# ModeSpec field order: id,name,vis,width,height,line_us,sync_us,porch_us,sep_us,pixel_ns,family
pattern = re.compile(
    r'\{Mode::(\w+),\s*"[^"]+",\s*(\d+),\s*(\d+),\s*(\d+),'
    r'\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}'
)

def audit() -> list[str]:
    errors: list[str] = []
    entries = {m[0]: tuple(int(n) for n in m[1:]) for m in pattern.findall(MODES_CPP.read_text())}
    if set(entries) != set(expected):
        errors.append(f"mode set mismatch: missing={set(expected)-set(entries)} extra={set(entries)-set(expected)}")
    for mode, (vis,width,height,period,sync,scan,family) in expected.items():
        if mode not in entries:
            continue
        got_vis,got_w,got_h,line_us,sync_us,porch_us,sep_us,pixel_ns,got_fam = entries[mode]
        want = {
            "VIS": (got_vis,vis), "width": (got_w,width), "height":(got_h,height),
            "line_us":(line_us,int(period*1000)), "sync_us":(sync_us,int(sync*1000)),
            "pixel_ns":(pixel_ns,int(scan*D(1000000)/width)),
            "family":(got_fam,family),
        }
        for field,(actual,ref) in want.items():
            if actual != ref:
                errors.append(f"{mode}: {field}={actual} expected={ref}")
        if family==3 and porch_us!=2080:
            errors.append(f"{mode}: PD porch {porch_us}, expected 2080us")
    return errors

if __name__ == "__main__":
    problems = audit()
    for problem in problems:
        print("SSTV MODE AUDIT FAIL:",problem,file=sys.stderr)
    if problems:
        sys.exit(1)
    print("SSTV MODE AUDIT PASS: 14 VIS/size/period/sync/dwell/family schedules")
