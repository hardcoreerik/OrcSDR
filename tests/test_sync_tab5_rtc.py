import unittest

from tools.sync_tab5_rtc import rtc_set_command, tz_set_command


class RtcSyncTests(unittest.TestCase):
    def test_builds_bounded_utc_command(self):
        self.assertEqual(rtc_set_command(1_789_000_000), "ORC_RTC_SET 1789000000")
        with self.assertRaises(ValueError):
            rtc_set_command(0)
        with self.assertRaises(ValueError):
            rtc_set_command(4_102_444_800)

    def test_builds_bounded_offset_command(self):
        self.assertEqual(tz_set_command(-420), "ORC_TZ_SET -420")
        self.assertEqual(tz_set_command(345), "ORC_TZ_SET 345")
        for bad in (-721, 841):
            with self.assertRaises(ValueError):
                tz_set_command(bad)


if __name__ == "__main__":
    unittest.main()
