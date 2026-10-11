"""Independent input/VGA oracle; target object audit, no host speedup claim."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'

class FusedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary=tempfile.TemporaryDirectory(prefix='doscraft-fused-')
        cls.addClassCleanup(temporary.cleanup)
        cls.exes=[]
        for arch,portable,transpose in (('x86_64',False,False),('x86_64',True,False),
                ('x86',False,False),('x86_64',False,True),('x86_64',True,True),
                ('x86',False,True),('x86',True,True)):
            exe=Path(temporary.name)/f'{arch}-{portable}-{transpose}.exe'
            args=[str(ZIG),'cc','-target',arch+'-windows-gnu','-O2','-fstrict-aliasing',
                  '-Wall','-Wextra','-Werror',
                  *(['-DDOS_VGA_FUSED_PORTABLE'] if portable else []),
                  *(['-DDOS_VGA_FUSED_TRANSPOSE'] if transpose else []),
                  '-I',str(ROOT/'src/platform/dos'),str(ROOT/'tests/vga_fused_test.c'),
                  str(ROOT/'src/platform/dos/vga_fused.c'),'-o',str(exe)]
            result=subprocess.run(args,capture_output=True,text=True,timeout=120)
            if result.returncode: raise AssertionError(result.stderr)
            cls.exes.append(exe)

    def check(self,mode):
        for exe in self.exes:
            with self.subTest(exe=exe.name):
                result=subprocess.run([str(exe),mode],capture_output=True,text=True,timeout=60)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertIn('PASS:',result.stdout)

    def test_arbitrary_images_pitch_alignment_sharp_hud_and_canaries(self): self.check('matrix')
    def test_individual_sharp_bit_exceptions(self): self.check('bits')
    def test_invalid_arguments_preserve_output(self): self.check('invalid')
    def test_protected_each_source_row_and_output_boundaries(self): self.check('protected')

    def test_actual_i486_object_integer_only_no_external_helpers(self):
        gcc=build_imported.COMPILER
        with tempfile.TemporaryDirectory(prefix='doscraft-fused-coff-') as directory:
            obj=Path(directory)/'fused.o'
            for flags in ([],['-DDOS_VGA_FUSED_TRANSPOSE']):
                result=subprocess.run([str(gcc),*build_imported.CFLAGS,'-fomit-frame-pointer',*flags,
                    '-Werror','-I',str(ROOT/'src/platform/dos'),'-c',
                    str(ROOT/'src/platform/dos/vga_fused.c'),'-o',str(obj)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(build_imported.coff_object(obj.read_bytes())['format'],'coff-i386')
                nm=gcc.with_name(gcc.name.replace('gcc','nm'))
                names=build_imported.parse_nm(subprocess.check_output([str(nm),'-g',str(obj)],text=True))
                self.assertEqual(names.get('_dos_vga_pack_fused'),'T')
                self.assertNotIn('U',names.values())
                dump=gcc.with_name(gcc.name.replace('gcc','objdump'))
                asm=subprocess.check_output([str(dump),'-dr',str(obj)],text=True)
                self.assertNotRegex(asm,r'%(?:[xyz]mm|mm|st)\d?')
                if flags: self.assertRegex(asm,r'xchg\s+%ah,%bl')

if __name__=='__main__': unittest.main()
