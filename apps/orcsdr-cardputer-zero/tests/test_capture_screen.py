import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("capture_screen", Path(__file__).resolve().parents[1] / "tools" / "capture_screen.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTest(unittest.TestCase):
    def test_rgb565_padding_and_pan(self):
        raw = bytes.fromhex("0000000000000000000000f8e0070000")
        image = capture.decode_frame(raw, 2, 1, 8, 16, 1, 1, ((11, 5, 0), (5, 6, 0), (0, 5, 0)))
        self.assertEqual(list(image.getdata()), [(255, 0, 0), (0, 255, 0)])

    def test_xrgb8888(self):
        image = capture.decode_frame(bytes.fromhex("ff000000"), 1, 1, 4, 32, 0, 0,
                                     ((16, 8, 0), (8, 8, 0), (0, 8, 0)))
        self.assertEqual(image.getpixel((0, 0)), (0, 0, 255))

    def test_rejects_truncated_frame(self):
        with self.assertRaises(ValueError):
            capture.decode_frame(b"", 1, 1, 2, 16, 0, 0, ((11, 5, 0), (5, 6, 0), (0, 5, 0)))


if __name__ == "__main__":
    unittest.main()
