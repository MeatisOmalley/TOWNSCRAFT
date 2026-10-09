"""Native startup/error-path mocks, not guest memory or hardware evidence."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported


class StartupTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file(), 'existing native Zig unavailable')
    def test_installed_ram_arena_lifetimes_and_startup_failures(self):
        headers = '''#ifndef MOCK_DPMI
#define MOCK_DPMI
typedef struct { unsigned char cpu; } __dpmi_version_ret;
typedef struct { unsigned long size_of_paging_file_partition_in_pages; } __dpmi_free_mem_info;
typedef union { struct { unsigned short ax, flags; } x;
                struct { unsigned char al, ah; } h; } __dpmi_regs;
int __dpmi_get_version(__dpmi_version_ret *);
int __dpmi_get_free_memory_information(__dpmi_free_mem_info *);
int __dpmi_int(int, __dpmi_regs *);
#endif
'''
        code = '''#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdint.h>
#undef NDEBUG
#include <assert.h>
static void *mock_malloc(size_t);
static void mock_free(void *);
static void mock_exit(int) __attribute__((noreturn));
static int mock_atexit(void (*)(void));
#define malloc mock_malloc
#define free mock_free
#define exit mock_exit
#define atexit mock_atexit
#include "system.c"
#undef malloc
#undef free
#undef exit
#undef atexit
static unsigned char storage[9*1048576+32] __attribute__((aligned(16)));
static int failure, binds, frees, heap_live, irq_live, exit_code;
static int saw_heap_shutdown, saw_irq_shutdown, clock_live;
static jmp_buf exit_target;
static void (*cleanup)(void);
int __dpmi_get_version(__dpmi_version_ret *v) { v->cpu=failure==1?3:4; return failure==2?-1:0; }
int __dpmi_int(int n, __dpmi_regs *r) {
    assert(n==0x15 && r->h.ah==0x88);
    r->x.flags=failure==3?1:0;
    r->x.ax=failure==4?14336:15360;
    return failure==5?-1:0;
}
int __dpmi_get_free_memory_information(__dpmi_free_mem_info *m) {
    m->size_of_paging_file_partition_in_pages=failure==6?1:0;
    return failure==7?-1:0;
}
static void *mock_malloc(size_t n) {
    assert(n==9*1048576+15); return failure==8?0:storage+1;
}
static void mock_free(void *p) {
    if (!p) return;
    assert(p==storage+1 && !irq_live && !heap_live && !clock_live);
    ++frees;
}
static void mock_exit(int n) { exit_code=n; longjmp(exit_target,1); }
static int mock_atexit(void (*fn)(void)) {
    assert(fn==dos_system_shutdown); cleanup=fn; return failure==10?-1:0;
}
int dos_heap_init(void *low, unsigned int ls, void *high, unsigned int hs) {
    assert(((uintptr_t)low&15)==0 && low==storage+16);
    assert(ls==1048576 && hs==8388608 && (unsigned char *)high==(unsigned char *)low+ls);
    ++binds; if(failure==9) return -1; heap_live=1; return 0;
}
void dos_heap_shutdown(void) { assert(!irq_live); heap_live=0; ++saw_heap_shutdown; }
int dos_irq_init(void) { if(failure==11) return -1; irq_live=1; return 0; }
void dos_irq_shutdown(void) { irq_live=0; ++saw_irq_shutdown; }
void dos_video_set_tick_clock(volatile unsigned int *p, unsigned int rate) {
    if(p) { assert(p==&g_ticks && rate==100 && irq_live); clock_live=1; }
    else { assert(!rate); clock_live=0; }
}
void dos_video_restore(void) {}
volatile unsigned int g_ticks;
int main(void) {
    int f;
    sys_init();
    assert(g_ramMB==16 && binds==1 && heap_live && irq_live && clock_live && cleanup);
    cleanup(); assert(frees==1 && !heap_live && !irq_live && !clock_live);
    sys_init(); assert(g_ramMB==16 && binds==2 && heap_live && irq_live);
    dos_system_shutdown(); assert(frees==2 && saw_heap_shutdown==2 && saw_irq_shutdown>=2);
    for(f=1;f<=11;++f) {
        failure=f; cleanup_registered=0; cleanup=0; exit_code=0;
        if(!setjmp(exit_target)) { sys_init(); assert(0); }
        assert(exit_code==1 && !irq_live);
        dos_system_shutdown();
        assert(!heap_live && !backing && !clock_live);
    }
    puts("PASS: startup lifetimes / installed RAM / 11 failures (host mocks)");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='doscraft-system-test-') as folder:
            folder = Path(folder)
            imported.stage_sources(imported.VENDOR, folder / 'imported')
            (folder / 'dpmi.h').write_text(headers)
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
            self.assertIn('PASS: startup lifetimes', result.stdout)
