"""Native transport/lifecycle mocks; separate from guest serial IRQ evidence."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported


class MouseTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file(), 'existing native Zig unavailable')
    def test_motion_buttons_absence_failures_and_lifecycle(self):
        headers = '''typedef struct { unsigned short offset16, segment; } __dpmi_raddr;
typedef union { struct { unsigned short ax,bx,cx,dx; } x; } __dpmi_regs;
int __dpmi_get_real_mode_interrupt_vector(int, __dpmi_raddr *);
int __dpmi_int(int, __dpmi_regs *);
'''
        code = '''#include <stdio.h>
#undef NDEBUG
#include <assert.h>
#include "input.c"
static int failure, calls, resets, motion_calls;
static unsigned int mx, my, buttons;
int __dpmi_get_real_mode_interrupt_vector(int n, __dpmi_raddr *v) {
    assert(n==0x33); v->segment=failure==1?0:0x1234; v->offset16=0;
    return failure==2?-1:0;
}
void dosmemget(unsigned long address, unsigned long size, void *data) {
    assert(address==0x12340 && size==1); *(unsigned char *)data=failure==3?0xCF:0xEB;
}
int __dpmi_int(int n, __dpmi_regs *r) {
    assert(n==0x33 && !r->x.bx && !r->x.cx && !r->x.dx); ++calls;
    if(r->x.ax==0) { ++resets; r->x.ax=failure==4?0:0xFFFF; return failure==5?-1:0; }
    if(r->x.ax==3) { r->x.bx=buttons; r->x.cx=123; r->x.dx=456; return failure==7?-1:0; }
    assert(r->x.ax==0x0B); ++motion_calls;
    r->x.cx=mx; r->x.dx=my; mx=my=0;
    return failure==6?-1:0;
}
int main(void) {
    int dx,dy,f,b,initial_calls,i;
    assert(!dos_input_mouse_present() && !pad_read());
    dx=dy=99; assert(!mouse_read(&dx,&dy) && !dx && !dy && !calls);
    for(f=1;f<=6;++f) {
        failure=f; initial_calls=calls;
        assert(!dos_input_init() && !dos_input_mouse_present());
        if(f<=3) assert(calls==initial_calls);
    }
    failure=0; mx=77; my=88;
    assert(dos_input_init() && dos_input_mouse_present() && !mx && !my);
    for(b=0;b<8;++b) for(i=0;i<65536;++i) {
        int other=65535-i;
        buttons=b; mx=i; my=other;
        assert(mouse_read(&dx,&dy)==(b&3));
        assert(dx==(i>=32768?i-65536:i));
        assert(dy==(other>=32768?other-65536:other));
        assert(mouse_read(&dx,&dy)==(b&3) && !dx && !dy);
    }
    for(f=6;f<=7;++f) {
        assert(dos_input_init()); failure=f; mx=7; my=65531; buttons=3;
        assert(!mouse_read(&dx,&dy) && !dx && !dy && !dos_input_mouse_present());
        initial_calls=calls;
        assert(!mouse_read(&dx,&dy) && calls==initial_calls);
        failure=0;
    }
    assert(dos_input_init()); initial_calls=calls; dos_input_shutdown(); dos_input_shutdown();
    assert(!dos_input_mouse_present() && !mouse_read(&dx,&dy) && calls==initial_calls);
    assert(resets>0 && motion_calls>0 && !pad_read());
    puts("PASS: mouse signed counters / buttons / lifecycle / failures (host mocks)");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='doscraft-mouse-test-') as folder:
            folder = Path(folder)
            imported.stage_sources(imported.VENDOR, folder / 'imported')
            (folder / 'sys').mkdir()
            (folder / 'dpmi.h').write_text(headers)
            (folder / 'sys/movedata.h').write_text('void dosmemget(unsigned long, unsigned long, void *);\n')
            (folder / 'test.c').write_text(code)
            exe = folder / 'test.exe'
            command = [str(ZIG), 'cc', '-target', 'x86-windows-gnu', '-std=gnu99', '-O2',
                       '-Wall', '-Wextra', '-Werror', '-I', str(folder),
                       '-I', str(folder / 'imported/src'), '-I', str(ROOT / 'src/platform/dos'),
                       str(folder / 'test.c'), '-o', str(exe)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('PASS: mouse signed counters', result.stdout)
