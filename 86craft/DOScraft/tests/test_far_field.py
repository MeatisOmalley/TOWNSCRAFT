from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported,dos_sources
from bake_far_field import surface
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
TERRAIN=ROOT/'build/checks/large-world-proto/terrain1024-far.bin'

class FarTests(unittest.TestCase):
    def test_baker_ignores_models_and_keeps_highest_cube(self):
        raw=bytearray(12288)
        raw[0]=11; raw[47]=23; raw[48+19]=2
        self.assertEqual(surface(raw)[:2],bytes((20,2)))
        raw[48+46]=7
        self.assertEqual(surface(raw)[:2],bytes((47,7)))

    def test_overlay_replays_with_release_and_planar(self):
        for name in ('world.c','render.c','raster.c','column_cache.inc','column_hdd.inc'):
            original=(build_imported.VENDOR/'src'/name).read_bytes()
            data,recipe=dos_sources.adapt(name,original,large_world=True,stream_light=True,
                collect_margin=True,raster_finish_skip=True,proven_rows=True,
                very_low_texels=True,low_planar=True,far_field=True)
            replay=original
            for step in recipe:
                before,after=step['before'].encode(),step['after'].encode()
                self.assertEqual(replay.count(before),step['count'])
                replay=replay.replace(before,after)
            self.assertEqual(replay,data)

    @unittest.skipUnless(ZIG.is_file() and TERRAIN.is_file(),'native Zig and distant seed required')
    def test_actual_world_edit_reload_and_checksum(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-far-') as name:
            folder=Path(name); build_imported.stage_sources(build_imported.VENDOR,folder)
            src=folder/'src'
            for f in src.iterdir():
                if f.is_file():
                    data,_=dos_sources.adapt(f.name,f.read_bytes(),large_world=True,stream_light=True,far_field=True)
                    f.write_bytes(data)
            (folder/'dpmi.h').write_text('typedef struct { struct { unsigned short ax,bx,flags; } x; } __dpmi_regs;\nint __dpmi_int(int, __dpmi_regs *);\n')
            tables=src/'tables.c'; self.run_cmd([sys.executable,folder/'tools/gentables.py',tables])
            flags=[ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-fno-builtin',
                '-DDOS_LARGE_WORLD','-DDOS_STREAM_LIGHT_CACHE','-DDOS_FAR_FIELD',
                '-Werror=implicit-function-declaration','-I',folder,'-I',src,'-I',ROOT/'src/platform/dos']
            obj=folder/'hdd.o'; self.run_cmd([*flags,'-Dopen=world_test_open','-c',ROOT/'src/platform/dos/hdd.c','-o',obj])
            exe=folder/'far.exe'
            self.run_cmd([*flags,ROOT/'tests/far_world_native.c',ROOT/'src/platform/dos/heap.c',
                *(src/s for s in ('blocks.c','fmath.c','libc.c')),tables,obj,'-o',exe])
            terrain=folder/'terrain.tmp'; shutil.copyfile(TERRAIN,terrain)
            self.assertIn('PASS: distant map',self.run_cmd([exe,terrain]).stdout)
            with terrain.open('r+b') as f:
                f.seek(178291200); value=f.read(1); f.seek(178291200); f.write(bytes([value[0]^1]))
            r=subprocess.run([str(exe),str(terrain)],capture_output=True,text=True,timeout=120)
            self.assertNotEqual(r.returncode,0); self.assertIn('Corrupt distant terrain',r.stderr)

    @unittest.skipUnless(ZIG.is_file(),'native Zig required')
    def test_renderer_gates_and_real_strip_layouts(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-far-render-') as name:
            folder=Path(name); build_imported.stage_sources(build_imported.VENDOR,folder)
            src=folder/'src'; tables=src/'tables.c'
            self.run_cmd([sys.executable,folder/'tools/gentables.py',tables])
            data,_=dos_sources.adapt('raster.c',(src/'raster.c').read_bytes(),large_world=True,
                raster_finish_skip=True,proven_rows=True,very_low_texels=True,low_planar=True,far_field=True)
            text=data.decode(); begin=text.index('void raster_far_strip('); end=text.index('void raster_pixel(',begin)
            (folder/'far_raster_test.inc').write_text(text[begin:end])
            exe=folder/'render.exe'
            self.run_cmd([ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99',
                '-I',folder,'-I',src,'-I',ROOT/'src/platform/dos',ROOT/'tests/far_render_native.c',
                src/'blocks.c',src/'fmath.c',tables,'-o',exe])
            self.assertIn('PASS: distant levels',self.run_cmd([exe]).stdout)

    def run_cmd(self,args):
        r=subprocess.run(list(map(str,args)),capture_output=True,text=True,errors='replace',timeout=120)
        self.assertEqual(r.returncode,0,r.stdout+'\n'+r.stderr[-6000:]); return r

if __name__=='__main__': unittest.main()
