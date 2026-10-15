"""Bounded overlay and native policy/control checks; no guest claims."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported
import dos_graphics

ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
SOURCE = ROOT / 'vendor/towns/src/game.c'
PLATFORM = ROOT / 'src/platform/dos'


def section(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


class TransformTests(unittest.TestCase):
    def setUp(self):
        self.original = SOURCE.read_bytes().replace(b'\r\n', b'\n')
        self.transformed, self.recipe = dos_graphics.adapt('game.c', self.original)

    def test_only_authored_game_overlay_and_vendor_unchanged(self):
        paths = sorted(p for p in imported.VENDOR.rglob('*') if p.is_file())
        hashes = {p: hashlib.sha256(p.read_bytes()).digest() for p in paths}
        for name in imported.SELECTED:
            data = (imported.VENDOR / 'src' / name).read_bytes()
            result, recipe = dos_graphics.adapt(name, data)
            if name == 'game.c':
                self.assertNotEqual(result, data)
                self.assertTrue(recipe)
            else:
                self.assertEqual((result, recipe), (data, []))
        for name in ('src/game.c', 'other/game.c', 'GAME.C', 'game.h', 'new_game.c'):
            self.assertEqual(dos_graphics.adapt(name, b'anything\r\n'), (b'anything\r\n', []))
        self.assertEqual(hashes, {p: hashlib.sha256(p.read_bytes()).digest() for p in paths})

    def test_exact_count_one_recipe_replays_lf_and_crlf(self):
        for ending in (b'\n', b'\r\n'):
            original = self.original.replace(b'\n', ending)
            result, recipe = dos_graphics.adapt('game.c', original)
            replay = original
            self.assertEqual(len(recipe), 11)
            for step in recipe:
                self.assertEqual(set(step), {'operation', 'before', 'after', 'count'})
                self.assertEqual(step['operation'], 'replace')
                self.assertEqual(step['count'], 1)
                before, after = step['before'].encode(), step['after'].encode()
                self.assertEqual(replay.count(before), 1)
                replay = replay.replace(before, after, 1)
            self.assertEqual(replay, result)
            self.assertEqual(result.replace(b'\r\n', b'\n'), self.transformed)

    def test_every_changed_or_duplicate_anchor_fails_closed(self):
        for before, _ in dos_graphics.REPLACEMENTS:
            with self.subTest(anchor=before[:60]):
                anchor = before.encode()
                self.assertEqual(self.original.count(anchor), 1)
                for bad in (self.original.replace(anchor, b'/* unexpected */\n', 1),
                            self.original + anchor):
                    with self.assertRaisesRegex(ValueError, 'expected exactly one'):
                        dos_graphics.adapt('game.c', bad)
        with self.assertRaises(ValueError):
            dos_graphics.adapt('game.c', self.transformed)
        for bad in (self.original.replace(b'\n', b'\r\n', 1),
                    self.original.replace(b'\n', b'\r')):
            with self.assertRaisesRegex(ValueError, 'line endings'):
                dos_graphics.adapt('game.c', bad)

    def test_unrelated_code_is_byte_identical(self):
        old, new = self.original.decode(), self.transformed.decode()
        # Mask only the permitted graphics settings/help regions, independently
        # of the replacement recipe. All remaining game bytes must match.
        for old_start, new_start, end in (
            ('/* View distance presets (PF3)', '/* DOS graphics policy:', 'static u32 fpsFrames'),
            ('static void draw_help(void)', 'static void draw_help(void)',
             '/* ---------------- Crafting screen ---------------- */'),
            ('static void auto_detect_quality(void)', 'static void init_graphics_quality(void)',
             '/* ---------------- Main loop ---------------- */'),
        ):
            old = old.replace(section(old, old_start, end), '')
            new = new.replace(section(new, new_start, end), '')
        new = new.replace('#include "graphics_presets.h"\n', '')
        new = new.replace('\tinit_graphics_quality();\n', '\tauto_detect_quality();\n')
        self.assertEqual(old, new)
        original = self.original.decode()
        transformed = self.transformed.decode()
        # Diagnostics and benchmark override stay exact; controls are tested
        # below against actual transformed C, independently of the policy API.
        diagnostic = section(original, '\t/* Rough CPU speed:', '\t/* Measured in Tsugaru:')
        self.assertIn(diagnostic, transformed)
        bench = section(original, '#ifdef BENCH_FLAT\n', '\tsound_init();')
        self.assertIn(bench, transformed)
        for key, next_key in (('PF6', 'PF7'), ('PF7', 'PF9'), ('PF4', 'PF8')):
            block = section(original, f'\tcase KEY_{key}:', f'\tcase KEY_{next_key}:')
            self.assertIn(block, transformed)
        self.assertIn(section(original, '\tcase KEY_PF9:', '\tcase KEY_PF3:'), transformed)
        self.assertIn(section(original, '\tcase KEY_PF5:',
                              '/* ---------------- Main loop ---------------- */'), transformed)

    def test_help_changes_only_graphics_rows_and_retains_debug_label(self):
        old = section(self.original.decode(), 'static void draw_help(void)',
                      '/* ---------------- Crafting screen ---------------- */')
        new = section(self.transformed.decode(), 'static void draw_help(void)',
                      '/* ---------------- Crafting screen ---------------- */')
        old_lines = re.findall(r'^\t\t"(.*)",$', old, re.M)
        new_lines = re.findall(r'^\t\t"(.*)",$', new, re.M)
        expected = old_lines[:]
        expected[11] = 'F2 Graphics: Low/Med/High/Very Low'
        expected[13] = 'PF4 Debug'
        self.assertEqual(new_lines, expected)
        self.assertIn('gfx_text_shadow(g_fb,132,206,dos_graphics_message(graphicsPresetSel)', new)
        self.assertLessEqual(132 + len('Graphics: Medium') * 8 + 1, 300)
        self.assertLess(30 + len('PF4 Debug') * 8, 132)


CONTROL_HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "sys.h"
#include "graphics_presets.h"
int g_viewDist,g_renderScale,g_flatDist,g_distanceMode,g_occlusion,g_textureSubdivision;
int g_musicOn,g_sfxOn,g_profEnable;
int dos_cpu386;
u32 g_profCount;
enum { GS_TITLE, GS_PLAY };
static int state,showDebug,saves,message_count;
static u32 cpuSpeedIndex,fake_ticks;
static const char *message;
static void game_message(const char *s) { message=s; ++message_count; }
static void save_game(void) { ++saves; }
static void render_sync_texture_resolution(void) { }
#include "graphics_apply.inc"
#define g_ticks (++fake_ticks)
#include "graphics_init.inc"
#undef g_ticks
#include "graphics_keys.inc"
static void check(int scale,int view,int flat) {
    assert(g_renderScale==scale && g_viewDist==view && g_flatDist==flat);
    assert(!g_distanceMode && !g_occlusion && g_textureSubdivision==SUBDIV_ADAPTIVE);
}
int main(void) {
    int cycle,key,old_count;
    static const int disabled[]={KEY_PF3,KEY_PF8,KEY_PF10};
    static const char *const expected[]={"Graphics: Medium","Graphics: High","Graphics: Very Low","Graphics: Low"};
    g_renderScale=g_viewDist=g_flatDist=g_distanceMode=g_occlusion=g_textureSubdivision=99;
    init_graphics_quality(); check(2,8,0);
    assert(graphicsPresetSel==DOS_GRAPHICS_LOW && !dos_very_low_mode && cpuSpeedIndex>0 && !message_count);
    for(cycle=0;cycle<30;++cycle) {
        for(key=0;key<3;++key) {
            old_count=message_count; settings_key(disabled[key]);
            check(2,8,0); assert(message_count==old_count);
        }
        g_occlusion=1; settings_key(KEY_PF2); check(2,8,0);
        assert(!strcmp(message,expected[0]));
        for(key=0;key<3;++key) {
            old_count=message_count; settings_key(disabled[key]);
            check(2,8,0); assert(message_count==old_count);
        }
        settings_key(KEY_PF2); check(1,8,0); assert(!strcmp(message,expected[1]));
        for(key=0;key<3;++key) {
            old_count=message_count; settings_key(disabled[key]);
            check(1,8,0); assert(message_count==old_count);
        }
        settings_key(KEY_PF2); check(2,8,0); assert(dos_very_low_mode);
        assert(!strcmp(message,expected[2]));
        settings_key(KEY_PF2); check(2,8,0); assert(!dos_very_low_mode);
        assert(!strcmp(message,expected[3]));
    }
    assert(message_count==120);
    settings_key(KEY_PF4); assert(showDebug); settings_key(KEY_PF4); assert(!showDebug);
    g_profCount=123; settings_key(KEY_PF5); assert(g_profEnable && !g_profCount);
    assert(!strcmp(message,"Profiler on"));
    g_profCount=456; settings_key(KEY_PF5); assert(!g_profEnable && !g_profCount);
    settings_key(KEY_PF6); assert(g_musicOn && !strcmp(message,"Music on"));
    settings_key(KEY_PF6); assert(!g_musicOn && !strcmp(message,"Music off"));
    settings_key(KEY_PF7); assert(g_sfxOn && !strcmp(message,"Sound effects on"));
    settings_key(KEY_PF7); assert(!g_sfxOn && !strcmp(message,"Sound effects off"));
    settings_key(KEY_PF9); assert(!saves);
    state=GS_PLAY; settings_key(KEY_PF9); assert(saves==1);
    check(2,8,0);
    dos_cpu386=1;
    old_count=message_count;
    init_graphics_quality(); check(2,4,0);
    assert(graphicsPresetSel==DOS_GRAPHICS_VERY_LOW && dos_very_low_mode && dos_potato_mode);
    assert(message_count==old_count);
    settings_key(KEY_PF2); check(2,8,4);
    assert(graphicsPresetSel==DOS_GRAPHICS_LOW && dos_very_low_mode && !dos_potato_mode);
    settings_key(KEY_PF2); check(2,4,0);
    assert(graphicsPresetSel==DOS_GRAPHICS_VERY_LOW && dos_very_low_mode && dos_potato_mode);
    dos_cpu386=0;
    init_graphics_quality(); check(2,8,0);
    assert(graphicsPresetSel==DOS_GRAPHICS_LOW && !dos_very_low_mode && !dos_potato_mode);
    puts("PASS: transformed Low startup, F2 cycles, disabled keys, retained controls; 386 Very Low startup");
    return 0;
}
'''


class CompiledTests(unittest.TestCase):
    def run_command(self, command, timeout=120):
        result = subprocess.run(list(map(str, command)), capture_output=True,
                                text=True, errors='replace', timeout=timeout)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    @unittest.skipUnless(ZIG.is_file(), 'existing native Zig unavailable')
    def test_native_pure_policy_and_actual_transformed_controls(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-graphics-native-') as folder:
            folder = Path(folder)
            imported.stage_sources(imported.VENDOR, folder / 'imported')
            data, _ = dos_graphics.adapt('game.c', SOURCE.read_bytes())
            text = data.decode().replace('\r\n', '\n')
            for name, start, end in (
                ('apply', '/* DOS graphics policy:', 'static u32 fpsFrames'),
                ('init', 'static void init_graphics_quality(void)', 'static void settings_key(int k)'),
                ('keys', 'static void settings_key(int k)',
                 '/* ---------------- Main loop ---------------- */'),
            ):
                (folder / f'graphics_{name}.inc').write_text(section(text, start, end))
            (folder / 'controls.c').write_text(CONTROL_HARNESS)
            for name, source, expected in (
                ('policy', ROOT / 'tests/graphics_presets_test.c', 'PASS: pure graphics policy'),
                ('controls', folder / 'controls.c', 'PASS: transformed Low startup'),
            ):
                exe = folder / f'{name}.exe'
                self.run_command([ZIG, 'cc', '-target', 'x86-windows-gnu', '-std=gnu99', '-O2',
                                  '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-I', folder,
                                  '-I', folder / 'imported/src', '-I', PLATFORM,
                                  source, PLATFORM / 'graphics_presets.c', '-o', exe])
                self.assertIn(expected, self.run_command([exe], timeout=10))

    @unittest.skipUnless(imported.COMPILER.is_file(), 'existing DJGPP unavailable')
    def test_djgpp_compiles_full_transformed_game_and_policy(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-graphics-dos-') as folder:
            folder = Path(folder)
            imported.stage_sources(imported.VENDOR, folder / 'imported')
            data, _ = dos_graphics.adapt('game.c', SOURCE.read_bytes())
            (folder / 'game.c').write_bytes(b'void render_sync_texture_resolution(void);\n'+data)
            for source in (folder / 'game.c', PLATFORM / 'graphics_presets.c'):
                self.run_command([imported.COMPILER, *imported.CFLAGS,
                                  '-I', folder / 'imported/src', '-I', PLATFORM,
                                  '-c', source, '-o', folder / f'{source.stem}.o'])
                self.assertEqual(imported.coff_object((folder / f'{source.stem}.o').read_bytes())
                                 ['format'], 'coff-i386')


if __name__ == '__main__':
    unittest.main()
