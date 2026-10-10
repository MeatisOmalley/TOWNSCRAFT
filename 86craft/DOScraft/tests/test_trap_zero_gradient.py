"""Independent original/candidate assembly images, not host timing evidence."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
import dos_sources
from test_edge_recip import EXPORTS
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class TrapZeroGradientTests(unittest.TestCase):
    def test_optional_overlay_is_exact_and_replayable(self):
        for name in imported.SELECTED:
            original=(imported.VENDOR/'src'/name).read_bytes()
            candidate,recipe=dos_sources.adapt(name,original,trap_zero_gradient=True)
            replay=original
            for step in recipe:
                old,new=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(old),step['count'])
                replay=replay.replace(old,new)
            self.assertEqual(candidate,replay)
            if name=='trap.S':
                self.assertNotEqual(candidate,original)
                self.assertEqual(candidate.count(b'testl\t%eax,%eax'),original.count(b'testl\t%eax,%eax')+2)
                self.assertEqual(candidate.count(b'\timull\t%ecx,%eax'),2)
            else: self.assertEqual((candidate,recipe),(original,[]))
        with self.assertRaises(ValueError):
            dos_sources.adapt('trap.S',b'unexpected',trap_zero_gradient=True)

    @unittest.skipUnless(ZIG.is_file(),'Native x86 Zig unavailable')
    def test_original_and_separate_candidate_traps_exact_images(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-trap-zero-') as directory:
            folder=Path(directory)
            imported.stage_sources(imported.VENDOR,folder/'imported')
            src=folder/'imported/src'
            original=(src/'trap.S').read_bytes()
            candidate,recipe=dos_sources.adapt('trap.S',original,trap_zero_gradient=True)
            self.assertTrue(recipe)
            reference=original
            for symbol in imported.TRAP_SYMBOLS:
                reference=reference.replace(('TRAP _'+symbol+',').encode(),
                                            ('TRAP _reference_'+symbol+',').encode())
            reference=reference.replace(b'_g_recip14(',b'_reference_g_recip14(')
            (folder/'reference.S').write_bytes(reference)
            (folder/'candidate.S').write_bytes(candidate)
            flags=[str(ZIG),'cc','-target','x86-windows-gnu','-mcpu=i486','-O2',
                '-std=gnu99','-fno-builtin','-Wall','-Wextra','-Werror',
                '-Wno-unused-function','-mno-80387','-mno-mmx','-mno-sse','-I',str(src)]
            def run(command):
                result=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=120)
                self.assertEqual(result.returncode,0,result.stdout+'\n'+result.stderr[-3000:])
                return result
            objects=[]
            sources=((src/'raster.c','reference',[f'-D{n}=reference_{n}' for n in (*EXPORTS,*imported.TRAP_SYMBOLS)]),
                (src/'raster.c','candidate',[]),(folder/'reference.S','reference-trap',[]),
                (folder/'candidate.S','candidate-trap',[]))
            for source,name,defines in sources:
                obj=folder/(name+'.o')
                run([*flags,*defines,'-c',source,'-o',obj]); objects.append(obj)
            for harness,message in (('raster_parity_test.c','PASS: 24000 polygons'),
                                    ('trap_zero_gradient_test.c','PASS: 16000 trapezoids')):
                exe=folder/(harness+'.exe')
                run([*flags,ROOT/'tests'/harness,*objects,'-o',exe])
                output=run([exe]).stdout
                self.assertIn(message,output); print(output.strip())


if __name__=='__main__': unittest.main()
