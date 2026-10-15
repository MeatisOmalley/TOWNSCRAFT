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
int __dpmi_allocate_dos_memory(int, int *);
int __dpmi_free_dos_memory(int);
typedef union { struct { unsigned short ax, flags; } x;
                struct { unsigned char al, ah; } h; } __dpmi_regs;
int __dpmi_get_version(__dpmi_version_ret *);
int __dpmi_get_free_memory_information(__dpmi_free_mem_info *);
int __dpmi_int(int, __dpmi_regs *);
#endif
'''
        nearptr = '''int __djgpp_nearptr_enable(void);
extern int __djgpp_conventional_base;
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
/* DOS memory below 640 KiB: 600 KiB free, of which 8 KiB stays with DOS. */
#define DOS_SEGMENT 0x1000
#define DOS_LARGEST (600*1024/16)
#define DOS_LOW ((DOS_LARGEST-512)*16)
static unsigned char dos_memory[DOS_LOW] __attribute__((aligned(16)));
static int failure, conventional, binds, frees, heap_live, irq_live, exit_code;
static int saw_heap_shutdown, saw_irq_shutdown, clock_live, dos_live;
int __djgpp_conventional_base;
int __djgpp_nearptr_enable(void) { return conventional; }
int __dpmi_allocate_dos_memory(int paragraphs, int *selector) {
    assert(conventional && !dos_live);
    if (paragraphs==0xFFFF) { *selector=DOS_LARGEST; return -1; }
    assert(paragraphs==DOS_LARGEST-512);
    __djgpp_conventional_base=(int)(uintptr_t)dos_memory-DOS_SEGMENT*16;
    *selector=77; dos_live=1; return DOS_SEGMENT;
}
int __dpmi_free_dos_memory(int selector) {
    assert(selector==77 && dos_live && !irq_live && !heap_live); dos_live=0; return 0;
}
static jmp_buf exit_target;
static void (*cleanup)(void);
int __dpmi_get_version(__dpmi_version_ret *v) { v->cpu=failure==1?2:conventional?3:4; return failure==2?-1:0; }
int __dpmi_int(int n, __dpmi_regs *r) {
    assert(n==0x15 && r->h.ah==0x88);
    r->x.flags=failure==3?1:0;
    r->x.ax=failure==4?512:15360; /* 1.5 MiB installed: below the 2 MiB minimum */
    return failure==5?-1:0;
}
int __dpmi_get_free_memory_information(__dpmi_free_mem_info *m) {
    m->size_of_paging_file_partition_in_pages=failure==6?1:0;
    return failure==7?-1:0;
}
static void *mock_malloc(size_t n) {
    assert(n==(conventional ? 9*1048576-DOS_LOW : 9*1048576)+15); return failure==8?0:storage+1;
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
    if (conventional) {
        /* The low arena's remainder moves into the high arena. */
        assert(low==dos_memory && ls==DOS_LOW && high==storage+16 && hs==9*1048576-DOS_LOW);
    } else {
        assert(((uintptr_t)low&15)==0 && low==storage+16);
        assert(ls==1048576 && hs==8388608 && (unsigned char *)high==(unsigned char *)low+ls);
    }
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
int dos_input_init(void) { assert(irq_live && clock_live); return 0; }
void dos_input_shutdown(void) {}
volatile unsigned int g_ticks;
int main(void) {
    int f;
    sys_init();
    assert(g_ramMB==16 && binds==1 && heap_live && irq_live && clock_live && cleanup);
    cleanup(); assert(frees==1 && !heap_live && !irq_live && !clock_live);
    sys_init(); assert(g_ramMB==16 && binds==2 && heap_live && irq_live && !dos_cpu386);
    dos_system_shutdown(); assert(frees==2 && saw_heap_shutdown==2 && saw_irq_shutdown>=2);
    conventional=1;
    sys_init(); assert(binds==3 && heap_live && dos_live && dos_cpu386);
    dos_system_shutdown(); assert(frees==3 && !dos_live);
    conventional=0;
    for(f=1;f<=11;++f) {
        failure=f; cleanup_registered=0; cleanup=0; exit_code=0;
        if(!setjmp(exit_target)) { sys_init(); assert(0); }
        assert(exit_code==1 && !irq_live);
        dos_system_shutdown();
        assert(!heap_live && !backing && !clock_live);
    }
    puts("PASS: startup lifetimes / installed RAM / conventional low arena / 11 failures (host mocks)");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='doscraft-system-test-') as folder:
            folder = Path(folder)
            imported.stage_sources(imported.VENDOR, folder / 'imported')
            (folder / 'dpmi.h').write_text(headers)
            (folder / 'sys').mkdir()
            (folder / 'sys/nearptr.h').write_text(nearptr)
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
