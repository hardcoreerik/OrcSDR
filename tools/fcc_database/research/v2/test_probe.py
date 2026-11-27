import io,json,random,subprocess,tempfile,unittest
from pathlib import Path
from build_probe import build,U64
class Tests(unittest.TestCase):
    def test_oracle(self):
        rng=random.Random(13)
        rows=[dict(low=1,high=U64,id=1),dict(low=U64,high=U64,id=2)]
        for i in range(2,10002):
            lo=rng.randrange(1,1000000);rows.append(dict(low=lo,high=lo+(rng.randrange(1,200) if i%7==0 else 0),id=i+1))
        def stream(rs):return io.StringIO(''.join(json.dumps(r)+'\n' for r in rs))
        with tempfile.TemporaryDirectory() as d:
            a,b=Path(d)/'a',Path(d)/'b';m=build(stream(rows),a,'US');self.assertEqual(m,build(stream(reversed(rows)),b,'US'));self.assertEqual(a.read_bytes(),b.read_bytes())
            q=[(0,0),(U64,U64),(0,U64),(1,1)]+[(x,x+rng.randrange(300)) for x in (rng.randrange(1000000) for _ in range(500))]
            result=subprocess.run(['./frequency_probe_test',str(a)],input=''.join(f'{lo} {hi}\n' for lo,hi in q),text=True,capture_output=True,check=True)
            lines=result.stdout.splitlines();self.assertEqual(len(lines),len(q));reads=[]
            for (lo,hi),line in zip(q,lines):
                ids,n=line.split('|');got=[int(x) for x in ids.split(',') if x];expected=[r['id'] for r in sorted(rows,key=lambda r:(r['low']!=r['high'],r['low'],r['high'],r['id'])) if r['low']<=hi and r['high']>=lo];self.assertEqual(got,expected);reads.append(int(n))
            print(json.dumps(dict(m,queries=len(q),typical_max_reads=max(reads[4:]),full_range_reads=reads[2]),sort_keys=True))
            bad=Path(d)/'bad';bad.write_bytes(a.read_bytes()[:-1]);self.assertEqual(subprocess.run(['./frequency_probe_test',str(bad)],input='',text=True).returncode,2)
    def test_duplicate(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):build(io.StringIO('{"low":1,"high":1,"id":1}\n'*2),Path(d)/'x','US')
if __name__=='__main__':unittest.main()
