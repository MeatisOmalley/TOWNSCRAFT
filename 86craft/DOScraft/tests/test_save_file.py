"""Actual save adapter, codec provenance, native short-I/O and fault tests."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_adapters
import build_imported as imported
from save_probe_reference import saveio_expected_image
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class SaveFileTests(unittest.TestCase):
    def test_pinned_codec_transformations_are_exact_and_replayable(self):
        portable, record = build_adapters.save_codec_source()
        data = (imported.VENDOR / record['source']).read_bytes()
        replay = b''.join(data[a:b] for a,b in record['selections'])
        for step in record['transformations']:
            before, after = step['before'].encode(), step['after'].encode()
            self.assertEqual(replay.count(before), step['count'])
            replay = replay.replace(before,after)
        self.assertEqual(replay,portable)
        self.assertEqual(imported.sha256(portable),record['staged_sha256'])
        replacements = [s for s in record['transformations'] if s['before']!='\r\n']
        self.assertEqual(len(replacements),3)
        self.assertIn(b'world_cache_active() && g_W<256 ? SAVE_VERSION : 3',portable)
        self.assertIn(b'No disk in drive A',portable)
        for token in (b'inb(',b'outb(',b'dma_setup('):
            self.assertNotIn(token,portable)

    def test_changed_pinned_save_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            vendor = Path(name)/'vendor'
            shutil.copytree(imported.VENDOR,vendor)
            (vendor/'src/save.c').write_bytes(b'/* wrong source */')
            with self.assertRaisesRegex(ValueError,'provenance mismatch'):
                build_adapters.save_codec_source(vendor)

    @unittest.skipUnless(ZIG.is_file(),'existing native Zig unavailable')
    def test_transport_codec_bounds_short_io_and_failures(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-save-test-') as name:
            folder=Path(name)
            build_adapters.stage(folder)
            (folder/'dpmi.h').write_text('typedef struct { struct { unsigned short ax,bx,flags; } x; } __dpmi_regs;\nint __dpmi_int(int, __dpmi_regs *);\n')
            exe=folder/'save-test.exe'
            command=[ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99',
                     '-Wall','-Wextra','-Werror','-I',folder,'-I',folder/'src',
                     '-I',ROOT/'src/platform/dos',ROOT/'tests/save_file_test.c','-o',exe]
            result=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr)
            image=folder/'codec-result.bin'
            result=subprocess.run([str(exe),str(image)],capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('PASS: DOS save transport and pinned codec fixtures',result.stdout)
            self.assertEqual(image.read_bytes(),saveio_expected_image())
