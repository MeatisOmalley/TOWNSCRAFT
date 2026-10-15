"""6-bit block IDs: exact cell round trip, unchanged baked bytes, torch/door splits."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
VENDOR = ROOT / 'vendor/towns/src'
sys.path.insert(0, str(ROOT / 'tools'))
import dos_sources  # noqa: E402


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(), 'native Win32 Zig unavailable')
class BlockIdTests(unittest.TestCase):
    def test_cells_doors_torches_and_items(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-block-ids-') as directory:
            folder = Path(directory)
            for header in VENDOR.glob('*.h'):
                shutil.copy(header, folder / header.name)
            # The same overlays the game build applies to these files.
            for name in ('blocks.h', 'blocks.c'):
                data, _ = dos_sources.adapt(name, (VENDOR / name).read_bytes())
                (folder / name).write_bytes(data)
            exe = folder / 'block-ids-test.exe'
            compile_result = subprocess.run([
                str(ZIG), 'cc', '-target', 'x86-windows-gnu', '-std=gnu99', '-O2',
                '-fno-builtin', '-Wall', '-Wextra', '-I', str(folder),
                str(ROOT / 'tests/block_ids_native.c'), str(folder / 'blocks.c'), '-o', str(exe),
            ], capture_output=True, text=True, timeout=120)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS: 6-bit block ids', result.stdout)

    def test_overlay_applies_once_to_every_game_source(self):
        for name in ('blocks.h', 'blocks.c', 'world.h', 'world.c', 'game.c', 'render.c'):
            data, steps = dos_sources.adapt(name, (VENDOR / name).read_bytes())
            self.assertTrue(steps, name)
            self.assertNotIn(b'((b)&31)', data, name)


if __name__ == '__main__':
    unittest.main()
