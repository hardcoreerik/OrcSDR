"""Host-only regression tests: run with python3 -m unittest discover -s tools/hardware."""
import copy,unittest
from validate_catalog import CatalogError, load, validate,public_catalog
class BoardCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.data=load()
    def test_only_tab5_released(self):
        entries=public_catalog(self.data)['boards']
        self.assertEqual([b['id'] for b in entries if b['install_enabled']],['m5stack-tab5'])
        self.assertTrue(all(b['manifest_url'] is None for b in entries[1:]))
    def test_same_chip_different_board(self):
        a,b=self.data['boards'][:2]
        self.assertEqual(a['chip_family'],b['chip_family'])
        self.assertNotEqual(a['id'],b['id'])
    def test_refuse_premature_firmware(self):
        d=copy.deepcopy(self.data)
        d['boards'][1]['flash']={'enabled':True,'manifest_url':'https://example.com/4b.json'}
        with self.assertRaises(CatalogError):validate(d)
    def test_refuse_duplicate_id(self):
        d=copy.deepcopy(self.data)
        d['boards'][1]['id']=d['boards'][0]['id']
        with self.assertRaises(CatalogError):validate(d)
    def test_refuse_insecure_url(self):
        d=copy.deepcopy(self.data)
        d['boards'][0]['flash']['manifest_url']='http://example.com/fw.json'
        with self.assertRaises(CatalogError):validate(d)
    def test_refuse_headless_with_display(self):
        d=copy.deepcopy(self.data)
        d['boards'][-1]['display']={'width':100,'height':100,'touch':False}
        with self.assertRaises(CatalogError):validate(d)
if __name__=='__main__':unittest.main()
