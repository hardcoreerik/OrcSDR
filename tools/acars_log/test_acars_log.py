import io
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import classify
import acars_log

SAMPLE = Path(__file__).resolve().parents[2] / 'data/acars-sd/20261029.jsonl'

class DecoderTests(unittest.TestCase):
    def test_crc_vectors(self):
        self.assertEqual(classify.crc16(b'AT1.N678DL' + bytes.fromhex('4104532300')), 0xA9B3)
        self.assertEqual(classify.crc16(b'ADS.N678DL' + bytes.fromhex('032D')), 0x2633)
        self.assertEqual(classify.crc16(b'TI2/024KSEAA'), 0x3BC3)

    @unittest.skipUnless(SAMPLE.exists(), 'requires the local data/acars-sd/20261029.jsonl sample')
    def test_real_capture_formats(self):
        expected={'ARINC 622', 'TI2 ATIS request', 'fault display', 'Southwest DFB header', 'Southwest DFB samples', 'DFB cruise fix', 'M1BPOS', 'label 32 position', 'label 33 position', 'Southwest DFB climb', 'DFB TRP'}
        seen=set()
        with SAMPLE.open(encoding='utf-8') as f:
            for line in f:
                m=json.loads(line)
                result=classify.classify(m['lbl'], bytes.fromhex(m['x']).decode('latin-1'),m['tail'])
                if result['format'] in expected:
                    seen.add(result['format'])
        self.assertEqual(seen,expected)

    def test_crc_rejects_corruption_and_unknown_stays_unknown(self):
        text='F06AWN0436#M1B/BA USADCXA.AT1.N926WN6082D826032E1E'
        self.assertEqual(classify.classify('H1',text,'N926WN')['format'],'unclassified')
        self.assertEqual(classify.classify('37','unknown data')['format'],'unclassified')

    @unittest.skipUnless(SAMPLE.exists(), 'requires the local data/acars-sd/20261029.jsonl sample')
    def test_ingest_is_idempotent_and_reports(self):
        with tempfile.TemporaryDirectory() as tmp:
            db=acars_log.connect(str(Path(tmp)/'test.sqlite'))
            count=acars_log.ingest(db,[SAMPLE])
            self.assertEqual(count,2165)
            self.assertEqual(acars_log.ingest(db,[SAMPLE]),0)
            self.assertEqual(db.execute('select count(*) from messages').fetchone()[0],count)
            flight=acars_log.report(db,'flight','WN0436')
            self.assertIn('KEUG to KDEN',flight)
            self.assertIn('ATC data link',flight)
            self.assertIn('Unclassified',acars_log.report(db,'day','2026-10-29'))
            self.assertGreater(db.execute('select count(*) from positions').fetchone()[0],0)
            db.close()

if __name__ == '__main__': unittest.main()
