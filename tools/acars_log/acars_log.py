"""Offline ACARS JSONL ingestion and reports."""
import argparse
import csv
import datetime as dt
import json
import sqlite3
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from classify import classify

SCHEMA = '''
CREATE TABLE IF NOT EXISTS messages (id INTEGER PRIMARY KEY, v INTEGER, utc INTEGER NOT NULL, up INTEGER NOT NULL, f INTEGER NOT NULL, tail TEXT, flt TEXT, lbl TEXT, mode TEXT, ack TEXT, blk TEXT, end INTEGER, cor INTEGER, fix INTEGER, raw INTEGER, n INTEGER, x TEXT NOT NULL, p TEXT, text TEXT, format TEXT, summary TEXT, details TEXT, UNIQUE(utc,up,f,x));
CREATE TABLE IF NOT EXISTS aircraft (tail TEXT PRIMARY KEY, first_seen INTEGER, last_seen INTEGER, count INTEGER);
CREATE TABLE IF NOT EXISTS flights (flight_id TEXT, tail TEXT, date TEXT, first_seen INTEGER, last_seen INTEGER, message_count INTEGER, origin TEXT, destination TEXT, PRIMARY KEY(flight_id,tail,date));
CREATE TABLE IF NOT EXISTS positions (id INTEGER PRIMARY KEY, message_id INTEGER REFERENCES messages(id), utc INTEGER, lat REAL, lon REAL, altitude_ft REAL, speed_kt REAL, mach REAL, source TEXT);
CREATE TABLE IF NOT EXISTS events (id INTEGER PRIMARY KEY, message_id INTEGER REFERENCES messages(id), kind TEXT, details TEXT);
'''
KEYS = 'v utc up f tail flt lbl mode ack blk end cor fix raw n x p'.split()

def connect(path):
    db=sqlite3.connect(path); db.row_factory=sqlite3.Row; db.executescript(SCHEMA); return db

def date_of(utc):
    return dt.datetime.fromtimestamp(utc, dt.timezone.utc).date().isoformat() if utc else 'undated'

def timestamp(utc):
    return dt.datetime.fromtimestamp(utc, dt.timezone.utc).strftime('%Y-%m-%d %H:%M:%SZ') if utc else 'undated'

def ingest(db, paths):
    files=[]
    for path in paths:
        p=Path(path)
        files.extend(sorted(p.glob('*.jsonl')) if p.is_dir() else [p])
    added=0
    with db:
        for path in files:
            with path.open(encoding='utf-8') as stream:
                for lineno,line in enumerate(stream,1):
                    try:
                        obj=json.loads(line); obj['utc']=int(obj.get('utc') or 0)
                        for key in ('up','f','x'):
                            if key not in obj: raise ValueError('missing '+key)
                        raw=bytes.fromhex(obj['x']); obj['x']=raw.hex().upper()
                        text=raw.decode('latin-1'); decoded=classify(obj.get('lbl',''),text,obj.get('tail',''))
                        vals=[obj.get(k) for k in KEYS]+[text,decoded['format'],decoded['summary'],json.dumps(decoded)]
                        cur=db.execute('INSERT OR IGNORE INTO messages ('+','.join(KEYS)+',text,format,summary,details) VALUES ('+','.join('?' for _ in vals)+')',vals)
                        if not cur.rowcount: continue
                        mid=cur.lastrowid; added+=1
                        for p in decoded['positions']:
                            db.execute('INSERT INTO positions(message_id,utc,lat,lon,altitude_ft,speed_kt,mach,source) VALUES(?,?,?,?,?,?,?,?)',(mid,obj['utc'],p['lat'],p['lon'],p['altitude_ft'],p['speed_kt'],p['mach'],p['source']))
                        for kind,details in decoded['events']:
                            db.execute('INSERT INTO events(message_id,kind,details) VALUES(?,?,?)',(mid,kind,json.dumps(details)))
                    except (ValueError, TypeError, KeyError, json.JSONDecodeError) as e:
                        raise ValueError(f'{path}:{lineno}: {e}') from e
        db.execute('DELETE FROM aircraft'); db.execute('DELETE FROM flights')
        db.execute("INSERT INTO aircraft SELECT tail, MIN(NULLIF(utc,0)), MAX(NULLIF(utc,0)), COUNT(*) FROM messages WHERE COALESCE(tail,'')<>'' AND lbl<>'AV' GROUP BY tail")
        db.execute("INSERT INTO flights SELECT flt, COALESCE(tail,''), CASE WHEN utc=0 THEN 'undated' ELSE date(utc,'unixepoch') END, MIN(NULLIF(utc,0)), MAX(NULLIF(utc,0)), COUNT(*), NULL, NULL FROM messages WHERE COALESCE(flt,'')<>'' GROUP BY flt,COALESCE(tail,''),CASE WHEN utc=0 THEN 'undated' ELSE date(utc,'unixepoch') END")
        for row in db.execute("SELECT flt,COALESCE(tail,'') tail,utc,details FROM messages WHERE format IN ('Southwest DFB header','label 33 position') AND COALESCE(flt,'')<>'' ORDER BY utc,id").fetchall():
            d=json.loads(row['details'])
            if d.get('origin') and d.get('destination'):
                db.execute('UPDATE flights SET origin=?,destination=? WHERE flight_id=? AND tail=? AND date=?',(d['origin'],d['destination'],row['flt'],row['tail'],date_of(row['utc'])))
    return added

def safe(s):
    return str(s or '').replace('|','\\|').replace('\r',' ').replace('\n',' ').replace('\x00',' ').replace('`', "'")

def report(db, kind, value, date=None):
    if kind=='day':
        dt.date.fromisoformat(value)
        rows=db.execute("SELECT * FROM messages WHERE utc>0 AND date(utc,'unixepoch')=? ORDER BY utc,id",(value,)).fetchall()
        out=[f'# ACARS fleet summary: {value}', '',f'Messages: {len(rows)}; aircraft: {len({r["tail"] for r in rows if r["tail"] and r["lbl"] != "AV"})}; flights: {len({(r["flt"],r["tail"]) for r in rows if r["flt"]})}', '', '## Messages by hour and channel', '', '| Hour UTC | Channel Hz | Messages |','|---|---:|---:|']
        for (hour,f),n in sorted(Counter((timestamp(r['utc'])[11:13],r['f']) for r in rows).items()): out.append(f'| {hour}:00 | {f} | {n} |')
        for title,counts in [('Busiest flights',Counter((r['flt'],r['tail']) for r in rows if r['flt'])),('Labels',Counter(r['lbl'] for r in rows))]:
            out+=['',f'## {title}','']
            for key,n in counts.most_common(20): out.append(f'- {safe(" / ".join(str(x or "") for x in key) if isinstance(key,tuple) else key)}: {n}')
        unknown=[r for r in rows if r['format']=='unclassified']
        out+=['',f'## Unclassified / undecoded', '',f'{len(unknown)} of {len(rows)} messages ({100*len(unknown)/len(rows) if rows else 0:.1f}%).','', 'Top unclassified labels and one example each:','']
        for label,n in Counter(r['lbl'] for r in unknown).most_common(10):
            example=next(r for r in unknown if r['lbl']==label); out.append(f'- {safe(label)}: {n}; as received: `{safe(example["text"])}`')
        return '\n'.join(out)+'\n'
    if kind=='flight':
        where='flt=?'; args=[value.upper()]
        if date: dt.date.fromisoformat(date); where+=" AND date(utc,'unixepoch')=?"; args.append(date)
    else: where='tail=? AND lbl<>\'AV\''; args=[value.upper()]
    rows=db.execute('SELECT * FROM messages WHERE '+where+' ORDER BY CASE WHEN utc=0 THEN 1 ELSE 0 END,utc,up,id',args).fetchall()
    out=[f'# ACARS {kind}: {safe(value.upper())}', '',f'{len(rows)} messages. Times are receive times in UTC; undated records have no valid clock.','', '## Route', '']
    routes=[]
    for r in rows:
        d=json.loads(r['details'])
        if d.get('origin') and d.get('destination'): routes.append((d['origin'],d['destination']))
    out+=['; '.join(f'{a} to {b}' for a,b in dict.fromkeys(routes)) if routes else 'Unknown']
    out+=['','## Timeline','','| Received UTC | Tail | Flight | Label | Interpretation |','|---|---|---|---|---|']
    for r in rows:
        desc=r['summary']+'; as received: '+r['text']
        out.append(f'| {timestamp(r["utc"])} | {safe(r["tail"])} | {safe(r["flt"])} | {safe(r["lbl"])} | {safe(desc)} |')
    ids=[r['id'] for r in rows]
    out+=['','## Positions and altitude / speed profile','','| Received UTC | Latitude | Longitude | Altitude ft | Speed kt | Mach | Source |','|---|---:|---:|---:|---:|---:|---|']
    for r in rows:
        d=json.loads(r['details'])
        for p in d['positions']: out.append(f'| {timestamp(r["utc"])} | {p["lat"]:.5f} | {p["lon"]:.5f} | {p["altitude_ft"] if p["altitude_ft"] is not None else ""} | {p["speed_kt"] or ""} | {p["mach"] or ""} | {safe(p["source"])} |')
        for p in d.get('samples',[]): out.append(f'| {timestamp(r["utc"])} (sample {p["time"]}) | | | {p["altitude_ft"]} | {p["speed_kt"]} calibrated | {p["mach"]} | Southwest DFB sample |')
    out+=['','## ATC data link contacts and ATIS requests','']
    for r in rows:
        for kind_,details in json.loads(r['details'])['events']:
            if kind_ in ('atc link','atis request'): out.append(f'- {timestamp(r["utc"])}: {safe(r["summary"])}')
    out+=['','## Counts by label','']
    out += [f'- {safe(k)}: {v}' for k,v in Counter(r['lbl'] for r in rows).most_common()]
    return '\n'.join(out)+'\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__); sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('ingest'); p.add_argument('paths',nargs='+'); p.add_argument('--db',required=True)
    p=sub.add_parser('report'); p.add_argument('kind',choices=['aircraft','flight','day']); p.add_argument('value'); p.add_argument('--date'); p.add_argument('--db',default='acars.sqlite'); p.add_argument('--out')
    p=sub.add_parser('export'); p.add_argument('format',choices=['csv']); p.add_argument('--db',default='acars.sqlite'); p.add_argument('--out')
    args=parser.parse_args()
    try:
        db=connect(args.db)
        if args.command=='ingest': print(f'Added {ingest(db,args.paths)} messages')
        elif args.command=='report':
            result=report(db,args.kind,args.value,args.date)
            if args.out: Path(args.out).write_text(result,encoding='utf-8')
            print(result,end='')
        else:
            stream=open(args.out,'w',newline='',encoding='utf-8') if args.out else sys.stdout
            try:
                writer=csv.writer(stream); writer.writerow(['message_id','utc','lat','lon','altitude_ft','speed_kt','mach','source'])
                writer.writerows(db.execute('SELECT message_id,utc,lat,lon,altitude_ft,speed_kt,mach,source FROM positions ORDER BY utc,message_id'))
            finally:
                if args.out: stream.close()
        db.close()
    except (OSError,ValueError,sqlite3.Error) as e: parser.exit(1,f'error: {e}\n')
if __name__=='__main__': main()

