"""Actual original/candidate collectors, immutable-source recipe checks."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import dos_sources
import build_imported as imported
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'

class GroupFacingTests(unittest.TestCase):
    def test_optional_recipe_exact_and_replayable(self):
        for name in imported.SELECTED:
            data=(imported.VENDOR/'src'/name).read_bytes()
            default=dos_sources.adapt(name,data)
            result,recipe=dos_sources.adapt(name,data,group_facing=True)
            replay=data
            for step in recipe:
                old,new=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(old),step['count'])
                replay=replay.replace(old,new)
            self.assertEqual(replay,result)
            if name!='render.c': self.assertEqual((result,recipe),default)

    def test_actual_collectors_identical_items_keys_and_caps(self):
        data=(imported.VENDOR/'src/render.c').read_bytes()
        text=data.decode().replace('\r\n','\n')
        candidate=dos_sources.adapt('render.c',data,group_facing=True)[0].decode()
        with tempfile.TemporaryDirectory(prefix='doscraft-group-facing-') as directory:
            directory=Path(directory)
            helpers=text[text.index('static inline int ord_slice'):text.index('static void sort_items')]
            (directory/'collect_helpers.inc').write_text(helpers)
            for kind,body in (('reference',text),('candidate',candidate)):
                collect=body[body.index('#define PLANE2'):body.index('/* The proof map follows')]
                collect=collect.replace('static void collect(void)',f'static void collect_{kind}(void)')
                if kind=='candidate': collect=collect.replace('q=gend; continue;','++earlyGroups; q=gend; continue;',1)
                (directory/f'collect_{kind}.inc').write_text(collect)
            exe=directory/'test.exe'
            result=subprocess.run([str(ZIG),'cc','-target','x86-windows-gnu','-O2','-fno-builtin',
                '-Wall','-Wextra','-Werror','-Wno-unused-function','-I',str(directory),
                '-I',str(imported.VENDOR/'src'),str(ROOT/'tests/group_facing_test.c'),'-o',str(exe)],
                capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('PASS: 6000 actual collector',result.stdout)

if __name__=='__main__': unittest.main()
