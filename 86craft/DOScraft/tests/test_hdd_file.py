"""Compile the real DOS file adapter with bounded native I/O fault mocks."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported


class TerrainFileTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file(), 'existing native Zig unavailable')
    def test_request_contract_bounds_and_faults(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-hdd-test-') as name:
            folder = Path(name)
            imported.stage_sources(imported.VENDOR, folder / 'imported')
            (folder / 'dpmi.h').write_text('typedef struct { struct { unsigned short ax,bx,flags; } x; } __dpmi_regs;\nint __dpmi_int(int, __dpmi_regs *);\n')
            executable = folder / 'test.exe'
            command = [str(ZIG), 'cc', '-target', 'x86-windows-gnu', '-O2', '-std=gnu99',
                       '-Wall', '-Wextra', '-Werror', '-I', str(folder), '-I', str(folder / 'imported/src'),
                       '-I', str(ROOT / 'src/platform/dos'),
                       str(ROOT / 'tests/hdd_file_test.c'), '-o', str(executable)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('PASS: DOS terrain transport', result.stdout)
