"""Early-game systems natively: tool wear, item moves and the save extension."""
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


def staged(folder, names):
    """Vendor headers plus the given sources, with the game build's overlays."""
    for header in VENDOR.glob('*.h'):
        shutil.copy(header, folder / header.name)
    for name in (*names, 'blocks.h', 'inventory.h', 'player.h', 'mobs.h', 'world.h'):
        data, _ = dos_sources.adapt(name, (VENDOR / name).read_bytes())
        (folder / name).write_bytes(data)


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(), 'native Win32 Zig unavailable')
class EarlyGameTests(unittest.TestCase):
    def build_and_run(self, test, sources, expect):
        with tempfile.TemporaryDirectory(prefix='doscraft-early-') as directory:
            folder = Path(directory)
            staged(folder, sources)
            exe = folder / 'early.exe'
            compile_result = subprocess.run([
                str(ZIG), 'cc', '-target', 'x86-windows-gnu', '-std=gnu99', '-O2',
                '-fno-builtin', '-Wall', '-Wextra', '-I', str(folder), '-I', str(ROOT / 'src/platform/dos'),
                str(ROOT / 'tests' / test), *(str(folder / s) for s in sources),
                str(ROOT / 'src/platform/dos/early_game.c'), '-o', str(exe),
            ], capture_output=True, text=True, timeout=180)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(expect, result.stdout)

    def test_tool_wear_and_save_extension(self):
        self.build_and_run('early_game_native.c', ('blocks.c', 'inventory.c'),
                           'PASS: tool wear and save extension')


if __name__ == '__main__':
    unittest.main()
