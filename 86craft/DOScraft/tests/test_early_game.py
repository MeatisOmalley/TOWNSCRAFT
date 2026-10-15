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
        self.build_and_run('early_game_native.c', ('blocks.c', 'inventory.c', 'libc.c'),
                           'PASS: water')


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(), 'native Win32 Zig unavailable')
class HungerTests(unittest.TestCase):
    def test_hunger_drain_healing_starving_and_sprint(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-hunger-') as directory:
            folder = Path(directory)
            staged(folder, ('player.c', 'libc.c'))
            exe = folder / 'hunger.exe'
            compile_result = subprocess.run([
                str(ZIG), 'cc', '-target', 'x86-windows-gnu', '-std=gnu99', '-O2', '-fno-builtin',
                '-Wall', '-Wextra', '-I', str(folder), str(ROOT / 'tests/hunger_native.c'),
                str(folder / 'player.c'), str(folder / 'libc.c'), '-o', str(exe),
            ], capture_output=True, text=True, timeout=180)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS: hunger', result.stdout)


class EarlyTextureTests(unittest.TestCase):
    def test_tiles_match_the_texture_ids_and_regenerate(self):
        import dos_early_game
        import early_game_textures
        self.assertEqual(sorted(early_game_textures.names()),
                         sorted(dos_early_game.BLOCK_TEXTURES + dos_early_game.ITEM_TEXTURES))
        inc = (ROOT / 'assets/textures/early_game.inc').read_text()
        self.assertEqual(inc.count('    { /* '), len(early_game_textures.names()))
        assets = ROOT / 'build/checks/biome-texture-study/assets-unpacked.bin'
        if assets.exists():
            with tempfile.TemporaryDirectory(prefix='doscraft-early-tex-') as directory:
                early_game_textures.write(assets, directory)
                self.assertEqual((Path(directory) / 'early_game.inc').read_text(), inc)


if __name__ == '__main__':
    unittest.main()
