"""Native independent world-space oracle; actual DOS images checked separately."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
import dos_sources
import dos_wall_visibility
from test_edge_recip import ZIG
from test_camera_reuse import snippet
from test_split_reuse import frontend

class WallVisibilityTests(unittest.TestCase):
    def test_replay_is_optional_and_verified_variant_is_separate(self):
        for verify in (False,True):
            for name in imported.SELECTED:
                original=(imported.VENDOR/'src'/name).read_bytes()
                candidate,recipe=dos_sources.adapt(name,original,wall_visibility=True,wall_verify=verify)
                replay=original
                for step in recipe:
                    old,new=step['before'].encode(),step['after'].encode()
                    self.assertEqual(replay.count(old),step['count']); replay=replay.replace(old,new)
                self.assertEqual(candidate,replay)
                if name!='render.c': self.assertEqual((candidate,recipe),(original,[]))
                else: self.assertEqual(b'Diagnostic double-render' in candidate,verify)
                self.assertEqual(dos_sources.adapt(name,original),(original,[]))
        with self.assertRaises(ValueError): dos_sources.adapt('render.c',b'unexpected',wall_visibility=True)
        with self.assertRaises(ValueError): dos_sources.adapt('render.c',b'',wall_verify=True)
        with self.assertRaises(ValueError): dos_sources.adapt('render.c',b'',wall_visibility=True,front_to_back=True)

    def test_independent_ray_plane_reference(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-wall-cone-') as folder:
            exe=Path(folder)/'test.exe'
            result=subprocess.run([str(ZIG),'cc','-target','x86-windows-gnu','-O2','-std=c99','-UNDEBUG',
                '-I',str(ROOT/'src/platform/dos'),str(ROOT/'tests/wall_visibility_test.c'),'-o',str(exe)],
                capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS: 100000 independent ray/plane boxes',result.stdout)
            print(result.stdout.strip())

    def test_actual_source_selection_union_and_edit_invalidation(self):
        text=dos_wall_visibility.FRONTEND
        declarations=text[text.index('#define WALL_PATCHES'):text.index('static DosWallCone')]
        body=text[text.index('static int wall_patch_ready'):text.index('static void wall_prepare')]
        with tempfile.TemporaryDirectory(prefix='doscraft-wall-sources-') as folder:
            folder=Path(folder); (folder/'wall_sources.inc').write_text(declarations+body)
            exe=folder/'test.exe'
            result=subprocess.run([str(ZIG),'cc','-target','x86-windows-gnu','-O2','-std=c99','-UNDEBUG',
                '-I',str(folder),str(ROOT/'tests/wall_sources_test.c'),'-o',str(exe)],
                capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS: actual opaque source union preserves doorways',result.stdout)

    def test_actual_transforms_subdivision_raster_and_assembly(self):
        data=(imported.VENDOR/'src/render.c').read_bytes()
        text=data.decode().replace('\r\n','\n')
        corners=text[text.index('static const u8 faceCorner'):text.index('static int max_items_for_ram')]
        projection=text[text.index('int face_light_level('):text.index('static inline int to_screen(')]
        with tempfile.TemporaryDirectory(prefix='doscraft-wall-raster-') as folder:
            folder=Path(folder); imported.stage_sources(imported.VENDOR,folder/'imported')
            src=folder/'imported/src'
            (folder/'corners.inc').write_text(corners)
            (folder/'projection.inc').write_text(projection)
            (folder/'reference.inc').write_text(snippet(data,'reference',False)+frontend(data,'reference'))
            tables=folder/'tables.c'
            subprocess.run([sys.executable,str(imported.VENDOR/'tools/gentables.py'),str(tables)],check=True)
            exe=folder/'test.exe'
            result=subprocess.run([str(ZIG),'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-UNDEBUG',
                '-fno-builtin','-I',str(folder),'-I',str(src),'-I',str(ROOT/'src/platform/dos'),
                str(ROOT/'tests/wall_raster_test.c'),str(src/'raster.c'),str(src/'trap.S'),
                str(src/'fmath.c'),str(tables),'-o',str(exe)],capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr[-4000:])
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('real raster shadow-cone cases',result.stdout)
            print(result.stdout.strip())

if __name__=='__main__': unittest.main()
