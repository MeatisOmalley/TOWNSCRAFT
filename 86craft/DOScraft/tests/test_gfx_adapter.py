"""Pinned 2D bodies and native page/presentation semantics, not guest game tests."""
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_adapters
import build_imported as imported
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class GfxAdapterTests(unittest.TestCase):
    def test_changed_pinned_drawing_source_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-gfx-corrupt-') as directory:
            vendor = Path(directory) / 'vendor'
            shutil.copytree(imported.VENDOR, vendor)
            source = vendor / 'src/gfx.c'
            source.write_bytes(b'/* unexpected modification */\n' + source.read_bytes())
            with self.assertRaisesRegex(ValueError, 'provenance mismatch'):
                build_adapters.gfx_drawing_source(vendor)

    def test_pinned_drawing_selection_is_exact_and_hardware_free(self):
        portable, record = build_adapters.gfx_drawing_source()
        original = (imported.VENDOR / record['source']).read_bytes()
        self.assertEqual(portable, b''.join(original[a:b] for a, b in record['selections']))
        for name in ('clear', 'rect', 'frame', 'darken', 'char', 'text',
                     'text_shadow', 'text_center'):
            self.assertEqual(portable.count(('void gfx_' + name + '(').encode()), 1)
        for name in ('present', 'init', 'wait_flip', 'sync_pages'):
            self.assertNotIn(('void gfx_' + name + '(').encode(), portable)

    @unittest.skipUnless(ZIG.is_file(), 'existing native Zig unavailable')
    def test_three_page_hud_and_partial_redraw_contract(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-gfx-native-') as directory:
            directory = Path(directory)
            build_adapters.stage(directory)
            exe = directory / 'gfx-test.exe'
            command = [ZIG, 'cc', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
                       '-Wall', '-Wextra', '-Werror', '-I', directory / 'src',
                       '-I', ROOT / 'src/platform/dos', ROOT / 'tests/gfx_adapter_test.c',
                       ROOT / 'src/platform/dos/gfx.c', directory / 'src/font.c', '-o', exe]
            result = subprocess.run(list(map(str, command)), capture_output=True,
                                    text=True, errors='replace', timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('PASS: three pages, retained HUD', result.stdout)

    @unittest.skipUnless(imported.COMPILER.is_file(), 'existing DJGPP unavailable')
    def test_dos_adapters_export_real_game_gfx_api(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-adapters-dos-') as directory:
            report = build_adapters.build(Path(directory))
            gfx = next(obj for obj in report['objects'] if obj['path'] == 'obj/gfx.o')
            for name in ('init', 'clear', 'present', 'back_page', 'wait_flip', 'sync_pages',
                         'rect', 'frame', 'darken', 'char', 'text', 'text_shadow', 'text_center'):
                self.assertEqual(gfx['symbols']['_gfx_' + name], 'T')
            self.assertEqual(gfx['symbols']['_dos_video_present'], 'U')
            save = next(obj for obj in report['objects'] if obj['path']=='obj/save.o')
            for name in ('save_world','load_world','save_loaded_mobs','save_stream_tick',
                         'save_error_text','dos_save_shutdown'):
                self.assertEqual(save['symbols']['_'+name],'T')
            for name in ('world_commit_columns','mobs_save','mobs_load','__dpmi_int'):
                self.assertEqual(save['symbols']['_'+name],'U')
            audio = next(obj for obj in report['objects'] if obj['path']=='obj/sound_assets.o')
            for name in ('dos_sound_assets_build','dos_sound_wave','dos_sound_sample','dos_sound_events'):
                self.assertEqual(audio['symbols']['_'+name],'T')
            self.assertEqual(audio['symbols']['_g_sinTab'],'U')
            for name in ('_outb','_inb','_cli','_sti','_fm_write'):
                self.assertNotIn(name,audio['symbols'])
            self.assertFalse(report['playable'])
            self.assertFalse(report['linked'])


if __name__ == '__main__':
    unittest.main()
