"""Test the actual timeout helpers without VGA hardware or DOS interrupts."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class VideoClockTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file(), 'existing Zig unavailable')
    def test_bios_fallback_irq_deadline_and_32bit_wrap(self):
        source = (ROOT / 'src/platform/dos/video.c').read_text()
        helpers = source[source.index('static volatile unsigned int *irq_clock;'):
                         source.index('#define VGA_PAGES')]
        harness = '''#undef NDEBUG
#include <assert.h>
typedef long long uclock_t;
#define UCLOCKS_PER_SEC 1193180
static uclock_t mock_clock;
static uclock_t uclock(void) { return mock_clock; }
''' + helpers + '''
int main(void) {
    volatile unsigned int ticks = 0xfffffffbu;
    uclock_t start;
    mock_clock = 500;
    assert(wait_clock() == 500);
    start = wait_clock();
    mock_clock += UCLOCKS_PER_SEC / 10 - 1;
    assert(wait_pending(start));
    ++mock_clock;
    assert(!wait_pending(start));
    dos_video_set_tick_clock(&ticks, 100);
    start = wait_clock();
    ticks += 9;
    assert(wait_pending(start));
    ++ticks;
    assert(!wait_pending(start));
    dos_video_set_tick_clock(0, 0);
    assert(wait_clock() == mock_clock);
    dos_video_set_tick_clock(&ticks, 0);
    assert(wait_clock() == mock_clock);
    dos_video_set_tick_clock(&ticks, 1);
    assert(wait_clock() == mock_clock);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='doscraft-video-clock-') as folder:
            folder = Path(folder)
            c = folder / 'test.c'
            exe = folder / 'test.exe'
            c.write_text(harness)
            command = [str(ZIG), 'cc', '-target', 'x86-windows-gnu', '-O2',
                       '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
