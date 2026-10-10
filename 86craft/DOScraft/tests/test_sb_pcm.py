"""Native SB1 transport faults and DOS code contracts; no audible proof."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
DOS=ROOT/'src/platform/dos'
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
from test_irq import mock_headers, run, ZIG


@unittest.skipUnless(ZIG.is_file(),'existing native Zig unavailable')
class SBTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='doscraft-sb-native-')
        cls.addClassCleanup(cls.temp.cleanup)
        folder=Path(cls.temp.name)
        mock_headers(folder)
        header=folder/'dpmi.h'
        header.write_text(header.read_text()+'\nint __dpmi_allocate_dos_memory(int,int *);\nint __dpmi_free_dos_memory(int);\n')
        (folder/'sys').mkdir()
        (folder/'sys/movedata.h').write_text('#include <stddef.h>\nvoid dosmemput(const void *,size_t,unsigned long);\n')
        cls.exe=folder/'sb-test.exe'
        run([ZIG,'cc','-target','x86-windows-gnu','-std=gnu99','-O2','-Wall','-Wextra','-Werror',
             '-I',folder,'-I',DOS,ROOT/'tests/sb_pcm_test.c','-o',cls.exe])

    def case(self,*arguments):
        self.assertIn('PASS: SB1 transport',run([self.exe,*arguments],20))

    def test_submission_irq_order_busy_bounds_and_reinitialization(self):
        for state in ('0','1'): self.case('lifecycle',state)

    def test_every_paragraph_around_dma_page_boundary(self):
        self.case('boundaries')

    def test_failure_unwind_and_retry_preserve_interrupt_state(self):
        for failure in ('version','get_vector','segment','lock','allocate','address',
                        'reset','dsp_version','command','atexit','install'):
            for state in ('0','1'):
                with self.subTest(failure=failure,state=state): self.case('failure',failure,state)

    def test_each_partial_start_command_stops_dma_before_free(self):
        for byte in ('0','1','2'): self.case('start_failure',byte)

    def test_vector_restore_failure_never_frees_live_irq_memory(self):
        self.case('restore_failure')


@unittest.skipUnless(imported.COMPILER.is_file(),'existing DJGPP unavailable')
class SBDOSCodeTests(unittest.TestCase):
    def test_integer_only_objects_irq_entry_and_link(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-sb-dos-') as name:
            folder=Path(name)
            sources=('sb_pcm.c','sb_entry.S')
            objects=[]
            for source in sources:
                obj=folder/(Path(source).stem+'.o')
                run([imported.COMPILER,*imported.CFLAGS,'-Werror','-I',DOS,'-c',DOS/source,'-o',obj])
                objects.append(obj)
            dump=imported.COMPILER.with_name(imported.COMPILER.name.replace('gcc','objdump'))
            c=run([dump,'-dr','--no-show-raw-insn',objects[0]])
            # Interrupt body only does bounded port I/O/counter updates.
            body=c.split('<_dos_sb_irq_body>:',1)[1].split('\n\n',1)[0]
            self.assertNotIn('call',body)
            self.assertNotIn('cli',c)
            self.assertNotIn('sti',c)
            self.assertNotRegex(c,r'\b(?:fld|fst|fadd|fmul|fdiv)\w*\b')
            asm=run([dump,'-dr','--no-show-raw-insn',objects[1]])
            for token in ('pusha','cld','popa','sti','iret','_dos_sb_irq_body'):
                self.assertIn(token,asm)
            harness=folder/'link.c'
            harness.write_text('#include "sb_pcm.h"\nunsigned long dos_keyboard_irq_save(void){return 0;}\n'
                'void dos_keyboard_irq_restore(unsigned long s){(void)s;}\n'
                'int main(void){return dos_sb_version()!=0 || dos_sb_busy();}\n')
            exe=folder/'SBTEST.EXE'
            run([imported.COMPILER,*imported.CFLAGS,'-I',DOS,harness,*objects,'-o',exe])
            self.assertEqual(exe.read_bytes()[:2],b'MZ')


if __name__=='__main__': unittest.main()
