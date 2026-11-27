"""Experimental FR/EM staging based on FCC's 2018 dictionary, not current schema."""
import argparse, json, re, sqlite3
from decimal import Decimal
from pathlib import Path

SCHEMA='''
CREATE TABLE fr (k TEXT PRIMARY KEY, source_key TEXT NOT NULL, low_raw TEXT, upper_raw TEXT, lo_hz TEXT, hi_hz TEXT, raw TEXT NOT NULL);
CREATE TABLE em (k TEXT NOT NULL, seq TEXT NOT NULL, code TEXT, assigned_raw TEXT, raw TEXT NOT NULL, PRIMARY KEY(k,seq));
'''

def natural(fields):
    # Length framing via JSON array; blank keys are never joinable.
    if any(not re.fullmatch(r'[0-9]{1,9}',x) for x in fields): raise ValueError('missing/invalid numeric source key')
    return json.dumps([int(x) for x in fields],separators=(',',':'))

def mhz(s):
    if not re.fullmatch(r'[0-9]{1,8}(?:\.[0-9]{1,8})?',s): raise ValueError('invalid MHz field')
    return Decimal(s)*1000000

def stage(db, fr_lines, em_lines):
    db.executescript(SCHEMA)
    counts=dict(fr=0,em=0,unindexed=0,orphan_em=0,frequency_conflict=0)
    with db:
        for line in fr_lines:
            a=line.rstrip('\r\n').split('|')
            if len(a)!=30 or a[0]!='FR': raise ValueError('FR schema mismatch')
            k=natural([a[i].strip() for i in (1,6,7,26)])
            lo=mhz(a[10].strip()) if a[10].strip() else None
            hi=mhz(a[11].strip()) if a[11].strip() else lo
            if lo is None or hi is None or lo<=0 or hi<lo or hi>2**64-1 or any(x!=x.to_integral_value() for x in (lo,hi)):
                lh=hh=None;counts['unindexed']+=1
            else: lh,hh=str(int(lo)),str(int(hi))
            db.execute('INSERT INTO fr VALUES (?,?,?,?,?,?,?)',(k,a[1],a[10],a[11],lh,hh,json.dumps(a,ensure_ascii=False)))
            counts['fr']+=1
        for line in em_lines:
            a=line.rstrip('\r\n').split('|')
            if len(a)!=16 or a[0]!='EM': raise ValueError('EM schema mismatch')
            k=natural([a[i].strip() for i in (1,5,6,12)])
            seq=natural([a[15].strip()])
            db.execute('INSERT INTO em VALUES (?,?,?,?,?)',(k,seq,a[9],a[7],json.dumps(a,ensure_ascii=False)))
            counts['em']+=1
        for emraw,frraw in db.execute('SELECT em.assigned_raw,fr.low_raw FROM em LEFT JOIN fr USING(k)'):
            if frraw is None: counts['orphan_em']+=1
            elif not emraw.strip() or not frraw.strip() or mhz(emraw.strip())!=mhz(frraw.strip()): counts['frequency_conflict']+=1
    return counts

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('fr');p.add_argument('em');p.add_argument('database');p.add_argument('--encoding',default='utf-8');a=p.parse_args()
    path=Path(a.database)
    with path.open('xb'): pass
    try:
        with sqlite3.connect(path) as db,open(a.fr,encoding=a.encoding) as fr,open(a.em,encoding=a.encoding) as em:
            print(json.dumps(stage(db,fr,em),sort_keys=True))
    except BaseException:
        path.unlink(missing_ok=True);raise
if __name__=='__main__': main()
