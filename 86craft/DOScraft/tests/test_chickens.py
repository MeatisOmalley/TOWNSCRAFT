from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_imported,dos_sources,dos_chickens
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class ChickenTests(unittest.TestCase):
    def test_real_mob_physics_inventory_and_save_paths(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-chickens-') as directory:
            folder=Path(directory);build_imported.stage_sources(build_imported.VENDOR,folder)
            src=folder/'src'
            for name in ('blocks.h','blocks.c','mobs.h','mobs.c','inventory.h','inventory.c'):
                path=src/name;path.write_bytes(dos_sources.adapt(name,path.read_bytes())[0])
            tables=src/'tables.c'
            subprocess.run([sys.executable,str(folder/'tools/gentables.py'),str(tables)],check=True)
            exe=folder/'chickens.exe'
            subprocess.run(list(map(str,[ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99',
                '-fno-builtin','-Wall','-Wextra','-Werror','-I',src,'-I',ROOT/'src/platform/dos',
                ROOT/'tests/chickens_native.c',*(src/name for name in
                    ('blocks.c','inventory.c','physics.c','fmath.c','libc.c')),tables,'-o',exe])),check=True)
            result=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
            self.assertIn('PASS: chicken spawn',result.stdout)

    def test_overlays_replay_exactly(self):
        for name in ('mobs.h','mobs.c','blocks.h','blocks.c','inventory.c','textures.c'):
            original=(build_imported.VENDOR/'src'/name).read_bytes()
            result,recipe=dos_chickens.adapt(name,original);replay=original
            for step in recipe:
                before=step['before'].encode();self.assertEqual(replay.count(before),step['count'])
                replay=replay.replace(before,step['after'].encode())
            self.assertEqual(replay,result,name)


if __name__=='__main__':unittest.main()
