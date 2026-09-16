import unittest

from tools.sync_tab5_rtc import rtc_set_command


class RtcSyncTests(unittest.TestCase):
    def test_builds_bounded_utc_command(self):
        self.assertEqual(rtc_set_command(1_789_000_000), "ORC_RTC_SET 1789000000")
        with self.assertRaises(ValueError):
            rtc_set_command(0)
        with self.assertRaises(ValueError):
            rtc_set_command(4_102_444_800)


if __name__ == "__main__":
    unittest.main()
