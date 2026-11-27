import csv
import io
import unittest
from ised_probe import row_probe, rows

class ISEDTests(unittest.TestCase):
    def fixture(self):
        r=['']*61
        r[0]='TX'; r[1]='463.400000'; r[2]='freq-1'; r[11]='7K60FXE'
        r[40]='49.125'; r[41]='-123.25'; r[47]='auth-1'; r[54]='Cafe\u0301, test'; r[55]='mailing address'
        return r
    def test_fields_unicode(self):
        r=self.fixture(); o=row_probe(r,1)
        self.assertEqual(o['frequency_hz'],463400000)
        self.assertEqual(o['site_e7'],[491250000,-1232500000])
        self.assertEqual(o['licensee'],'Café, test')
        self.assertEqual(o['authorization_id'],'auth-1')
        self.assertNotIn('mailing address',str(o))
    def test_rx_missing_site(self):
        r=self.fixture(); r[0]='RX'; r[40]=''; o=row_probe(r,1)
        self.assertEqual(o['direction'],'RX'); self.assertIsNone(o['site_e7'])
    def test_bad_numbers(self):
        for value in ('NaN','0','-1','463.4000001','1e30'):
            r=self.fixture();r[1]=value
            with self.assertRaises(ValueError):row_probe(r,1)
    def test_bad_geometry_and_schema(self):
        r=self.fixture();r[40]='91'
        with self.assertRaises(ValueError):row_probe(r,1)
        with self.assertRaises(ValueError):row_probe(r[:30],1)
    def test_quoted_csv(self):
        s=io.StringIO();csv.writer(s).writerow(self.fixture());s.seek(0)
        self.assertEqual(list(rows(s))[0]['licensee'],'Café, test')
    def test_unknown_direction(self):
        r=self.fixture();r[0]='?'
        with self.assertRaises(ValueError):row_probe(r,1)

if __name__=='__main__':unittest.main()
