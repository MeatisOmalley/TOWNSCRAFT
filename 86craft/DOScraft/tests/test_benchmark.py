from pathlib import Path
import sys
import unittest
import subprocess
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from benchmark import parse
from game_media import startup


class BenchmarkTests(unittest.TestCase):
    def test_guest_clock_wrap_resolution(self):
        root=Path(__file__).resolve().parents[1]
        zig=root.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
        with tempfile.TemporaryDirectory(prefix='doscraft-bench-clock-') as directory:
            exe=Path(directory)/'clock.exe'
            subprocess.run([str(zig),'cc','-O2','-Wall','-Wextra','-Werror',
                '-I',str(root/'src/platform/dos'),str(root/'tests/bench_clock_test.c'),
                '-o',str(exe)],check=True,capture_output=True,timeout=120)
            subprocess.run([str(exe)],check=True,capture_output=True,timeout=10)
    def fixture(self):
        text='RESULT=PASS\n'
        for n in range(6):
            text+=f'SCENE={n}\nPACK=32000\nUPLOAD=64000\nWAIT=0\n'
            text+='FRAMES='+','.join(['100000']*32)+'\nSECTIONS='+','.join(['32000']*10)+'\n'
        return text

    def test_guest_units_and_sample_count(self):
        result=parse(self.fixture())
        self.assertEqual(result['scenes'][0]['summary_ms']['mean'],100)
        self.assertEqual(result['scenes'][0]['transport_mean_ms']['upload'],2)
        self.assertEqual(result['scenes'][0]['stage_mean_ms']['draw'],1)

    def test_reject_incomplete_and_zero_samples(self):
        for text in ('RESULT=FAIL\n',self.fixture().replace('100000','0')):
            with self.assertRaises(ValueError): parse(text)

    def test_no_keyboard_three_guarded_processes(self):
        script=startup(benchmark=True)
        self.assertIn(b'DOSCRAFT /1\r\nIF ERRORLEVEL 1',script)
        self.assertIn(b'DOSCRAFT /2\r\nIF ERRORLEVEL 1',script)
        self.assertIn(b'DOSCRAFT /3\r\n',script)


if __name__=='__main__': unittest.main()
