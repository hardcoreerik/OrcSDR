import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / 'components' / 'orcsdr_orientation'


class Config(c.Structure):
    _fields_ = [('display_x_from_sensor', c.c_float * 3),
                ('display_y_from_sensor', c.c_float * 3),
                ('dwell_ms', c.c_uint32), ('hysteresis_deg', c.c_float)]


class Policy(c.Structure):
    _fields_ = [('enabled', c.c_bool), ('has_filter', c.c_bool),
                ('pending_manual', c.c_bool), ('touch_active', c.c_bool),
                ('geometry_locked', c.c_bool), ('current_degrees', c.c_int32),
                ('pending_degrees', c.c_int32), ('stable_degrees', c.c_int32),
                ('stable_since_ms', c.c_uint32), ('last_sample_ms', c.c_uint32),
                ('filtered', c.c_float * 3), ('config', Config)]


class OrientationPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cl = Path(r'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe')
        include = cl.parents[3] / 'include'
        env = dict(os.environ)
        env['PATH'] = str(cl.parent) + os.pathsep + env['PATH']
        dll = Path(cls.temp.name) / 'orientation.dll'
        args = [str(cl), '/nologo', '/LD', '/O2', '/GS-', '/Zl', '/DORCSDR_ORIENTATION_EXPORTS',
                '/I' + str(include), '/I' + str(COMPONENT / 'include'),
                str(COMPONENT / 'src' / 'orcsdr_orientation_policy.c'),
                str(ROOT / 'tests' / 'orientation_float_support.c'),
                '/link', '/NOENTRY', '/NODEFAULTLIB', '/OUT:' + str(dll)]
        run = subprocess.run(args, cwd=cls.temp.name, env=env, capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(dll))
        kernel = c.WinDLL('kernel32', use_last_error=True)
        kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        cls.lib.orcsdr_orientation_policy_init.argtypes = [c.POINTER(Policy), c.POINTER(Config), c.c_int32]
        cls.lib.orcsdr_orientation_policy_init.restype = c.c_bool
        cls.lib.orcsdr_orientation_policy_enable.argtypes = [c.POINTER(Policy), c.c_bool]
        cls.lib.orcsdr_orientation_policy_set_gates.argtypes = [c.POINTER(Policy), c.c_bool, c.c_bool]
        cls.lib.orcsdr_orientation_policy_request_manual.argtypes = [c.POINTER(Policy), c.c_int32]
        cls.lib.orcsdr_orientation_policy_request_manual.restype = c.c_bool
        cls.lib.orcsdr_orientation_policy_update.argtypes = [c.POINTER(Policy), c.c_float, c.c_float, c.c_float,
                                                              c.c_uint32, c.POINTER(c.c_int32)]
        cls.lib.orcsdr_orientation_policy_update.restype = c.c_bool

    def make_policy(self, initial=0, dwell=500, hysteresis=12):
        # This is an explicit test fixture calibration. Production profile must
        # supply its own measured sensor-to-panel matrix.
        cfg = Config((c.c_float * 3)(1, 0, 0), (c.c_float * 3)(0, 1, 0), dwell, hysteresis)
        state = Policy()
        self.assertTrue(self.lib.orcsdr_orientation_policy_init(c.byref(state), c.byref(cfg), initial))
        return state

    def tick(self, state, xyz, ms):
        out = c.c_int32(-1)
        changed = self.lib.orcsdr_orientation_policy_update(c.byref(state), *xyz, ms, c.byref(out))
        return out.value if changed else None

    def test_auto_rotation_defaults_off_and_requires_stable_dwell(self):
        state = self.make_policy(dwell=500)
        self.assertFalse(state.enabled)
        self.assertIsNone(self.tick(state, (0, -1, 0), 100))
        self.lib.orcsdr_orientation_policy_enable(c.byref(state), True)
        for ms in range(100, 600, 50):
            self.assertIsNone(self.tick(state, (0, -1, 0), ms))
        self.assertEqual(self.tick(state, (0, -1, 0), 600), 90)

    def test_face_up_down_and_near_boundary_are_rejected(self):
        state = self.make_policy(dwell=100)
        self.lib.orcsdr_orientation_policy_enable(c.byref(state), True)
        for ms in range(100, 700, 50):
            self.assertIsNone(self.tick(state, (0, 0, 1), ms))
        # Within the configured 12 degree band around the 45 degree boundary.
        import math
        theta = math.radians(50)
        sample = (math.cos(theta), -math.sin(theta), 0)
        for ms in range(700, 1500, 50):
            self.assertIsNone(self.tick(state, sample, ms))

    def test_shake_resets_dwell_and_touch_or_update_lock_defers(self):
        state = self.make_policy(dwell=300)
        self.lib.orcsdr_orientation_policy_enable(c.byref(state), True)
        target = (0, -1, 0)
        self.tick(state, target, 100)
        for ms in range(150, 300, 50):
            self.tick(state, target, ms)
        # A large vector step is rejected as shake and restarts stable dwell.
        self.assertIsNone(self.tick(state, (1, 0, 0), 300))
        for ms in range(350, 600, 50):
            self.tick(state, target, ms)
        self.assertIsNone(self.tick(state, target, 600))
        self.lib.orcsdr_orientation_policy_set_gates(c.byref(state), True, False)
        for ms in range(650, 1050, 50):
            self.tick(state, target, ms)
        self.assertEqual(state.current_degrees, 0)
        self.lib.orcsdr_orientation_policy_set_gates(c.byref(state), False, True)
        self.assertIsNone(self.tick(state, target, 1050))
        self.assertEqual(state.current_degrees, 0)
        self.lib.orcsdr_orientation_policy_set_gates(c.byref(state), False, False)
        self.assertEqual(self.tick(state, target, 1100), 90)

    def test_manual_fallback_works_when_auto_is_off_and_waits_for_gates(self):
        state = self.make_policy()
        self.assertTrue(self.lib.orcsdr_orientation_policy_request_manual(c.byref(state), 180))
        self.lib.orcsdr_orientation_policy_set_gates(c.byref(state), False, True)
        self.assertIsNone(self.tick(state, (float('nan'), 0, 0), 100))
        self.assertTrue(state.pending_manual)
        self.lib.orcsdr_orientation_policy_set_gates(c.byref(state), False, False)
        self.assertEqual(self.tick(state, (0, 0, 0), 150), 180)
        self.assertFalse(state.enabled)

    def test_invalid_angles_and_axis_profiles_are_rejected(self):
        state = self.make_policy()
        self.assertFalse(self.lib.orcsdr_orientation_policy_request_manual(c.byref(state), 45))
        bad = Config((c.c_float * 3)(0, 0, 0), (c.c_float * 3)(0, 1, 0), 500, 12)
        self.assertFalse(self.lib.orcsdr_orientation_policy_init(c.byref(state), c.byref(bad), 0))
        skewed = Config((c.c_float * 3)(1, 0, 0), (c.c_float * 3)(0.8, 0.6, 0), 500, 12)
        self.assertFalse(self.lib.orcsdr_orientation_policy_init(c.byref(state), c.byref(skewed), 0))


if __name__ == '__main__':
    unittest.main()
