from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class ProvenRowsTests(unittest.TestCase):
    def test_exact_vga_reconstruction(self):
        with tempfile.TemporaryDirectory(prefix='dos-vga-proven-') as directory:
            for portable in (False, True):
                for vertical in (False, True):
                    with self.subTest(portable=portable,vertical=vertical):
                        exe = Path(directory)/f'proven-{int(portable)}-{int(vertical)}.exe'
                        subprocess.run([str(ZIG), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                            *(['-DDOS_VGA_PROVEN_PORTABLE'] if portable else []),
                            *(['-DDOS_VGA_PROVEN_VERTICAL'] if vertical else []),
                            '-I', str(ROOT/'src/platform/dos'),
                            str(ROOT/'tests/vga_proven_test.c'),
                            str(ROOT/'src/platform/dos/vga_proven.c'), '-o', str(exe)],
                            check=True, capture_output=True, timeout=120)
                        subprocess.run([str(exe)], check=True, capture_output=True, timeout=15)


if __name__ == '__main__':
    unittest.main()
