import importlib.util
from pathlib import Path
import unittest
from PIL import Image

spec = importlib.util.spec_from_file_location("regions", Path(__file__).resolve().parents[1] / "tools/check_screenshot_regions.py")
regions = importlib.util.module_from_spec(spec)
spec.loader.exec_module(regions)


class ScreenshotRegionsTest(unittest.TestCase):
    def test_changed_value_preserves_static_chrome(self):
        before = Image.new("RGB", (32, 32), "black")
        before.putpixel((3, 3), (0, 255, 0))
        after = before.copy()
        after.putpixel((20, 20), (255, 255, 255))
        result = regions.compare_regions(before, after, [("chrome", (0, 0, 8, 8)), ("value", (16, 16, 8, 8))])
        self.assertTrue(result[0]["unchanged"])
        self.assertFalse(result[1]["unchanged"])
        self.assertEqual(result[1]["changed_bounds_in_region"], (4, 4, 5, 5))

    def test_rejects_wrong_orientation_or_invalid_region(self):
        before = Image.new("RGB", (16, 32))
        with self.assertRaises(ValueError):
            regions.compare_regions(before, Image.new("RGB", (32, 16)), [])
        for bounds in ((-1, 0, 1, 1), (0, 0, 0, 1), (0, 0, 17, 1)):
            with self.assertRaises(ValueError):
                regions.compare_regions(before, before, [("invalid", bounds)])


if __name__ == "__main__":
    unittest.main()
