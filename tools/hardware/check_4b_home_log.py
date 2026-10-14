#!/usr/bin/env python3
"""Gate Waveshare 4B *measured* IQ stream logs, not graphics correctness.
Usage: python tools/hardware/check_4b_home_log.py --log ./orcsdr-4b.log
"""
import argparse
import pathlib
import re
import sys
ROW = re.compile(r'\((\d+)\).*HOME_4B IQ hz=(\d+) sps=(\d+) consumed=(\d+) drops=(\d+)')
START = re.compile(r'HOME_4B RF START hz=(\d+) rate=(\d+)')
def assess(text,duration_sec=300,min_efficiency=0.95,max_added_drops=0):
    if not START.search(text):
        return False,'No RF START marker (no proven stream)'
    rows=[tuple(map(int,m.groups())) for m in ROW.finditer(text)]
    if len(rows)<max(3,duration_sec//4):
        return False,'Insufficient live IQ rows'
    if any(b[0]-a[0]>10000 for a,b in zip(rows,rows[1:])):
        return False,'IQ log has >10s telemetry gaps'
    elapsed_ms=rows[-1][0]-rows[0][0]
    if elapsed_ms<duration_sec*1000:
        return False,f'Only {elapsed_ms/1000:.1f}s sampled; need {duration_sec}s'
    warmup=rows[0][0]+15000
    steady=[r for r in rows if r[0]>=warmup]
    if not steady:
        return False,'No post-warmup records'
    threshold=int(960000*min_efficiency)
    below=[r for r in steady if r[2]<threshold]
    if below:
        return False,f'{len(below)}/{len(steady)} post-warmup rates below {threshold} SPS'
    if any(b[3]<a[3] for a,b in zip(rows,rows[1:])):
        return False,'Consumed counter reversed (possible receiver restart)'
    added=steady[-1][4]-steady[0][4]
    if added<0:
        return False,'Drops counter reversed (possible restart)'
    if added>max_added_drops:
        return False,f'Added {added} dropped blocks (limit {max_added_drops})'
    return True,(f'IQ PASS: {elapsed_ms/1000:.1f}s; min SPS '
                 f'{min(r[2] for r in steady)}; added drops {added}. '
                 'Screen and touch remain UNVERIFIED.')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--log',type=pathlib.Path,required=True)
    p.add_argument('--seconds',type=int,default=300)
    p.add_argument('--max-added-drops',type=int,default=0)
    args=p.parse_args()
    if not args.log.is_file():
        p.error(f'Log not found: {args.log}')
    ok,msg=assess(args.log.read_text(errors='replace'),args.seconds,
                  max_added_drops=args.max_added_drops)
    print(msg)
    return 0 if ok else 1
if __name__=='__main__':sys.exit(main())
