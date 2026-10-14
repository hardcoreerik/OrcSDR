import unittest
from check_4b_home_log import assess
def fixture(last=310000,rate=958000,drop=0):
    out='I (100) ORCSDR_4B_HOME: HOME_4B RF START hz=96100000 rate=960000\n'
    for ms in range(2000,last+1,1000):
        out+=f'I ({ms}) ORCSDR_4B_HOME: HOME_4B IQ hz=96100000 sps={rate} consumed={ms*1000} drops={drop if ms>=20000 else 0}\n'
    return out
class Qualify(unittest.TestCase):
    def test_sustained(self):self.assertTrue(assess(fixture())[0])
    def test_absence(self):self.assertFalse(assess('No dongle')[0])
    def test_short(self):self.assertFalse(assess(fixture(last=50000))[0])
    def test_low_rate(self):self.assertFalse(assess(fixture(rate=800000))[0])
    def test_drop(self):self.assertFalse(assess(fixture(drop=5))[0])
if __name__=='__main__':unittest.main()
