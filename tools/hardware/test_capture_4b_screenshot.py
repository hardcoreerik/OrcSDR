import base64
import io
import struct
import unittest
import zlib
from capture_4b_screenshot import receive, validate_png


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png():
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"\x00\x00\xff\x00")) + chunk(b"IEND", b""))


def stream(data, skip=False, crc=None):
    token = "0123456789abcdef"
    lines = ["I (10) HOME_4B IQ: noise", "ORCUI_PNG_ERROR another-token ignored",
             f"ORCUI_PNG_BEGIN {token} 1 1 {len(data)} {zlib.crc32(data) if crc is None else crc:08x} " + "a" * 64]
    for seq, offset in enumerate(range(0, len(data), 21)):
        lines.append(f"ORCUI_PNG_DATA {token} {seq + int(skip)} " + base64.b64encode(data[offset:offset + 21]).decode())
        lines.append("I (20) telemetry between chunks")
    lines.append(f"ORCUI_PNG_END {token}")
    return io.BytesIO(("\n".join(lines) + "\n").encode())


class CaptureTests(unittest.TestCase):
    def test_transfer_with_telemetry(self):
        data, meta = receive(stream(png()), "0123456789abcdef", 1)
        self.assertEqual(data, png())
        self.assertEqual((meta["width"], meta["height"]), (1, 1))

    def test_sequence_gap(self):
        with self.assertRaisesRegex(ValueError, "out-of-order"):
            receive(stream(png(), skip=True), "0123456789abcdef", 1)

    def test_transport_corruption(self):
        with self.assertRaisesRegex(ValueError, "checksum"):
            receive(stream(png(), crc=0), "0123456789abcdef", 1)

    def test_png_corruption(self):
        data = bytearray(png())
        data[30] ^= 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            validate_png(data, 1, 1)

    def test_truncated_png(self):
        with self.assertRaises(ValueError):
            validate_png(png()[:-5], 1, 1)


if __name__ == "__main__":
    unittest.main()
