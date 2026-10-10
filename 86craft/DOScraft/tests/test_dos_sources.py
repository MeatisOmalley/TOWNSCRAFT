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


class DosSourcesTests(unittest.TestCase):
    def test_default_is_immutable_and_changes_are_replayable(self):
        for name in imported.SELECTED:
            data=(imported.VENDOR/'src'/name).read_bytes()
            self.assertEqual(dos_sources.adapt(name,data),(data,[]))
            result,recipe=dos_sources.adapt(name,data,raster_zero_dx=True)
            replay=data
            for step in recipe:
                old,new=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(old),step['count'])
                replay=replay.replace(old,new)
            self.assertEqual(replay,result)
            if name!='raster.c': self.assertFalse(recipe)
        with self.assertRaises(ValueError):
            dos_sources.adapt('raster.c',b'unexpected source',raster_zero_dx=True)

    def test_real_raster_and_assembly_exact_pixel_parity(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-raster-parity-') as folder:
            folder=Path(folder)
            imported.stage_sources(imported.VENDOR,folder/'imported')
            src=folder/'imported/src'
            original=(src/'raster.c').read_bytes()
            candidate,_=dos_sources.adapt('raster.c',original,raster_zero_dx=True)
            (src/'candidate.c').write_bytes(candidate)
            exports=('g_recip14','g_statPixels','g_fastFlat','g_statOcclusionTiles',
                'raster_occlusion_reset','raster_occluded','raster_set_target','raster_width',
                'raster_height','raster_poly','raster_flat_poly','raster_occluder',
                'raster_pixel','raster_line','raster_fill_rows','raster_finish')
            flags=[str(ZIG),'cc','-target','x86-windows-gnu','-O2','-std=gnu99',
                '-fno-builtin','-Wall','-Wextra','-Wno-unused-function','-I',str(src)]
            def run(command):
                result=subprocess.run(command,capture_output=True,text=True,timeout=120)
                self.assertEqual(result.returncode,0,result.stdout+'\n'+result.stderr[-3000:])
                return result
            run([*flags,*(f'-D{n}=reference_{n}' for n in exports),'-c',str(src/'raster.c'),'-o',str(folder/'reference.o')])
            run([*flags,'-c',str(src/'candidate.c'),'-o',str(folder/'candidate.o')])
            run([*flags,'-c',str(src/'trap.S'),'-o',str(folder/'trap.o')])
            exe=folder/'test.exe'
            run([*flags,str(ROOT/'tests/raster_parity_test.c'),str(folder/'reference.o'),
                str(folder/'candidate.o'),str(folder/'trap.o'),'-o',str(exe)])
            self.assertIn('PASS: 24000 polygons',run([str(exe)]).stdout)


if __name__=='__main__': unittest.main()
