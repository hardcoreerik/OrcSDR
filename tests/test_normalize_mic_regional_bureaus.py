#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NORMALIZER = ROOT / "tools" / "data_catalog" / "normalize_mic_regional_bureaus.py"


KANTO_HTML = """<html><body>
<h2>AM放送（中波放送）</h2>
<table>
  <tr><th>&nbsp;</th><th>事業者名</th><th>親局周波数</th><th>中継局周波数</th></tr>
  <tr><td>1</td><td>日本放送協会</td><td>594kHz(東京)</td><td>1584kHz(富士吉田)</td></tr>
  <tr><td>2</td><td>株式会社山梨放送</td><td>765kHz</td><td>&nbsp;</td></tr>
</table>
<h2>短波放送</h2>
<table>
  <tr><th>&nbsp;</th><th>事業者名</th><th>周波数 （MHz）</th><th>備考</th></tr>
  <tr><td>1</td><td>株式会社日経ラジオ社</td><td>6.055 3.925</td><td>第1放送</td></tr>
</table>
<h2>FM放送（超短波放送）</h2>
<table>
  <tr><th>&nbsp;</th><th>事業者名</th><th>親局周波数（MHz）</th><th>中継局周波数（MHz）</th><th>主な放送区域</th></tr>
  <tr><td>1</td><td>日本放送協会</td><td>82.5（東京・墨田）</td><td>77.5（新島）</td><td>新島村</td></tr>
</table>
<h2>東京都のコミュニティ放送局</h2>
<table>
  <tr><th>&nbsp;</th><th>事業者名</th><th>所在地</th><th>周波数(MHz)</th></tr>
  <tr><td>1</td><td>株式会社エフエムむさしの</td><td>武蔵野市</td><td>78.2</td></tr>
</table>
</body></html>"""


KINKI_TYUHA_HTML = """<html><body>
<h1>中波放送局（AM）の置局状況</h1>
<table>
  <tr><th>放送事業者名</th><th>局名</th><th>送信場所</th><th>周波数</th><th>電力</th></tr>
  <tr><td>日本放送協会（NHK）</td><td>NHK大阪AM</td><td>大阪府堺市美原区</td><td>666 kHz</td><td>100 kW</td></tr>
  <tr><td>ラジオ関西</td><td>ラジオかんさい</td><td>兵庫県淡路市</td><td>558 kHz</td><td>20 kW</td></tr>
</table>
</body></html>"""


KINKI_KENIKI_HTML = """<html><body>
<h1>超短波放送局（県域FM局等）</h1>
<table>
  <tr><th>幹局名</th><th>周波数（メガヘルツ）</th><th>電力</th><th>送信場所</th><th>中継局数</th></tr>
  <tr><td>大阪放送局</td><td>88.1</td><td>10kW</td><td>飯盛山</td><td>1</td></tr>
</table>
<table>
  <tr><th>放送事業者名</th><th>周波数（メガヘルツ）</th><th>電力</th><th>送信場所</th><th>中継局数</th></tr>
  <tr><td>エフエム大阪</td><td>85.1</td><td>10kW</td><td>飯盛山</td><td>1</td></tr>
</table>
</body></html>"""


KINKI_FMHOKAN_HTML = """<html><body>
<h1>FM補完中継局の置局状況</h1>
<table>
  <tr><th>放送事業者名</th><th>周波数</th><th>電力</th><th>主な放送区域</th></tr>
  <tr><td>MBSラジオ（MBS-R）</td><td>90.6 MHz</td><td>7 kW</td><td>大阪市等</td></tr>
</table>
</body></html>"""


def encode_sjis(text: str) -> bytes:
    return text.encode("shift_jis", errors="replace")


class MicRegionalBureauNormalizerTest(unittest.TestCase):
    def test_extracts_am_fm_sw_across_kanto_and_kinki(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            (stage / "kanto_list.html").write_bytes(encode_sjis(KANTO_HTML))
            (stage / "kinki_tyuha.html").write_bytes(encode_sjis(KINKI_TYUHA_HTML))
            (stage / "kinki_keniki.html").write_bytes(encode_sjis(KINKI_KENIKI_HTML))
            (stage / "kinki_fmhokan.html").write_bytes(encode_sjis(KINKI_FMHOKAN_HTML))
            output = stage / "out.ndjson"
            subprocess.run(
                [sys.executable, str(NORMALIZER),
                 "--stage", str(stage), "--out", str(output)],
                check=True, capture_output=True, text=False)
            records = [json.loads(line) for line in
                       output.read_text(encoding="utf-8").splitlines()]

            services = sorted((r["service"], r["frequency_hz"]) for r in records)
            self.assertIn(("am", 594000), services)      # Kanto NHK Tokyo
            self.assertIn(("am", 1584000), services)     # Kanto NHK Fujiyoshida
            self.assertIn(("am", 765000), services)      # Kanto Yamanashi
            self.assertIn(("am", 666000), services)      # Kinki NHK Osaka
            self.assertIn(("am", 558000), services)      # Kinki Radio Kansai
            self.assertIn(("fm", 82500000), services)    # Kanto NHK Tokyo FM
            self.assertIn(("fm", 77500000), services)    # Kanto NHK relay
            self.assertIn(("fm", 78200000), services)    # Kanto CommFM 武蔵野
            self.assertIn(("fm", 88100000), services)    # Kinki NHK Osaka FM
            self.assertIn(("fm", 85100000), services)    # Kinki FM Osaka
            self.assertIn(("fm", 90600000), services)    # Kinki MBS Wide-FM
            self.assertIn(("shortwave", 6055000), services)  # Radio Nikkei 1
            self.assertIn(("shortwave", 3925000), services)  # Radio Nikkei 1 dawn/dusk

            for record in records:
                self.assertEqual(record["source"], "mic_regional_bureau_lists")
                self.assertEqual(record["country"], "JP")
                self.assertTrue(record["id"].startswith("mic_regional_bureau_lists:"))
                self.assertIn(record["service"], ("am", "fm", "shortwave"))
                # Kinki-source records carry power_w; Kanto does not.
                if "kinki:" in record["id"]:
                    self.assertIn("power_w", record)
                    self.assertGreater(int(record["power_w"]), 0)

    def test_rejects_out_of_band_and_unparseable(self):
        bad_kanto = """<html><body>
<h2>コミュニティFM</h2>
<table>
  <tr><th>&nbsp;</th><th>事業者名</th><th>所在地</th><th>周波数(MHz)</th></tr>
  <tr><td>1</td><td>Bad Station</td><td>Nowhere</td><td>999.9</td></tr>
  <tr><td>2</td><td>Also Bad</td><td>Nowhere</td><td>not-a-number</td></tr>
</table>
</body></html>"""
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            (stage / "kanto_list.html").write_bytes(encode_sjis(bad_kanto))
            output = stage / "out.ndjson"
            subprocess.run(
                [sys.executable, str(NORMALIZER),
                 "--stage", str(stage), "--out", str(output)],
                check=True, capture_output=True, text=False)
            self.assertEqual(output.read_text(encoding="utf-8").strip(), "")


if __name__ == "__main__":
    unittest.main()
