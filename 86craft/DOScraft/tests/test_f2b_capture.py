"""Actual raster/assembly image oracle for bounded exact-order capture."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class CaptureTests(unittest.TestCase):
    def test_real_painter_reverse_opaque_oracle_and_bounds(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-f2b-capture-') as folder:
            folder=Path(folder); imported.stage_sources(imported.VENDOR,folder/'imported')
            src=folder/'imported/src'; exe=folder/'test.exe'
            command=[str(ZIG),'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-UNDEBUG',
                '-fno-builtin','-Wall','-Wextra','-Wno-unused-function','-I',str(src),
                '-I',str(ROOT/'src/platform/dos'),str(ROOT/'tests/f2b_capture_test.c'),
                str(ROOT/'src/platform/dos/f2b_capture.c'),str(ROOT/'src/platform/dos/coverage_spans.c'),
                str(src/'raster.c'),str(src/'trap.S'),'-o',str(exe)]
            result=subprocess.run(command,capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('PASS: 48 actual-assembly scenes',result.stdout)
            # Also compile the actual target object; native proof is not enough.
            subprocess.run([str(imported.COMPILER),*imported.CFLAGS,'-I',str(src),
                '-I',str(ROOT/'src/platform/dos'),'-c',str(ROOT/'src/platform/dos/f2b_capture.c'),
                '-o',str(folder/'capture.o')],check=True,capture_output=True,text=True)


if __name__=='__main__': unittest.main()
