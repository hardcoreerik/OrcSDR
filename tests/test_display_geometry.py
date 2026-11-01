"""Exercise production C geometry; no IDF, display driver or framebuffer required."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / 'components/orcsdr_display_geometry'


class Point(c.Structure):
    _fields_ = [('x', c.c_int32), ('y', c.c_int32)]


class Rect(c.Structure):
    _fields_ = [(k, c.c_int32) for k in ('x', 'y', 'width', 'height')]


class Geometry(c.Structure):
    _fields_ = [(k, c.c_int32) for k in ('native_width', 'native_height', 'rotation', 'width', 'height')] + [('safe', Rect)]


class Calibration(c.Structure):
    _fields_ = [(k, c.c_int32) for k in ('x_min', 'x_max', 'y_min', 'y_max', 'swap_axes', 'mirror_x', 'mirror_y')]


class GeometryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        d = Path(cls.temp.name)
        cl = Path(r'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe')
        include = cl.parents[3] / 'include'
        env = dict(os.environ)
        env['PATH'] = str(cl.parent) + os.pathsep + env['PATH']
        args = [str(cl), '/nologo', '/LD', '/O2', '/GS-', '/Zl', '/DORCSDR_GEOMETRY_EXPORTS',
                '/I'+str(include), '/I'+str(COMPONENT/'include'),
                str(COMPONENT/'orcsdr_display_geometry.c'), '/link', '/NOENTRY', '/NODEFAULTLIB', '/OUT:geometry.dll']
        run = subprocess.run(args, cwd=d, env=env, capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(d/'geometry.dll'))
        kernel = c.WinDLL('kernel32', use_last_error=True)
        kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        p, r, g = c.POINTER(Point), c.POINTER(Rect), c.POINTER(Geometry)
        cls.lib.orcsdr_geometry_init.argtypes = [g] + [c.c_int32]*7
        cls.lib.orcsdr_view_to_native.argtypes = [g, Point, p]
        cls.lib.orcsdr_native_to_view.argtypes = [g, Point, p]
        cls.lib.orcsdr_view_rect_to_native.argtypes = [g, Rect, r]
        cls.lib.orcsdr_touch_to_view.argtypes = [g, c.POINTER(Calibration), Point, p]
        for n in ('orcsdr_geometry_init', 'orcsdr_view_to_native', 'orcsdr_native_to_view',
                  'orcsdr_view_rect_to_native', 'orcsdr_touch_to_view'):
            getattr(cls.lib, n).restype = c.c_bool

    def geometry(self, width=568, height=1232, rotation=0, insets=(0, 0, 0, 0)):
        g = Geometry()
        self.assertTrue(self.lib.orcsdr_geometry_init(c.byref(g), width, height, rotation, *insets))
        return g

    def test_both_lilygo_orientations_and_existing_devices(self):
        for w, h in ((568, 1232), (1280, 720), (800, 480), (240, 240)):
            for angle in (0, 90, 180, 270):
                g = self.geometry(w, h, angle)
                self.assertEqual((g.width, g.height), (w, h) if angle%180==0 else (h, w))

    def test_clockwise_presentation_matches_existing_lilygo_mapping(self):
        g = self.geometry(rotation=90)
        for x, y, nx, ny in ((0,0,567,0), (1231,0,567,1231), (0,567,0,0), (1231,567,0,1231)):
            p = Point()
            self.assertTrue(self.lib.orcsdr_view_to_native(c.byref(g), Point(x,y), c.byref(p)))
            self.assertEqual((p.x,p.y), (nx,ny))

    def test_forward_inverse_all_pixels_and_all_rotations(self):
        # Exhaustive asymmetric small panel catches swapped extents/off-by-one at edges.
        for angle in (0,90,180,270):
            g = self.geometry(17,31,angle)
            for y in range(g.height):
                for x in range(g.width):
                    native, back = Point(), Point()
                    self.assertTrue(self.lib.orcsdr_view_to_native(c.byref(g),Point(x,y),c.byref(native)))
                    self.assertTrue(self.lib.orcsdr_native_to_view(c.byref(g),native,c.byref(back)))
                    self.assertEqual((back.x,back.y),(x,y))

    def test_physical_safe_area_rotates_with_panel(self):
        expected = {0:(3,5,548,1216),90:(5,17,1216,548),180:(17,11,548,1216),270:(11,3,1216,548)}
        for angle, rect in expected.items():
            g = self.geometry(rotation=angle,insets=(3,5,17,11))
            self.assertEqual(tuple(getattr(g.safe,k) for k in ('x','y','width','height')),rect)

    def test_reject_invalid_profiles_and_points_without_overwriting_output(self):
        for w,h,a,insets in ((0,10,0,(0,0,0,0)),(-1,10,0,(0,0,0,0)),
                             (10,10,45,(0,0,0,0)),(10,10,0,(5,0,5,0)),
                             (10,10,0,(-1,0,0,0)),(2147483647,10,0,(0,0,0,0))):
            g = Geometry();g.width=99
            self.assertFalse(self.lib.orcsdr_geometry_init(c.byref(g),w,h,a,*insets))
            self.assertEqual(g.width,99)
        g=self.geometry(rotation=90)
        for x,y in ((-1,0),(g.width,0),(0,g.height),(2147483647,2147483647)):
            p=Point(71,72)
            self.assertFalse(self.lib.orcsdr_view_to_native(c.byref(g),Point(x,y),c.byref(p)))
            self.assertEqual((p.x,p.y),(71,72))

    def test_dirty_rectangles_clip_and_rotate_without_losing_edge_pixels(self):
        for angle in (0,90,180,270):
            g=self.geometry(rotation=angle)
            for rect in (Rect(0,0,g.width,g.height),Rect(g.width-1,g.height-1,1,1),Rect(-5,-6,9,10)):
                n=Rect()
                self.assertTrue(self.lib.orcsdr_view_rect_to_native(c.byref(g),rect,c.byref(n)))
                self.assertGreater(n.width,0);self.assertGreater(n.height,0)
                self.assertGreaterEqual(n.x,0);self.assertGreaterEqual(n.y,0)
                self.assertLessEqual(n.x+n.width,g.native_width)
                self.assertLessEqual(n.y+n.height,g.native_height)
                if rect.x==g.width-1:self.assertEqual(n.width*n.height,1)
            n=Rect()
            self.assertFalse(self.lib.orcsdr_view_rect_to_native(c.byref(g),Rect(g.width,0,1,1),c.byref(n)))
            self.assertFalse(self.lib.orcsdr_view_rect_to_native(c.byref(g),Rect(0,0,-1,1),c.byref(n)))

    def test_raw_touch_endpoints_calibrate_then_inverse_rotate(self):
        cal=Calibration(-20,1039,10,2409,0,0,0)
        for angle in (0,90,180,270):
            g=self.geometry(rotation=angle)
            for raw,native in ((Point(-20,10),Point(0,0)),(Point(1039,2409),Point(567,1231))):
                expected,p=Point(),Point()
                self.assertTrue(self.lib.orcsdr_native_to_view(c.byref(g),native,c.byref(expected)))
                self.assertTrue(self.lib.orcsdr_touch_to_view(c.byref(g),c.byref(cal),raw,c.byref(p)))
                self.assertEqual((p.x,p.y),(expected.x,expected.y))
            self.assertFalse(self.lib.orcsdr_touch_to_view(c.byref(g),c.byref(cal),Point(1040,10),c.byref(Point())))

    def test_touch_swapping_and_mirroring_do_not_change_draw_mapping(self):
        g=self.geometry(rotation=90)
        cal=Calibration(0,2399,0,1059,1,1,0)
        p=Point()
        self.assertTrue(self.lib.orcsdr_touch_to_view(c.byref(g),c.byref(cal),Point(0,0),c.byref(p)))
        # swapped raw origin -> mirrored native top-right -> view top-left.
        self.assertEqual((p.x,p.y),(0,0))
        cal.x_max=cal.x_min
        self.assertFalse(self.lib.orcsdr_touch_to_view(c.byref(g),c.byref(cal),Point(0,0),c.byref(p)))


if __name__=='__main__':
    unittest.main()
