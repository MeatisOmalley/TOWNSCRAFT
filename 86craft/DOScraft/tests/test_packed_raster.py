"""Compact raster correctness against real painter assembly, not a speed test."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported as imported
import dos_sources
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class PackedRasterTests(unittest.TestCase):
    def test_overlay_is_explicit_replayable_and_fail_closed(self):
        for name in imported.SELECTED:
            data=(imported.VENDOR/'src'/name).read_bytes()
            result,recipe=dos_sources.adapt(name,data,packed_raster=True)
            replay=data
            for step in recipe:
                before,after=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(before),step['count'])
                replay=replay.replace(before,after)
            self.assertEqual(replay,result)
            self.assertEqual(bool(recipe),name=='render.c')
            self.assertEqual(dos_sources.adapt(name,data),(data,[]))
        with self.assertRaises(ValueError):
            dos_sources.adapt('render.c',b'unexpected renderer',packed_raster=True)

    def test_real_assembly_pixel_parity_both_scales(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-packed-parity-') as folder:
            folder=Path(folder)
            imported.stage_sources(imported.VENDOR,folder/'imported')
            src=folder/'imported/src'
            exports=('g_recip14','g_statPixels','g_fastFlat','g_statOcclusionTiles',
                'raster_occlusion_reset','raster_occluded','raster_set_target','raster_width',
                'raster_height','raster_poly','raster_flat_poly','raster_occluder',
                'raster_pixel','raster_line','raster_fill_rows','raster_finish')
            flags=[str(ZIG),'cc','-target','x86-windows-gnu','-O2','-std=gnu99',
                '-fno-builtin','-Wall','-Wextra','-Wno-unused-function','-I',str(src),
                '-I',str(ROOT/'src/platform/dos')]
            def run(command):
                result=subprocess.run(command,capture_output=True,text=True,timeout=120)
                self.assertEqual(result.returncode,0,result.stdout+'\n'+result.stderr[-3000:])
                return result
            run([*flags,*(f'-D{n}=reference_{n}' for n in exports),'-c',str(src/'raster.c'),'-o',str(folder/'reference.o')])
            run([*flags,'-Draster_finish=__real_raster_finish','-c',str(src/'raster.c'),'-o',str(folder/'candidate.o')])
            run([*flags,'-c',str(src/'trap.S'),'-o',str(folder/'trap.o')])
            exe=folder/'test.exe'
            run([*flags,'-DPACKED_RASTER_TEST',str(ROOT/'tests/raster_parity_test.c'),
                str(ROOT/'src/platform/dos/packed_raster.c'),str(folder/'reference.o'),
                str(folder/'candidate.o'),str(folder/'trap.o'),'-o',str(exe)])
            self.assertIn('PASS: 24000 polygons',run([str(exe)]).stdout)


if __name__=='__main__': unittest.main()
