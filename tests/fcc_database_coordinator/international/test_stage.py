import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from stage import project, inventory


class Tests(unittest.TestCase):
    def run_project(self, text, path, country='AU', **kw):
        args = dict(country=country, source='fixture', key_columns=['key'], low_column='lo', high_column='hi', unit='MHz', approved_schema=True)
        args.update(kw)
        return project(io.StringIO(text), path, **args)

    def test_au_jp_deterministic_and_distinct(self):
        with tempfile.TemporaryDirectory() as d:
            a,b,c = [Path(d)/n for n in 'abc']
            text='key,lo,hi,private\nA,100,100,PRIVATE\nB,99,101,PRIVATE\n'
            x=self.run_project(text,a); y=self.run_project('key,lo,hi,private\nB,99,101,PRIVATE\nA,100,100,PRIVATE\n',b)
            z=self.run_project(text,c,'JP')
            self.assertEqual(a.read_bytes(),b.read_bytes()); self.assertNotEqual(x['projection_sha256'],z['projection_sha256'])
            self.assertEqual((x['indexed'],x['discrete'],x['intervals'],x['unique_discrete']),(2,1,1,1))
            self.assertNotIn('PRIVATE',a.read_text()); self.assertTrue(all(set(json.loads(l))=={'id','low','high'} for l in a.read_text().splitlines()))

    def test_japanese_text(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'jp'
            r=self.run_project('key,lo,hi,private\n局一,144.1,144.1,氏名\n',p,'JP')
            self.assertEqual(r['indexed'],1); self.assertNotIn('氏名',p.read_text()); self.assertNotIn('局一',p.read_text())

    def test_exclusions(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x'; r=self.run_project('key,lo,hi\na,100.0000001,100.1\nb,101,100\nc,100,\nd,100,100\n',p)
            self.assertEqual((r['input'],r['indexed'],r['excluded_frequency']),(4,1,3))

    def test_invalid_duplicate_and_schema(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x'
            for t in ['key,lo,hi\na,100,100\na,101,101\n','key,lo,lo\na,100,100\n','key,lo,hi\n,100,100\n']:
                with self.assertRaises(ValueError): self.run_project(t,p)
                self.assertFalse(p.exists())
            with self.assertRaises(ValueError): self.run_project('key,lo,hi\na,100,100\n',p,approved_schema=False)

    def test_zip_inventory(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'a.zip'
            with zipfile.ZipFile(p,'w') as z: z.writestr('rows.csv','key,lo\na,100\n')
            r=inventory(p); self.assertEqual(r['members'][0]['member'],'rows.csv'); self.assertEqual(len(r['archive_sha256']),64)
            with zipfile.ZipFile(p,'a') as z: z.writestr('../bad','x')
            with self.assertRaises(ValueError): inventory(p)

    def test_no_overwrite(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x'; p.write_text('keep')
            with self.assertRaises(FileExistsError): self.run_project('key,lo,hi\na,100,100\n',p)
            self.assertEqual(p.read_text(),'keep')


if __name__ == '__main__': unittest.main()
