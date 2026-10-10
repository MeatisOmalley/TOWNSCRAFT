"""Independent original/candidate assembly images, not host timing evidence."""
from pathlib import Path
import subprocess
import struct
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
        self.assembly_parity(trap_zero_gradient=True)

    @unittest.skipUnless(ZIG.is_file(),'Native x86 Zig unavailable')
    def test_destination_reuse_original_and_separate_traps_exact_images(self):
        self.assembly_parity(trap_destination=True)

    def test_stable_gradient_original_and_separate_traps_exact_images(self):
        self.assembly_parity(trap_stable=True)

    def test_four_pixel_original_and_separate_traps_exact_images(self):
        self.assembly_parity(trap_four=True)

    def test_four_pixel_overlay_is_optional_and_replayable(self):
        for name in imported.SELECTED:
            original=(imported.VENDOR/'src'/name).read_bytes()
            candidate,recipe=dos_sources.adapt(name,original,trap_four=True)
            replay=original
            for step in recipe:
                old,new=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(old),step['count']); replay=replay.replace(old,new)
            self.assertEqual(candidate,replay)
            if name!='trap.S': self.assertEqual((candidate,recipe),(original,[]))
        with self.assertRaises(ValueError): dos_sources.adapt('trap.S',b'unexpected',trap_four=True)
        with self.assertRaises(ValueError): dos_sources.adapt('trap.S',original,trap_four=True,trap_stable=True)

    def test_stable_gradient_overlay_is_optional_and_replayable(self):
        for name in imported.SELECTED:
            original=(imported.VENDOR/'src'/name).read_bytes()
            candidate,recipe=dos_sources.adapt(name,original,trap_stable=True)
            replay=original
            for step in recipe:
                old,new=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(old),step['count']); replay=replay.replace(old,new)
            self.assertEqual(candidate,replay)
            if name!='trap.S': self.assertEqual((candidate,recipe),(original,[]))
        with self.assertRaises(ValueError): dos_sources.adapt('trap.S',b'unexpected',trap_stable=True)
        for option in ('trap_destination','trap_zero_gradient'):
            with self.assertRaises(ValueError): dos_sources.adapt('trap.S',original,trap_stable=True,**{option:True})

    @unittest.skipUnless(imported.COMPILER.is_file(),'DJGPP unavailable')
    def test_stable_gradient_actual_i486_abi_and_footprint(self):
        compiler=imported.COMPILER; nm=compiler.with_name(compiler.name.replace('gcc','nm'))
        objdump=compiler.with_name(compiler.name.replace('gcc','objdump'))
        with tempfile.TemporaryDirectory(prefix='doscraft-stable-coff-') as directory:
            folder=Path(directory); imported.stage_sources(imported.VENDOR,folder/'imported')
            original=(folder/'imported/src/trap.S').read_bytes()
            candidate,_=dos_sources.adapt('trap.S',original,trap_stable=True)
            four,_=dos_sources.adapt('trap.S',original,trap_four=True)
            sections=[]
            for name,data in (('original',original),('candidate',candidate),('four',four)):
                source=folder/(name+'.S');source.write_bytes(data);obj=folder/(name+'.o')
                result=subprocess.run([str(compiler),*imported.ASFLAGS,'-c',str(source),'-o',str(obj)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                binary=obj.read_bytes();info=imported.coff_object(binary)
                sections.append({binary[20+i*40:28+i*40].rstrip(b'\0').decode('ascii'):
                    struct.unpack_from('<I',binary,20+i*40+16)[0] for i in range(info['sections'])})
                symbols=imported.parse_nm(subprocess.check_output([str(nm),'-g',str(obj)],text=True))
                for symbol in imported.TRAP_SYMBOLS: self.assertEqual(symbols['_'+symbol],'T')
                self.assertEqual({n:t for n,t in symbols.items() if t=='U'},{'_g_recip14':'U'})
                asm=subprocess.check_output([str(objdump),'-dr',str(obj)],text=True)
                self.assertNotRegex(asm,r'%(?:[xyz]mm|mm|st)\d?')
                if name=='candidate':
                    self.assertEqual(asm.count('push   %ebp'),10)
                    self.assertEqual(asm.count('pop    %ebp'),10)
                    for symbol in ('_trap_opaque2_stable','_trap_opaque2w_stable'):
                        self.assertEqual(symbols[symbol],'T')
                else:
                    self.assertEqual(asm.count('push   %ebp'),8)
                    self.assertEqual(asm.count('pop    %ebp'),8)
            for info in sections[1:]:
                self.assertEqual(sections[0]['.bss'],info['.bss'])
                self.assertGreater(info['.text'],sections[0]['.text'])
                self.assertLess(info['.text']-sections[0]['.text'],2048)
            print('Original/stable/four-texel COFF sections:',sections)

    def test_destination_overlay_is_optional_exact_and_replayable(self):
        for name in imported.SELECTED:
            original=(imported.VENDOR/'src'/name).read_bytes()
            candidate,recipe=dos_sources.adapt(name,original,trap_destination=True)
            replay=original
            for step in recipe:
                old,new=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(old),step['count'])
                replay=replay.replace(old,new)
            self.assertEqual(candidate,replay)
            if name=='trap.S':
                self.assertNotEqual(candidate,original)
                self.assertEqual(candidate.count(b'rowX'),1,'Only unused BSS slot retained')
                self.assertEqual(candidate.count(b'\timull\t%ecx,%eax'),2)
                self.assertEqual(candidate.count(b'\timull\t%ebx,%eax'),2)
            else: self.assertEqual((candidate,recipe),(original,[]))
        with self.assertRaises(ValueError):
            dos_sources.adapt('trap.S',b'unexpected',trap_destination=True)

    def assembly_parity(self, **options):
        with tempfile.TemporaryDirectory(prefix='doscraft-trap-zero-') as directory:
            folder=Path(directory)
            imported.stage_sources(imported.VENDOR,folder/'imported')
            src=folder/'imported/src'
            original=(src/'trap.S').read_bytes()
            candidate,recipe=dos_sources.adapt('trap.S',original,**options)
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

    @unittest.skipUnless(imported.COMPILER.is_file(),'DJGPP unavailable')
    def test_destination_actual_i486_coff_is_smaller_same_abi_and_scratch(self):
        compiler=imported.COMPILER
        objdump=compiler.with_name(compiler.name.replace('gcc','objdump'))
        nm=compiler.with_name(compiler.name.replace('gcc','nm'))
        with tempfile.TemporaryDirectory(prefix='doscraft-trap-coff-') as directory:
            folder=Path(directory)
            imported.stage_sources(imported.VENDOR,folder/'imported')
            original=(folder/'imported/src/trap.S').read_bytes()
            candidate,_=dos_sources.adapt('trap.S',original,trap_destination=True)
            metadata=[]
            for name,data in (('original',original),('candidate',candidate)):
                source=folder/(name+'.S'); source.write_bytes(data)
                obj=folder/(name+'.o')
                result=subprocess.run([str(compiler),*imported.ASFLAGS,'-c',str(source),'-o',str(obj)],
                    capture_output=True,text=True,timeout=120)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                binary=obj.read_bytes()
                info=imported.coff_object(binary)
                metadata.append({binary[20+i*40:28+i*40].rstrip(b'\0').decode('ascii'):
                    struct.unpack_from('<I',binary,20+i*40+16)[0] for i in range(info['sections'])})
                symbols=imported.parse_nm(subprocess.check_output([str(nm),'-g',str(obj)],text=True))
                self.assertEqual(symbols,{**{'_'+n:'T' for n in imported.TRAP_SYMBOLS},'_g_recip14':'U'})
                asm=subprocess.check_output([str(objdump),'-dr',str(obj)],text=True)
                self.assertEqual(asm.count('push   %ebp'),8)
                self.assertEqual(asm.count('pop    %ebp'),8)
                self.assertEqual(asm.count('imul'),32,'Same gradient and prestep multiplies')
                self.assertNotRegex(asm,r'%(?:[xyz]mm|mm|st)\d?')
            old,new=metadata
            # COFF metadata includes per-section lengths; no new scratch/cache.
            self.assertLess(new['.text'],old['.text'])
            self.assertEqual(new['.bss'],old['.bss'])


if __name__=='__main__': unittest.main()
