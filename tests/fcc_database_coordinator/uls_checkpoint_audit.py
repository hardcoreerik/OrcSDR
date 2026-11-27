"""Historical staging-boundary audit, not a current FCC archive test."""
import json
import sqlite3
import unittest
from uls_stage import stage


def record(kind, key=('101','7','2','3'), low='100.125', upper='', seq='1', call='EXAMPLE'):
    row=['']*(30 if kind=='FR' else 16)
    row[0],row[1],row[4]=kind,key[0],call
    if kind=='FR':
        row[6],row[7],row[26]=key[1:]
        row[10],row[11]=low,upper
    else:
        row[5],row[6],row[12]=key[1:]
        row[7],row[9],row[15]=low,'7K60FXE',seq
    return '|'.join(row)+'\n'


class Boundary(unittest.TestCase):
    def test_emission_sequence_and_numeric_key_equivalence(self):
        with sqlite3.connect(':memory:') as db:
            counts=stage(db,[record('FR')],[record('EM',('000101','007','02','03')),record('EM',seq='2')])
            self.assertEqual(counts['orphan_em'],0)
            self.assertEqual(db.execute('SELECT count(*) FROM em JOIN fr USING(k)').fetchone()[0],2)

    def test_inside_block_center_flagged_not_proven_conflict(self):
        with sqlite3.connect(':memory:') as db:
            counts=stage(db,[record('FR',low='100',upper='101')],[record('EM',low='100.5')])
            self.assertEqual(counts['frequency_conflict'],1)
            self.assertEqual(db.execute('SELECT lo_hz,hi_hz FROM fr').fetchone(),('100000000','101000000'))
            # Historical policy compares only the lower endpoint. Need service-specific semantics.

    def test_conflict_counter_does_not_quarantine_join(self):
        with sqlite3.connect(':memory:') as db:
            counts=stage(db,[record('FR')],[record('EM',low='200')])
            self.assertEqual(counts['frequency_conflict'],1)
            self.assertEqual(db.execute('SELECT count(*) FROM em JOIN fr USING(k)').fetchone()[0],1)
            self.assertEqual(json.loads(db.execute('SELECT raw FROM em').fetchone()[0])[7],'200')

    def test_blank_key_rolls_back_data(self):
        with sqlite3.connect(':memory:') as db:
            with self.assertRaises(ValueError):
                stage(db,[record('FR'),record('FR',('102','','2','3'))],[])
            self.assertEqual(db.execute('SELECT count(*) FROM fr').fetchone()[0],0)

    def test_fractional_hz_retained_but_not_indexable(self):
        with sqlite3.connect(':memory:') as db:
            counts=stage(db,[record('FR',low='100.12500001')],[])
            self.assertEqual(counts['unindexed'],1)
            self.assertEqual(db.execute('SELECT low_raw,lo_hz,hi_hz FROM fr').fetchone(),('100.12500001',None,None))

    def test_malformed_orphan_frequency_not_validated(self):
        with sqlite3.connect(':memory:') as db:
            counts=stage(db,[],[record('EM',low='invalid')])
            self.assertEqual(counts['orphan_em'],1)
            self.assertEqual(db.execute('SELECT assigned_raw FROM em').fetchone()[0],'invalid')

if __name__=='__main__': unittest.main()
