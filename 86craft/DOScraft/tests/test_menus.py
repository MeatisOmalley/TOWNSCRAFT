from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
import dos_sources
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class MenuTests(unittest.TestCase):
    def test_overlay_recipe_replays_after_release_overlays(self):
        original=(imported.VENDOR/'src/game.c').read_bytes()
        for flags in ({'graphics':True}, {'graphics':True,'hud_proof':True,'large_world':True,'stream_light':True}):
            data,recipe=dos_sources.adapt('game.c',original,**flags)
            replay=original
            for step in recipe:
                before,after=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(before),step['count'])
                replay=replay.replace(before,after)
            self.assertEqual(replay,data)
            self.assertIn(b'case GS_MENU: menu_key(k)',data)
            self.assertIn(b'state=helpReturn;',data)
            # Main-menu settings and help never render an uninitialized world.
            self.assertIn(b'GS_MENU==state && menuOrigin==GS_TITLE',data)

    @unittest.skipUnless(ZIG.is_file(),'existing native Zig unavailable')
    def test_navigation_and_single_slot_failures(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-menus-') as name:
            folder=Path(name)
            imported.stage_sources(imported.VENDOR,folder/'imported')
            exe=folder/'menus.exe'
            command=[ZIG,'cc','-target','x86-windows-gnu','-std=gnu99','-O2',
                     '-Wall','-Wextra','-Werror','-DDOS_FAR_FIELD','-I',folder/'imported/src',
                     '-I',ROOT/'src/platform/dos',ROOT/'tests/menus_native.c',
                     ROOT/'src/platform/dos/graphics_presets.c','-o',exe]
            result=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('PASS: menu navigation',result.stdout)


if __name__=='__main__': unittest.main()
