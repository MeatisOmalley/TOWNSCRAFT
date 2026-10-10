"""Actual pinned world/cache/mobs/inventory with real file-backed DOS adapters.

Native x86 Win32 substitutes only DOS commit and routing of DOS absolute paths.
Two executable runs prove reload cannot rely on surviving process RAM.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_adapters
from terrain_fixture import terrain_image
from save_fixture import save_image
from world_storage_reference import worldio_expected_image
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(), 'native Win32 Zig unavailable')
class WorldStorageTests(unittest.TestCase):
    def test_real_eviction_edits_light_mobs_save_and_cold_reload(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-world-storage-') as name:
            folder=Path(name)
            record=build_adapters.stage(folder)
            for source in ('world.c','mobs.c','inventory.c','column_store.inc','column_hdd.inc','column_cache.inc'):
                row=next(r for r in record['files'] if r['source']=='src/'+source)
                self.assertFalse(row['transformations'])
                self.assertEqual(row['source_sha256'],row['staged_sha256'])
            (folder/'dpmi.h').write_text('typedef struct { struct { unsigned short ax,bx,flags; } x; } __dpmi_regs;\nint __dpmi_int(int, __dpmi_regs *);\n')
            tables=folder/'src/tables.c'
            self.command([sys.executable,folder/'tools/gentables.py',tables])
            flags=[ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99','-fno-builtin',
                   '-Wall','-Wextra','-Werror=implicit-function-declaration','-I',folder,
                   '-I',folder/'src','-I',ROOT/'src/platform/dos']
            objects=[]
            for source in ('save','hdd'):
                obj=folder/(source+'.o'); objects.append(obj)
                self.command([*flags,'-Dopen=world_test_open','-c',ROOT/f'src/platform/dos/{source}.c','-o',obj])
            exe=folder/'world-test.exe'
            self.command([*flags,ROOT/'tests/world_storage_native.c',ROOT/'src/platform/dos/heap.c',
                          *(folder/'src'/s for s in ('inventory.c','blocks.c','fmath.c','libc.c')),
                          tables,*objects,'-o',exe])
            terrain=folder/'terrain.tmp'; save=folder/'world.sav'
            terrain.write_bytes(terrain_image()); save.write_bytes(save_image())
            self.assertIn('PASS: real world storage write',self.command([exe,'write',terrain,save]).stdout)
            saved=save.read_bytes()
            self.assertEqual(saved,worldio_expected_image(),'complete v3 snapshot and untouched trailing tracks')
            self.assertIn('PASS: real world storage reload',self.command([exe,'reload',terrain,save]).stdout)
            self.assertEqual(save.read_bytes(),saved,'reload must not write the durable save medium')
            self.assertEqual(terrain.read_bytes()[:512],terrain_image()[:512])
            terrain_before_refusal=terrain.read_bytes()
            refused=subprocess.run([str(exe),'write',str(terrain),str(save)],capture_output=True,text=True,timeout=20)
            self.assertNotEqual(refused.returncode,0)
            self.assertIn('refuses nonblank',refused.stderr)
            self.assertEqual(save.read_bytes(),saved)
            self.assertEqual(terrain.read_bytes(),terrain_before_refusal)

    def command(self,args):
        result=subprocess.run(list(map(str,args)),capture_output=True,text=True,errors='replace',timeout=120)
        self.assertEqual(result.returncode,0,result.stdout+'\n'+result.stderr)
        return result


if __name__=='__main__': unittest.main()
