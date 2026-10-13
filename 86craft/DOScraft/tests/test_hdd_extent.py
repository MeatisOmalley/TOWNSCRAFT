"""Validate extent discovery before any guest reads bypass DOS file seeks."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'

class ExtentTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file(),'existing native Zig unavailable')
    def test_bounds_fragmentation_identity_and_faults(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-extent-') as folder:
            exe=Path(folder)/'test.exe'
            result=subprocess.run([str(ZIG),'cc','-O2','-std=gnu99','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'src/platform/dos'),str(ROOT/'tests/hdd_extent_test.c'),'-o',str(exe)],
                capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('PASS: bounded FAT16 extent',result.stdout)
