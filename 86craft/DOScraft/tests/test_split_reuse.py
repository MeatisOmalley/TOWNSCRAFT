"""Actual adaptive frontend, projection, clipping and raster/assembly parity."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
import dos_sources
from test_camera_reuse import snippet,ZIG


def frontend(data,prefix):
    text=data.decode().replace('\r\n','\n')
    body=text[text.index('static int quad_in_frustum('):text.index('static void draw_model_box(')]
    names=('TX','TY','TZ','PX','PY','PZ','planeLen','camBX','camBY','camBZ',
        'draw_quad','draw_quad_split','quad_in_frustum','box_outside')
    pattern=re.compile(r'\b('+'|'.join(names)+r')\b')
    return pattern.sub(lambda m:prefix+'_'+m[0],body)


class SplitReuseTests(unittest.TestCase):
    def test_optional_replay_and_real_frontend_images(self):
        data=(imported.VENDOR/'src/render.c').read_bytes()
        candidate,recipe=dos_sources.adapt('render.c',data,split_reuse=True)
        replay=data
        for step in recipe:
            old,new=step['before'].encode(),step['after'].encode()
            self.assertEqual(replay.count(old),step['count']); replay=replay.replace(old,new)
        self.assertEqual(candidate,replay)
        self.assertEqual(dos_sources.adapt('render.c',data),(data,[]))
        with self.assertRaises(ValueError): dos_sources.adapt('render.c',b'unexpected',split_reuse=True)
        for name in imported.SELECTED:
            if name!='render.c':
                original=(imported.VENDOR/'src'/name).read_bytes()
                self.assertEqual(dos_sources.adapt(name,original,split_reuse=True),(original,[]))
        text=data.decode().replace('\r\n','\n')
        corners=text[text.index('static const u8 faceCorner'):text.index('static int max_items_for_ram')]
        projection=text[text.index('int face_light_level('):text.index('static inline int to_screen(')]
        projection=projection.replace('static inline void project(','static inline void actual_project(')
        projection=projection.replace('static void draw_spoly(','static void actual_spoly(')
        from dos_split_reuse import CACHE
        with tempfile.TemporaryDirectory(prefix='doscraft-split-reuse-') as folder:
            folder=Path(folder); imported.stage_sources(imported.VENDOR,folder/'imported')
            src=folder/'imported/src'
            for name,body in (('corners.inc',corners+CACHE),('projection.inc',projection),
                ('reference.inc',snippet(data,'reference',False)+frontend(data,'reference')),
                ('candidate.inc',snippet(candidate,'candidate',False)+frontend(candidate,'candidate'))):
                (folder/name).write_text(body)
            tables=folder/'tables.c'
            subprocess.run([sys.executable,str(imported.VENDOR/'tools/gentables.py'),str(tables)],check=True)
            exe=folder/'test.exe'
            command=[str(ZIG),'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-UNDEBUG',
                '-fno-builtin','-Wall','-Wextra','-Wno-unused-function','-I',str(folder),'-I',str(src),
                str(ROOT/'tests/split_reuse_test.c'),str(src/'raster.c'),str(src/'trap.S'),
                str(src/'fmath.c'),str(tables),'-o',str(exe)]
            result=subprocess.run(command,capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr[-5000:])
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=90)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS: 24000 real adaptive/full/off parents',result.stdout)
            print(result.stdout.strip())


if __name__=='__main__': unittest.main()
