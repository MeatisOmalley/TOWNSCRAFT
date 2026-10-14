from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported,dos_sources
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'

def generate(folder):
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    build_imported.stage_sources(build_imported.VENDOR,folder)
    src=folder/'src'
    for f in src.iterdir():
        if f.is_file():
            original=f.read_bytes()
            data,recipe=dos_sources.adapt(f.name,original,trap_four=True,
                very_low_texels=True,raster_finish_skip=True,proven_rows=True,
                low_planar=True,texel_edx=True,row_mul=True,row_pointer=True,
                flat_inline=True,fill_rows=True,viewmodel=True)
            replay=original
            for step in recipe:
                a,b=step['before'].encode(),step['after'].encode()
                assert replay.count(a)==step['count'];replay=replay.replace(a,b)
            assert replay==data;f.write_bytes(data)
    libc=(src/'libc.c').read_text();start=libc.index('u32 hash3(')
    end=libc.index('\n}\n',start)+3;(src/'texture_hash.inc').write_text(libc[start:end])
    tables=src/'tables.c'
    subprocess.run([sys.executable,str(folder/'tools/gentables.py'),str(tables)],check=True)
    exe=folder/'viewmodel.exe'
    subprocess.run(list(map(str,[ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-fno-builtin',
        '-DDOS_VIEWMODEL','-DDOS_VERY_LOW','-DDOS_LOW_PLANAR','-DDOS_TEXEL_EDX','-DDOS_ROW_MUL',
        '-I',src,'-I',ROOT/'src/platform/dos',ROOT/'tests/viewmodel_native.c',
        src/'raster.c',src/'trap.S',src/'textures.c',src/'blocks.c',src/'fmath.c',tables,
        ROOT/'src/platform/dos/viewmodel.c',ROOT/'src/platform/dos/low_planar.S',
        ROOT/'src/platform/dos/graphics_presets.c','-o',exe])),check=True)
    result=subprocess.run([str(exe),str(folder.resolve())],capture_output=True,text=True)
    if result.returncode: raise AssertionError(result.stdout+'\n'+result.stderr)
    return result.stdout

class ViewmodelTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file(),'native Zig required')
    def test_real_viewmodel_raster_and_overlay(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-viewmodel-') as name:
            self.assertIn('PASS: real viewmodel',generate(name))

if __name__=='__main__':unittest.main()
