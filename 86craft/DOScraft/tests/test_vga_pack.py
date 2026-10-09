"""Native transport correctness test; does NOT establish an emulated VGA mode."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class VgaPackTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.exists(), 'existing native Zig toolchain not available')
    def test_complete_frame_roundtrip_and_bounds(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-vga-test-') as directory:
            executable = Path(directory) / 'vga-pack-test.exe'
            subprocess.run([str(ZIG), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src/platform/dos'),
                            str(ROOT / 'tests/vga_pack_test.c'),
                            str(ROOT / 'src/platform/dos/vga_pack.c'), '-o', str(executable)],
                           check=True, capture_output=True, timeout=120)
            result = subprocess.run([str(executable)], check=True, capture_output=True,
                                    text=True, timeout=10)
            self.assertIn('PASS: all 76800 indexed pixels', result.stdout)


if __name__ == '__main__':
    unittest.main()
