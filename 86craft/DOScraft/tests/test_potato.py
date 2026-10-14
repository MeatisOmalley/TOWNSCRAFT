from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported,dos_sources
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'

class PotatoTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file(),'native Zig required')
    def test_real_raster_rows_and_transitions(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-potato-') as name:
            folder=Path(name); build_imported.stage_sources(build_imported.VENDOR,folder)
            src=folder/'src'
            for f in src.iterdir():
                if f.is_file():
                    original=f.read_bytes()
                    data,recipe=dos_sources.adapt(f.name,original,trap_four=True,
                        very_low_texels=True,raster_finish_skip=True,proven_rows=True,
                        low_planar=True,texel_edx=True,row_mul=True,row_pointer=True,flat_inline=True,fill_rows=True)
                    replay=original
                    for step in recipe: replay=replay.replace(step['before'].encode(),step['after'].encode())
                    self.assertEqual(replay,data)
                    f.write_bytes(data)
            tables=src/'tables.c'
            self.run_cmd([sys.executable,folder/'tools/gentables.py',tables])
            exe=folder/'potato.exe'
            self.run_cmd([ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-fno-builtin',
                '-DDOS_VERY_LOW','-DDOS_LOW_PLANAR','-DDOS_TEXEL_EDX','-DDOS_ROW_MUL',
                '-I',src,'-I',ROOT/'src/platform/dos',ROOT/'tests/potato_native.c',
                src/'raster.c',src/'trap.S',ROOT/'src/platform/dos/low_planar.S',
                src/'fmath.c',tables,ROOT/'src/platform/dos/graphics_presets.c','-o',exe])
            self.assertIn('PASS: real potato',self.run_cmd([exe]).stdout)

    def run_cmd(self,args):
        r=subprocess.run(list(map(str,args)),capture_output=True,text=True,errors='replace',timeout=120)
        self.assertEqual(r.returncode,0,r.stdout+'\n'+r.stderr[-6000:]); return r

if __name__=='__main__': unittest.main()
