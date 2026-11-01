"""Compile the real shared renderer/input against a recording display."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class KeypadTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp=tempfile.TemporaryDirectory();cls.addClassCleanup(temp.cleanup)
        d=Path(temp.name)
        tools=Path(r'C:/Program Files/Microsoft Visual Studio/18/Community/VC')
        # Capture the developer environment privately; never print environment values.
        batch=tools/'Auxiliary/Build/vcvars64.bat'
        result=subprocess.run(f'cmd.exe /d /s /c ""{batch}" >nul && set"',
                              text=True,capture_output=True)
        if result.returncode:raise AssertionError('MSVC developer environment activation failed')
        env=dict(os.environ)
        for line in result.stdout.splitlines():
            if '=' in line:
                k,v=line.split('=',1)
                if k.upper() in {'INCLUDE','LIB','LIBPATH','PATH'}:env[k]=v
        cl=tools/'Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe'
        ui=ROOT/'apps/orcsdr-tab5/ui'
        run=subprocess.run([str(cl),'/nologo','/std:c++17','/EHsc','/MD','/LD',
            '/I'+str(ROOT/'tests/freq_keypad_ui_stubs'),'/I'+str(ui),
            str(ROOT/'tests/freq_keypad_recording_test.cpp'),str(ROOT/'tests/dashboard_header_render_fixture.cpp'),
            str(ui/'freq_keypad.cpp'),str(ui/'focus_nav.cpp'),
            '/link','/OUT:keypad.dll'],cwd=d,env=env,text=True,capture_output=True)
        if run.returncode:raise AssertionError(run.stdout+run.stderr)
        cls.lib=c.CDLL(str(d/'keypad.dll'))
        kernel=c.WinDLL('kernel32');kernel.FreeLibrary.argtypes=[c.c_void_p]
        cls.addClassCleanup(lambda:kernel.FreeLibrary(cls.lib._handle))
        cls.lib.check_keypad.argtypes=[c.c_int,c.c_int,c.c_int]
        cls.lib.check_header_renderer.argtypes=[c.c_int,c.c_int,c.c_int]
        cls.lib.check_combined_controls.argtypes=[c.c_int,c.c_int,c.c_int]

    def test_existing_tab5_key_positions_unchanged(self):
        self.assertEqual(self.lib.check_tab5_legacy(),0)

    def test_focus_uses_viewport_center_and_invalidates_old_regions(self):
        self.assertEqual(self.lib.check_focus_viewport(),0)

    def test_header_geometry_for_portrait_landscape_and_legacy_profiles(self):
        self.assertEqual(self.lib.check_header_layouts(),0)

    def test_real_header_draw_touch_progress_and_cache_invalidation(self):
        for w,h,safe in [(1280,720,0),(1232,568,1),(568,1232,1),(800,480,0)]:
            with self.subTest(width=w,height=h,safe=safe):
                self.assertEqual(self.lib.check_header_renderer(w,h,safe),0)

    def test_common_header_and_keypad_share_viewport_and_focus(self):
        for w,h,safe in [(1280,720,0),(1232,568,1),(568,1232,1),(800,480,0)]:
            with self.subTest(width=w,height=h,safe=safe):
                self.assertEqual(self.lib.check_combined_controls(w,h,safe),0)

    def test_home_regions_fit_without_overlaps_and_preserve_legacy(self):
        self.assertEqual(self.lib.check_home_layouts(),0)

    def test_same_renderer_input_and_focus_in_both_orientations(self):
        for w,h,safe in [(1232,568,0),(568,1232,0),(1232,568,1),(568,1232,1),
                         (1280,720,0),(800,480,0),(720,720,0)]:
            with self.subTest(width=w,height=h,safe=safe):
                self.assertEqual(self.lib.check_keypad(w,h,safe),0)

if __name__=='__main__':unittest.main()
