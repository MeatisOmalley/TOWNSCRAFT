"""Benchmark input isolation is compile-time only; playable parity tested separately."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported

HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include "keyboard.h"
#include "sys.h"
#include "input.h"
unsigned long dos_keyboard_irq_save(void) { return 0; }
void dos_keyboard_irq_restore(unsigned long v) { (void)v; }
int __dpmi_get_real_mode_interrupt_vector(int n,void *v) { (void)n;(void)v;assert(0);return -1; }
int __dpmi_int(int n,void *v) { (void)n;(void)v;assert(0);return -1; }
void dosmemget(unsigned long a,unsigned long n,void *v) { (void)a;(void)n;(void)v;assert(0); }
int main(void) {
    int i,r,dx=7,dy=8;
    dos_keyboard_reset();
    for(r=0;r<32;++r) for(i=0;i<256;++i) dos_keyboard_scancode((unsigned char)i);
    for(i=0;i<128;++i) assert(!g_keyDown[i]);
    assert(key_get_event()==-1); key_flush(); assert(key_get_event()==-1);
    assert(!mouse_read(&dx,&dy) && !dx && !dy && !pad_read());
    puts("PASS: 8192 live scan codes and mouse ignored by benchmark only");
    return 0;
}
'''


class BenchmarkInputTests(unittest.TestCase):
    def test_native_benchmark_does_not_accept_live_input(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-bench-input-') as folder:
            folder=Path(folder)
            imported.stage_sources(imported.VENDOR,folder/'imported')
            (folder/'sys').mkdir()
            (folder/'dpmi.h').write_text('typedef struct {unsigned short offset16,segment;} __dpmi_raddr;\n'
                'typedef union {struct {unsigned short ax,bx,cx,dx;} x;} __dpmi_regs;\n'
                'int __dpmi_get_real_mode_interrupt_vector(int,__dpmi_raddr *);\n'
                'int __dpmi_int(int,__dpmi_regs *);\n')
            (folder/'sys/movedata.h').write_text('void dosmemget(unsigned long,unsigned long,void *);\n')
            (folder/'test.c').write_text(HARNESS)
            exe=folder/'test.exe'
            command=[str(ZIG),'cc','-target','x86-windows-gnu','-std=gnu99','-O2',
                '-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-unused-function','-DDOSCRAFT_BENCH',
                '-I',str(folder),'-I',str(folder/'imported/src'),'-I',str(ROOT/'src/platform/dos'),
                str(folder/'test.c'),str(ROOT/'src/platform/dos/keyboard.c'),
                str(ROOT/'src/platform/dos/input.c'),'-o',str(exe)]
            for command in (command,[str(exe)]):
                result=subprocess.run(command,capture_output=True,text=True,timeout=120)
                self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('PASS: 8192 live scan codes',result.stdout)


if __name__=='__main__': unittest.main()
