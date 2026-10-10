from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'

class VideoTransportTests(unittest.TestCase):
    def test_real_presenter_reference_and_all_plane_candidate(self):
        for flags in ([],['-DDOS_VGA_PACK_ALL'],['-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE'],
                      ['-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST'],
                      ['-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_VGA_PACK_WORDS'],
                      ['-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer'],
                      ['-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_ET4000_CHUNKY']):
            with self.subTest(flags=flags),tempfile.TemporaryDirectory(prefix='doscraft-present-test-') as directory:
                exe=Path(directory)/'test.exe'
                command=[str(ZIG),'cc','-target','x86-windows-gnu','-O2','-Wall','-Wextra','-Werror',*flags,
                    '-I',str(ROOT/'tests/video_stubs'),'-I',str(ROOT/'src/platform/dos'),
                    str(ROOT/'tests/video_transport_test.c'),str(ROOT/'src/platform/dos/video.c'),
                    str(ROOT/'src/platform/dos/vga_pack.c'),str(ROOT/'src/platform/dos/vga_pack_all.c'),
                    str(ROOT/'src/platform/dos/vga_hud_cache.c'),
                    str(ROOT/'src/platform/dos/vga_upload_plan.c'),
                    str(ROOT/'src/platform/dos/vga_pack_words.c'),
                    '-o',str(exe)]
                process=subprocess.run(command,capture_output=True,text=True,timeout=120)
                self.assertEqual(process.returncode,0,process.stderr)
                process=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
                self.assertEqual(process.returncode,0,process.stderr)
                self.assertIn('PASS: real presenter',process.stdout)

if __name__=='__main__': unittest.main()
