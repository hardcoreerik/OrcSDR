"""Deterministic unsigned experimental frequency shard. Input: normalized JSONL, not raw FCC."""
import argparse, hashlib, json, os, sqlite3, struct, tempfile
from pathlib import Path
U64 = (1 << 64)-1
HEADER = struct.Struct('<8sHHI2s6xQQQQQ')
ENTRY = struct.Struct('<QQQ')
NODE = struct.Struct('<QQQQ')
BLOCK = 32
LIMIT = 1 << 30

def integer(v):
    if type(v) is not int or not 0 <= v <= U64: raise ValueError('invalid u64')
    return v

def key(v): return integer(v).to_bytes(8, 'big')

def build(src, dest, country):
    if len(country)!=2 or not country.isascii() or not country.isalpha() or country!=country.upper(): raise ValueError('country')
    dest=Path(dest)
    if dest.exists(): raise FileExistsError(dest)
    with tempfile.TemporaryDirectory(dir=dest.parent) as work:
        db=sqlite3.connect(Path(work)/'sort.db')
        try:
            db.execute('CREATE TABLE rows (id BLOB PRIMARY KEY, lo BLOB, hi BLOB) WITHOUT ROWID')
            for line in src:
                r=json.loads(line); lo,hi,ident=map(integer,(r['low'],r['high'],r['id']))
                if lo>hi or not ident: raise ValueError('range or zero id')
                try: db.execute('INSERT INTO rows VALUES (?,?,?)',(key(ident),key(lo),key(hi)))
                except sqlite3.IntegrityError as e: raise ValueError('duplicate id') from e
            db.commit()
            exact=db.execute('SELECT count(*) FROM rows WHERE lo=hi').fetchone()[0]
            ranges=db.execute('SELECT count(*) FROM rows WHERE lo<hi').fetchone()[0]
            blocks=(ranges+BLOCK-1)//BLOCK
            leaves=1<<(blocks-1).bit_length() if blocks else 0
            nodes=2*leaves-1 if leaves else 0
            roff=HEADER.size+exact*ENTRY.size; noff=roff+ranges*ENTRY.size
            size=noff+nodes*NODE.size
            if size>LIMIT: raise ValueError('shard exceeds 1 GiB')
            tmp=Path(work)/'shard'
            with tmp.open('w+b') as f:
                f.write(HEADER.pack(b'FCCFRQ02',2,0,HEADER.size,country.encode(),exact,ranges,nodes,roff,noff))
                for pred in ('lo=hi','lo<hi'):
                    for lo,hi,ident in db.execute('SELECT lo,hi,id FROM rows WHERE '+pred+' ORDER BY lo,hi,id'):
                        f.write(ENTRY.pack(*(int.from_bytes(v,'big') for v in (lo,hi,ident))))
                f.truncate(size)
                def put(i,node): f.seek(noff+i*NODE.size);f.write(NODE.pack(*node))
                def get(i): f.seek(noff+i*NODE.size);return NODE.unpack(f.read(NODE.size))
                for i in range(leaves):
                    start=i*BLOCK; count=min(BLOCK,max(0,ranges-start))
                    if count:
                        f.seek(roff+start*ENTRY.size); rr=[ENTRY.unpack(f.read(ENTRY.size)) for _ in range(count)]
                        put(leaves-1+i,(rr[0][0],max(x[1] for x in rr),start,count))
                    else: put(leaves-1+i,(U64,0,0,0))
                for i in reversed(range(leaves-1)):
                    a,b=get(2*i+1),get(2*i+2)
                    put(i,(min(a[0],b[0]),max(a[1],b[1]),0,0))
            with tmp.open('rb') as f: digest=hashlib.file_digest(f,'sha256').hexdigest()
            os.replace(tmp,dest)
            return dict(country=country,exact=exact,ranges=ranges,nodes=nodes,bytes=size,sha256=digest,qualification='experimental_unsigned')
        finally: db.close()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input');p.add_argument('output');p.add_argument('--country',required=True);a=p.parse_args()
    with open(a.input,encoding='utf8') as f: print(json.dumps(build(f,a.output,a.country),sort_keys=True))
