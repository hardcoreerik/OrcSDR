import sqlite3,unittest
from uls_stage import stage

def fr(uid='1',loc='1',ant='1',freq='1',lo='463.400000',hi=''):
    a=['']*30
    for i,v in {0:'FR',1:uid,4:'SYNTH',6:loc,7:ant,10:lo,11:hi,26:freq}.items():a[i]=v
    return '|'.join(a)+'\n'
def em(uid='1',loc='1',ant='1',freq='1',lo='463.4',seq='1'):
    a=['']*16
    for i,v in {0:'EM',1:uid,4:'SYNTH',5:loc,6:ant,7:lo,9:'7K60FXE',12:freq,15:seq}.items():a[i]=v
    return '|'.join(a)+'\n'
class Tests(unittest.TestCase):
    def test_composite_join(self):
        db=sqlite3.connect(':memory:');c=stage(db,[fr(),fr(uid='2'),fr(ant='2'),fr(freq='2')],[em(),em(uid='3'),em(ant='2',lo='463.5'),em(freq='2')]);self.assertEqual(c,dict(fr=4,em=4,unindexed=0,orphan_em=1,frequency_conflict=1));self.assertEqual(db.execute('SELECT count(*) FROM em JOIN fr USING(k)').fetchone()[0],3)
    def test_range_and_fractional(self):
        db=sqlite3.connect(':memory:');c=stage(db,[fr(hi='464.0'),fr(freq='2',lo='463.40000001')],[]);self.assertEqual(c['unindexed'],1);self.assertEqual(db.execute('SELECT lo_hz,hi_hz FROM fr WHERE hi_hz IS NOT NULL').fetchone(),('463400000','464000000'))
    def test_bad_schema(self):
        with self.assertRaises(ValueError):stage(sqlite3.connect(':memory:'),['FR|1'],[])
    def test_duplicate(self):
        with self.assertRaises(sqlite3.IntegrityError):stage(sqlite3.connect(':memory:'),[fr(),fr()],[])
if __name__=='__main__':unittest.main()
