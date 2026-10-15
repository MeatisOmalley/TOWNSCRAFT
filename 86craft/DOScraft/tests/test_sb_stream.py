from pathlib import Path
import tempfile
import unittest
from test_irq import mock_headers,run,ZIG
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(ZIG.is_file(),'native Zig unavailable')
class StreamTests(unittest.TestCase):
    def test_continuous_dma_and_failure_cleanup(self):
        with tempfile.TemporaryDirectory(prefix='dos-sb-stream-') as name:
            p=Path(name);mock_headers(p)
            h=p/'dpmi.h';h.write_text(h.read_text()+'\nint __dpmi_allocate_dos_memory(int,int *);\nint __dpmi_free_dos_memory(int);\n')
            (p/'sys').mkdir();(p/'sys/movedata.h').write_text('#include <stddef.h>\nvoid dosmemput(const void *,size_t,unsigned long);\n')
            exe=p/'test.exe'
            run([ZIG,'cc','-target','x86-windows-gnu','-std=gnu99','-O2','-Wall','-Wextra','-Werror',
                 '-I',p,'-I',ROOT/'src/platform/dos',ROOT/'tests/sb_stream_test.c','-o',exe])
            self.assertIn('PASS: continuous SB1',run([exe]))
