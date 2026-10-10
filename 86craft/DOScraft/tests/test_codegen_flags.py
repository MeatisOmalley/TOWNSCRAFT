"""Explicit codegen selection and actual i486 frame-register/ABI audit."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
from build_game import portable_experiment_flags,DEFAULT_ADAPTER_FLAGS


class CodegenTests(unittest.TestCase):
    def test_production_uses_only_accepted_transport_and_codegen(self):
        self.assertEqual(DEFAULT_ADAPTER_FLAGS,('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE',
            '-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_ET4000_CHUNKY'))
        self.assertEqual(portable_experiment_flags(DEFAULT_ADAPTER_FLAGS),('-fomit-frame-pointer',))

    def test_explicit_allowlist_preserves_order_and_integer_target(self):
        self.assertEqual(portable_experiment_flags(('-DDOS_VGA_PACK_ALL',)),())
        self.assertEqual(portable_experiment_flags(('-DDOS_F2B_VERIFY','-fomit-frame-pointer','-O3','-ffast-math')),
                         ('-DDOS_F2B_VERIFY','-fomit-frame-pointer'))
        for flag in ('-march=i486','-mtune=i486','-mno-80387','-mno-mmx','-mno-sse','-mgeneral-regs-only'):
            self.assertIn(flag,imported.CFLAGS)

    @unittest.skipUnless(imported.COMPILER.is_file(),'Existing DJGPP unavailable')
    def test_actual_target_releases_ebp_but_preserves_callee_save_abi(self):
        compiler=imported.COMPILER
        objdump=compiler.with_name(compiler.name.replace('gcc','objdump'))
        nm=compiler.with_name(compiler.name.replace('gcc','nm'))
        with tempfile.TemporaryDirectory(prefix='doscraft-codegen-') as directory:
            for omit in (False,True):
                obj=Path(directory)/('omit.o' if omit else 'reference.o')
                flags=('-fomit-frame-pointer',) if omit else ('-fno-omit-frame-pointer',)
                result=subprocess.run([str(compiler),*imported.CFLAGS,*flags,'-Werror','-c',
                    str(ROOT/'src/platform/dos/vga_pack_all.c'),'-o',str(obj)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(imported.coff_object(obj.read_bytes())['format'],'coff-i386')
                names=imported.parse_nm(subprocess.check_output([str(nm),'-g',str(obj)],text=True))
                self.assertEqual(names.get('_dos_vga_pack_all'),'T')
                self.assertNotIn('U',names.values())
                asm=subprocess.check_output([str(objdump),'-dr',str(obj)],text=True)
                if omit: self.assertNotRegex(asm,r'mov\s+%esp,%ebp')
                else: self.assertRegex(asm,r'mov\s+%esp,%ebp')
                self.assertRegex(asm,r'push\s+%ebp')
                self.assertRegex(asm,r'pop\s+%ebp')
                self.assertNotRegex(asm,r'%(?:[xyz]mm|mm|st)\d?')


if __name__=='__main__': unittest.main()
