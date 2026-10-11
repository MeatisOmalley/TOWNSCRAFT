"""Exercise the DOS crafting grid against the pinned recipe table natively."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(), 'native Win32 Zig unavailable')
class CraftGridTests(unittest.TestCase):
    def test_recipes_outputs_and_item_return(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-crafting-') as directory:
            exe = Path(directory) / 'craft-grid-test.exe'
            compile_result = subprocess.run([
                str(ZIG), 'cc', '-target', 'x86-windows-gnu', '-std=gnu99',
                '-O2', '-fno-builtin', '-Wall', '-Wextra',
                '-I', str(ROOT / 'vendor/towns/src'),
                '-I', str(ROOT / 'src/platform/dos'),
                str(ROOT / 'tests/craft_grid_native.c'),
                str(ROOT / 'vendor/towns/src/inventory.c'), '-o', str(exe),
            ], capture_output=True, text=True, timeout=120)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS: all crafting patterns', result.stdout)
