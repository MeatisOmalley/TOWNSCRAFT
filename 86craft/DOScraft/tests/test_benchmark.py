from pathlib import Path
import sys
import unittest
import subprocess
import tempfile
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import benchmark
from benchmark import parse
from game_media import startup


class BenchmarkTests(unittest.TestCase):
    def test_new_probes_remain_independent_of_current_release(self):
        variants={
            'fused-low':('-DDOS_VGA_FUSED',),
            'fused-words':('-DDOS_VGA_FUSED','-DDOS_VGA_FUSED_TRANSPOSE'),
            'group-facing':('-DDOS_GROUP_FACING',),
        }
        for candidate,extra in variants.items():
            with self.subTest(candidate=candidate), \
                 patch.object(sys,'argv',['benchmark.py','--candidate',candidate,
                     '--quality','low','--bios','seed/isa486.nvr','--prepare-only']), \
                 patch.object(benchmark,'build') as build, \
                 patch.object(benchmark,'prepare',return_value=(Path('private/vm'),{'exe_sha256':'fixture'})), \
                 patch.object(benchmark,'validate',return_value=[]), \
                 patch.object(Path,'read_text',return_value='{"exe_sha256":"fixture"}'), \
                 patch.object(benchmark.subprocess,'Popen') as launch:
                benchmark.main()
                self.assertEqual(build.call_args.kwargs,{'benchmark':True,
                    'extra_adapter_flags':(*benchmark.DEFAULT_ADAPTER_FLAGS,*extra,
                        '-DDOS_BENCH_QUALITY=0')})
                launch.assert_not_called()
        for flag in ('-DDOS_VGA_FUSED','-DDOS_VGA_FUSED_TRANSPOSE','-DDOS_GROUP_FACING'):
            self.assertNotIn(flag,benchmark.DEFAULT_ADAPTER_FLAGS)

    def test_production_recipe_uses_current_defaults_without_changing_replay(self):
        for index,quality in enumerate(('low','medium','high')):
            with self.subTest(quality=quality), \
                 patch.object(sys,'argv',['benchmark.py','--candidate','production',
                     '--quality',quality,'--bios','seed/isa486.nvr','--prepare-only']), \
                 patch.object(benchmark,'build') as build, \
                 patch.object(benchmark,'prepare',return_value=(Path('private/vm'),{'exe_sha256':'fixture'})), \
                 patch.object(benchmark,'validate',return_value=[]), \
                 patch.object(Path,'read_text',return_value='{"exe_sha256":"fixture"}'), \
                 patch.object(benchmark.subprocess,'Popen') as launch:
                benchmark.main()
                self.assertEqual(build.call_args.kwargs,{'benchmark':True,
                    'extra_adapter_flags':(*benchmark.DEFAULT_ADAPTER_FLAGS,
                        '-DDOS_BENCH_QUALITY='+str(index))})
                launch.assert_not_called()

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
        self.assertEqual(result['scenes'][0]['fps']['average_fps'],10)
        self.assertFalse(result['scenes'][0]['fps']['0_1_percent_minimum_sample_count_met'])

    def test_long_run_units_percentiles_and_sample_validation(self):
        text='SAMPLES_PER_SCENE=1000\n'+self.fixture().replace(
            ','.join(['100000']*32),','.join(['100000']*999+['500000']))
        result=parse(text)['scenes'][0]
        self.assertEqual(result['summary_ms']['p99_9'],100)
        self.assertEqual(result['summary_ms']['max'],500)
        self.assertEqual(result['fps']['0_1_percent_low_fps'],2)
        self.assertAlmostEqual(result['fps']['1_percent_low_fps'],10*1e6/1400000)
        self.assertEqual(result['transport_mean_ms']['upload'],.064)
        self.assertEqual(result['stage_mean_ms']['draw'],.032)
        with self.assertRaises(ValueError): parse(text.replace('SAMPLES_PER_SCENE=1000','SAMPLES_PER_SCENE=999'))
        with self.assertRaises(ValueError): parse(text.replace('SAMPLES_PER_SCENE=1000','SAMPLES_PER_SCENE=10001'))

    def test_reject_incomplete_and_zero_samples(self):
        for text in ('RESULT=FAIL\n',self.fixture().replace('100000','0')):
            with self.assertRaises(ValueError): parse(text)

    def test_separate_edit_call_and_frame_latency(self):
        text='VERSION=2\n'+self.fixture().replace('SCENE=5','SCENE=block_edits')
        for n in range(6):
            name='block_edits' if n==5 else str(n)
            calls=','.join(['2000' if n==5 else '0']*32)
            text=text.replace('SCENE='+name+'\n','SCENE='+name+'\nEDIT_CALLS='+calls+'\n')
        result=parse(text)
        self.assertEqual(result['scenes'][5]['edit_call_mean_ms'],2)
        self.assertEqual(result['scenes'][5]['edit_cycle_summary_ms']['mean'],102)
        with self.assertRaises(ValueError): parse(text.replace('EDIT_CALLS=0','EDIT_CALLS=2000',1))
        with self.assertRaises(ValueError): parse('VERSION=2\n'+self.fixture())

    def test_no_keyboard_three_guarded_processes(self):
        script=startup(benchmark=True)
        self.assertIn(b'DOSCRAFT /1\r\nIF ERRORLEVEL 1',script)
        self.assertIn(b'DOSCRAFT /2\r\nIF ERRORLEVEL 1',script)
        self.assertIn(b'DOSCRAFT /3\r\n',script)


if __name__=='__main__': unittest.main()
