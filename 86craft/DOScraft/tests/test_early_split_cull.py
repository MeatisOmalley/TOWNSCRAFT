"""Conservative parent rejection proven against the real descendant gate."""
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


class SplitCullTests(unittest.TestCase):
    def test_exact_overlay_and_real_fixed_point_descendants(self):
        data=(imported.VENDOR/'src/render.c').read_bytes()
        candidate,recipe=dos_sources.adapt('render.c',data,early_split_cull=True)
        replay=data
        for step in recipe:
            old,new=step['before'].encode(),step['after'].encode()
            self.assertEqual(replay.count(old),step['count']); replay=replay.replace(old,new)
        self.assertEqual(replay,candidate)
        self.assertEqual(dos_sources.adapt('render.c',data),(data,[]))
        for name in imported.SELECTED:
            if name!='render.c':
                original=(imported.VENDOR/'src'/name).read_bytes()
                self.assertEqual(dos_sources.adapt(name,original,early_split_cull=True),(original,[]))
        with self.assertRaises(ValueError):
            dos_sources.adapt('render.c',b'unexpected',early_split_cull=True)
        text=candidate.decode().replace('\r\n','\n')
        # Extract the actual gate and original leaf gate. No retyped arithmetic.
        start=text.index('static int split_parent_visible(')
        end=text.index('static void draw_quad(',start)
        gates=text[start:end]
        gates=re.sub(r'\b(PX|PY|PZ)\b',lambda m:'reference_'+m[0],gates)
        self.assertLess(text.index('if(!split_parent_visible'),text.index('if(g_textureSubdivision==SUBDIV_FULL)'))
        with tempfile.TemporaryDirectory(prefix='doscraft-split-gate-') as folder:
            folder=Path(folder); imported.stage_sources(imported.VENDOR,folder/'imported')
            (folder/'camera_reference.inc').write_text(snippet(data,'reference',False))
            (folder/'split_gate.inc').write_text(gates)
            tables=folder/'tables.c'
            subprocess.run([sys.executable,str(imported.VENDOR/'tools/gentables.py'),str(tables)],check=True)
            exe=folder/'test.exe'
            command=[str(ZIG),'cc','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wall','-Wextra',
                '-I',str(folder),'-I',str(folder/'imported/src'),str(ROOT/'tests/early_split_cull_test.c'),
                str(folder/'imported/src/fmath.c'),str(tables),'-o',str(exe)]
            result=subprocess.run(command,capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('PASS: 120000 real parent gates',result.stdout)


if __name__=='__main__': unittest.main()
