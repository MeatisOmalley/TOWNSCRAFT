"""Compare the checked bake with the actual runtime and exercise cold edits."""
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported
import dos_sources
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
TERRAIN=ROOT/'build/checks/large-world-proto/terrain1024-lit.bin'

@unittest.skipUnless(sys.platform=='win32' and ZIG.is_file() and TERRAIN.is_file(),'native Zig and lit seed required')
class StreamLightTests(unittest.TestCase):
    def test_canonical_bake_and_cold_edit_invalidation(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-stream-light-') as name:
            folder=Path(name); build_imported.stage_sources(build_imported.VENDOR,folder)
            src=folder/'src'
            for file in src.iterdir():
                if file.is_file():
                    data,_=dos_sources.adapt(file.name,file.read_bytes(),large_world=True,stream_light=True)
                    file.write_bytes(data)
            (folder/'dpmi.h').write_text('typedef struct { struct { unsigned short ax,bx,flags; } x; } __dpmi_regs;\nint __dpmi_int(int, __dpmi_regs *);\n')
            tables=src/'tables.c'
            self.run_command([sys.executable,folder/'tools/gentables.py',tables])
            flags=[ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-fno-builtin',
                '-DDOS_LARGE_WORLD','-DDOS_STREAM_LIGHT_CACHE','-Werror=implicit-function-declaration',
                '-I',folder,'-I',src,'-I',ROOT/'src/platform/dos']
            obj=folder/'hdd.o'
            self.run_command([*flags,'-Dopen=world_test_open','-c',ROOT/'src/platform/dos/hdd.c','-o',obj])
            exe=folder/'light-test.exe'
            self.run_command([*flags,ROOT/'tests/stream_light_native.c',ROOT/'src/platform/dos/heap.c',
                *(src/s for s in ('blocks.c','fmath.c','libc.c')),tables,obj,'-o',exe])
            terrain=folder/'terrain.tmp'; shutil.copyfile(TERRAIN,terrain)
            self.assertIn('PASS: canonical baked light',self.run_command([exe,terrain]).stdout)
            header=TERRAIN.read_bytes()[:512]
            sx,sz=struct.unpack_from('<2I',header,40)
            for offset,message in [((294977-32)*512,'Corrupt prebaked lighting index'),
                ((294977+(sz//16*64+sx//16)*13)*512,'Cannot read 1024-world spawn terrain')]:
                with terrain.open('r+b') as file:
                    file.seek(offset); original=file.read(1); file.seek(offset); file.write(bytes([original[0]^1]))
                result=subprocess.run([str(exe),str(terrain)],capture_output=True,text=True,timeout=120)
                self.assertNotEqual(result.returncode,0)
                self.assertIn(message,result.stderr)
                with terrain.open('r+b') as file:
                    file.seek(offset); file.write(original)
    def run_command(self,args):
        result=subprocess.run(list(map(str,args)),capture_output=True,text=True,errors='replace',timeout=120)
        self.assertEqual(result.returncode,0,result.stdout+'\n'+result.stderr[-6000:]); return result

if __name__=='__main__': unittest.main()
