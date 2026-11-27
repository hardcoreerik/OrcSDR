import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from acquire import acquire

class AcquisitionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.zip = self.root / 'input.zip'
        self.out = self.root / 'out'

    def fixture(self, name='FR.dat', body=b'FR|synthetic\n'):
        with zipfile.ZipFile(self.zip, 'w') as z:
            z.writestr(name, body)

    def test_verified_inventory(self):
        self.fixture()
        sha = hashlib.sha256(self.zip.read_bytes()).hexdigest()
        result = acquire('fixture', self.out, local=self.zip, expected=sha)
        m = json.loads((result / 'manifest.json').read_text())
        self.assertEqual(m['archive_sha256'], sha)
        self.assertEqual(m['members'][0]['sha256'], hashlib.sha256(b'FR|synthetic\n').hexdigest())
        self.assertEqual(m['extracted_bytes'], 13)
        self.assertEqual(m['redistribution'], 'not_reviewed')
        with self.assertRaises(FileExistsError):
            acquire('fixture', self.out, local=self.zip)

    def test_wrong_hash(self):
        self.fixture()
        with self.assertRaises(ValueError):
            acquire('fixture', self.out, local=self.zip, expected='0'*64)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_path_traversal(self):
        self.fixture('../escape')
        with self.assertRaises(ValueError):
            acquire('fixture', self.out, local=self.zip)

    def test_expansion_limit(self):
        self.fixture(body=b'x'*100)
        with self.assertRaises(ValueError):
            acquire('fixture', self.out, local=self.zip, max_expanded=99)

    def test_download_limit(self):
        self.fixture()
        with self.assertRaises(ValueError):
            acquire('fixture', self.out, local=self.zip, max_download=1)

    def test_invalid_zip(self):
        self.zip.write_bytes(b'not a zip')
        with self.assertRaises(zipfile.BadZipFile):
            acquire('fixture', self.out, local=self.zip)

    def test_duplicate_member(self):
        with zipfile.ZipFile(self.zip, 'w') as z:
            z.writestr('same', b'a')
            z.writestr('same', b'b')
        with self.assertRaises(ValueError):
            acquire('fixture', self.out, local=self.zip)

    def test_insecure_url(self):
        with self.assertRaises(ValueError):
            acquire('fixture', self.out, url='http://example.com/file.zip')

if __name__ == '__main__':
    unittest.main()
