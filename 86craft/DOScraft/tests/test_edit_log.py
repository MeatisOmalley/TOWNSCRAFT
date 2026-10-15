"""V3 edit log: the actual world cache, save codec and DOS adapters on private media.

Each scenario step is a separate native process (tests/edit_log_native.c), so
a reload never relies on RAM from the run that wrote the media. The medium is
synthetic but has the real layout and realistic record sizes: 64 shared base
columns of 7 sectors each (the baked seed averages 3,287 bytes).
"""
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_adapters
import dos_sources
from bake_large_world import checksum
from convert_large_v2 import V2_MAGIC, encode, check_v2, decode
from fat_image import FatImage, scratch_hdd
import convert_large_v3 as v3
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
AIR, STONE, GRASS, DIRT, BEDROCK, COAL = 0, 1, 2, 3, 11, 12
SPAWN = (520, 520)


# Sectors per base record for the 64 variants, following the baked seed's
# histogram (5:455 6:947 7:1552 8:802 9:296 10:42 11:2 of 4096 columns).
SIZES = [5] * 7 + [6] * 15 + [7] * 24 + [8] * 12 + [9] * 4 + [10, 11]


def column(variant):
    """256 cells of bedrock, stone with ores, grass and air, sized to fill
    SIZES[variant] sectors less 64 bytes, so one edit keeps the sector count."""
    ores = ((512 * SIZES[variant] - 64) // 2 - 4 * 256) // 2
    out = bytearray()
    for cell in range(256):
        lx, lz = cell & 15, cell >> 4
        h = 20 + (lx * 3 + lz * 5 + variant * 7) % 13
        runs, y = [(1, BEDROCK)], 1
        for k in range(ores // 256 + (cell < ores % 256)):
            at = 2 + 3 * k + (lx + lz) % 2
            runs += [(at - y, STONE), (1, COAL)]
            y = at + 1
        runs += [(h - 1 - y, STONE), (1, GRASS), (48 - h, AIR)]
        for n, b in runs:
            out += bytes((n, b))
    return bytes(out)


def synthetic_v2(pool_pairs=384):
    records = [column(v) for v in range(64)]
    light = encode(bytes(6400))
    base_sector = 193
    light_sector = base_sector + sum(v3.sectors(len(r)) for r in records)
    tile_sector = light_sector + v3.sectors(len(light))
    pool = tile_sector + 1024
    image = bytearray((pool + pool_pairs * 48) * 512)
    offsets, at = [], base_sector
    for r in records:
        image[at * 512:at * 512 + len(r)] = r
        offsets.append(at * 512)
        at += v3.sectors(len(r))
    image[light_sector * 512:light_sector * 512 + len(light)] = light
    sums = [checksum(r) for r in records]
    for c in range(4096):
        variant = (c * 37 + c // 64) % 64
        struct.pack_into('<3I', image, 512 + c * 12, offsets[variant], len(records[variant]), sums[variant])
        struct.pack_into('<3I', image, 97 * 512 + c * 12, light_sector * 512, len(light), checksum(light))
    image[:16] = V2_MAGIC
    struct.pack_into('<9I', image, 16, 2, 1024, 4096, 24, 33, 4242, SPAWN[0], SPAWN[1],
                     checksum(image[512:512 + 4096 * 12]))
    struct.pack_into('<6I', image, 120, base_sector, 1, 97, pool, pool_pairs, pool + pool_pairs * 48)
    check_v2(image)
    return bytes(image)


def flip(path, offset):
    with path.open('r+b') as f:
        f.seek(offset)
        b = f.read(1)
        f.seek(offset)
        f.write(bytes([b[0] ^ 0x5a]))


def reseal(save, start):
    length = struct.unpack_from('<I', save, start + 12)[0]
    struct.pack_into('<I', save, start + 8, v3.save_sum(save[start + 20:start + 20 + length]))


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(), 'native Win32 Zig unavailable')
class EditLogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='doscraft-edit-log-')
        folder = Path(cls.tmp.name)
        build_adapters.stage(folder / 'stage', large_world=True)
        src = folder / 'stage/src'
        for f in src.iterdir():
            if f.is_file() and f.suffix in ('.c', '.h', '.inc') and f.name != 'towns_save_codec.inc':
                data, _ = dos_sources.adapt(f.name, f.read_bytes(), large_world=True)
                f.write_bytes(data)
        (folder / 'stage/dpmi.h').write_text('typedef struct { struct { unsigned short ax,bx,flags; } x; } __dpmi_regs;\n'
                                             'int __dpmi_int(int, __dpmi_regs *);\n')
        tables = src / 'tables.c'
        cls.run_cmd([sys.executable, folder / 'stage/tools/gentables.py', tables])
        flags = [ZIG, 'cc', '-target', 'x86-windows-gnu', '-O2', '-std=gnu99', '-fno-builtin',
                 '-DDOS_LARGE_WORLD', '-Wall', '-Werror=implicit-function-declaration',
                 '-I', folder / 'stage', '-I', src, '-I', ROOT / 'src/platform/dos']
        objects = []
        for name in ('save', 'hdd'):
            obj = folder / (name + '.o')
            objects.append(obj)
            cls.run_cmd([*flags, '-Dopen=world_test_open', '-c', ROOT / f'src/platform/dos/{name}.c', '-o', obj])
        cls.exe = folder / 'edit-log.exe'
        cls.run_cmd([*flags, ROOT / 'tests/edit_log_native.c', ROOT / 'src/platform/dos/heap.c',
                     *(src / s for s in ('inventory.c', 'blocks.c', 'fmath.c', 'libc.c')),
                     tables, *objects, '-o', cls.exe])
        cls.v2 = synthetic_v2()
        cls.case = 0

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @staticmethod
    def run_cmd(args, ok=True):
        result = subprocess.run(list(map(str, args)), capture_output=True, text=True, errors='replace',
                                timeout=900)
        if ok and result.returncode:
            raise AssertionError(f'{args[:2]} failed:\n{result.stdout[-3000:]}\n{result.stderr[-3000:]}')
        return result

    def media(self, log_sectors=32768, v2=False):
        type(self).case += 1
        terrain = Path(self.tmp.name) / f'terrain{self.case}.tmp'
        save = Path(self.tmp.name) / f'world{self.case}.sav'
        terrain.write_bytes(self.v2 if v2 else v3.from_v2(self.v2, log_sectors, checked=True))
        save.write_bytes(bytes(2 * v3.BANK))
        self.game('new', terrain, save)
        return terrain, save

    def game(self, mode, terrain, save, *args, ok=True):
        result = self.run_cmd([self.exe, mode, terrain, save, *args], ok=False)
        if ok:
            self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-3000:])
            self.assertIn('PASS', result.stdout)
        return result

    @staticmethod
    def stat(output, name):
        return int(re.findall(rf'\b{name}=(\d+)', output)[-1])

    def banks(self, terrain, save):
        return v3.v3_records(terrain.read_bytes(), save.read_bytes(), checked=True)

    def every_column(self, log_sectors):
        terrain, save = self.media(log_sectors)
        out = self.game('edit', terrain, save, 0, 4096, 1, 512).stdout
        self.assertEqual(self.stat(out, 'live_records'), 4096)
        self.assertEqual(self.stat(out, 'refused'), 0)
        print('\nall columns:', re.findall(r'LOG .*', out)[-1],
              max(map(int, re.findall(r'SAVE ms=(\d+)', out))), 'ms max save')
        self.game('check', terrain, save, 0, 4096, 1)
        newest, older = self.banks(terrain, save)
        self.assertEqual(len(newest[1]), 4096)
        self.assertEqual(len(older[1]), 3584)

    def test_every_column_edits_saves_and_reloads_486(self):
        self.every_column(v3.DEFAULT_LOG_BYTES // 512)

    def test_every_column_edits_saves_and_reloads_386(self):
        disk = scratch_hdd(cylinders=977, heads=5, sectors=17)
        volume = FatImage(disk, struct.unpack_from('<I', disk, 454)[0] * 512)
        # The baked seed's immutable prefix: 47,652 sectors before the log.
        self.every_column(v3.log_size_for_volume(volume, 47652, [2 * v3.BANK]))

    def test_wraparound_reuses_released_records(self):
        terrain, save = self.media(log_sectors=8192)
        out = self.game('edit', terrain, save, 0, 64, 0, 64, 30).stdout
        self.assertGreater(self.stat(out, 'wraps'), 0, 'the head wraps and reuses released records')
        self.assertEqual((self.stat(out, 'refused'), self.stat(out, 'compactions')), (0, 0))
        print('\nwrap:', re.findall(r'STATS .*', out)[-1])
        self.game('check', terrain, save, 0, 64, 29 % 6)
        self.banks(terrain, save)

    def test_compaction_when_nearly_full(self):
        terrain, save = self.media(log_sectors=2048)
        self.game('edit', terrain, save, 0, 64, 0, 64, 4)
        # Few 24-sector runs remain, so saves copy a window's live records.
        out = self.game('edit', terrain, save, 64, 140, 2, 20).stdout
        self.assertGreater(self.stat(out, 'compactions'), 0)
        self.assertGreater(self.stat(out, 'moved'), 0)
        self.assertEqual(self.stat(out, 'refused'), 0)
        pauses = list(map(int, re.findall(r'SAVE ms=(\d+)', out)))
        print('\ncompaction:', re.findall(r'STATS .*', out)[-1], re.findall(r'LOG .*', out)[-1],
              max(pauses), 'ms max save')
        self.game('edit', terrain, save, 0, 0, 0, 1)  # the next commits release the window
        out = self.game('edit', terrain, save, 0, 0, 0, 1).stdout
        self.game('check', terrain, save, 64, 140, 2)
        self.game('check', terrain, save, 0, 64, 3)
        self.banks(terrain, save)

    def test_crash_between_record_write_and_commit(self):
        terrain, save = self.media(log_sectors=2048)
        self.game('edit', terrain, save, 0, 40, 1, 40)
        committed = self.banks(terrain, save)[0][1]
        before = terrain.read_bytes()
        self.game('edit', terrain, save, 0, 40, 2, 0)  # records written, no commit
        after = terrain.read_bytes()
        self.assertNotEqual(before, after, 'the unsaved records reached the log')
        for off, n, total, previous in committed.values():
            self.assertEqual(after[off:off + v3.sectors(n) * 512], before[off:off + v3.sectors(n) * 512])
        self.game('check', terrain, save, 0, 40, 1)

    def test_both_banks_survive_later_writes(self):
        terrain, save = self.media(log_sectors=2048)
        self.game('edit', terrain, save, 0, 60, 1, 60)
        self.game('edit', terrain, save, 0, 60, 2, 60)
        pinned = {off: n for _, bank in self.banks(terrain, save) for off, n, _, _ in bank.values()}
        before = terrain.read_bytes()
        self.game('edit', terrain, save, 60, 120, 3, 0)  # unsaved records fill most free space
        after = terrain.read_bytes()
        for off, n in pinned.items():
            self.assertEqual(after[off:off + n], before[off:off + n])
        self.game('check', terrain, save, 0, 60, 2)
        newest = v3.newest_first(v3.committed_banks(save.read_bytes()))[0][0]
        flip(save, newest + 300)  # the newer bank fails its payload checksum
        self.game('check', terrain, save, 0, 60, 1)

    def test_full_log_refuses_play_records_but_saves(self):
        terrain, save = self.media(log_sectors=2048)
        out = self.game('fill', terrain, save, 0, 0, 4).stdout
        filled = self.stat(out, 'FILL columns')
        self.assertGreater(filled, 100)
        self.game('check', terrain, save, 0, filled, 4)

    def test_corrupt_records_and_directories_are_refused(self):
        terrain, save = self.media()
        self.game('edit', terrain, save, 0, 12, 1, 12)
        newest, older = self.banks(terrain, save)
        start, records = newest
        far = 3
        flip(terrain, records[v3_path(far)][0] + 7)
        out = self.game('probe', terrain, save, 0, 12, 1).stdout
        self.assertEqual(re.findall(r'CORRUPT=(\d+)', out), [str(v3_path(far))])
        data = bytearray(save.read_bytes())
        a, b = v3_path(4), v3_path(5)
        entry = lambda c: start + v3.DIRECTORY + 12 + c * 16
        data[entry(b):entry(b) + 4] = data[entry(a):entry(a) + 4]
        reseal(data, start)
        save.write_bytes(data)
        self.assertIn('refused', self.game('fail', terrain, save).stdout)
        struct.pack_into('<I', data, entry(b), (v3.log_extent(terrain.read_bytes())[0] - 1) * 512)
        reseal(data, start)
        save.write_bytes(data)
        self.assertIn('refused', self.game('fail', terrain, save).stdout)

    def test_populated_384_pair_v2_save_migrates(self):
        terrain, save = self.media(v2=True)
        self.game('edit', terrain, save, 0, 384, 1, 128)
        self.game('edit', terrain, save, 0, 100, 2, 100)
        full = self.game('edit', terrain, save, 384, 1, 3, 1, ok=False)
        self.assertNotEqual(full.returncode, 0, 'the V2 pool is full at 384 columns')
        image, updated, proof = v3.migrate_player(terrain.read_bytes(), save.read_bytes())
        self.assertEqual((proof['edited_columns'], proof['edited_records'], proof['save_banks']), (384, 484, 2))
        migrated, migrated_save = terrain.with_suffix('.v3'), save.with_suffix('.v3')
        migrated.write_bytes(image)
        migrated_save.write_bytes(updated)
        self.game('check', migrated, migrated_save, 0, 100, 2)
        self.game('check', migrated, migrated_save, 100, 284, 1)
        self.game('edit', migrated, migrated_save, 384, 600, 3, 200)
        self.game('check', migrated, migrated_save, 384, 600, 3)
        self.game('check', migrated, migrated_save, 0, 100, 2)
        older = updated[:]
        newest = v3.newest_first(v3.committed_banks(updated))[0][0]
        corrupt = bytearray(updated)
        corrupt[newest + 300] ^= 0x5a
        migrated_save.write_bytes(corrupt)
        self.game('check', migrated, migrated_save, 0, 100, 1)  # the older bank's records
        self.game('check', migrated, migrated_save, 100, 284, 1)
        bad = bytearray(save.read_bytes())
        bad[300] ^= 1
        with self.assertRaisesRegex(ValueError, 'committed'):
            v3.migrate_player(terrain.read_bytes(), bytes(bad))
        del older


def v3_path(i):
    """Column id at serpentine index i (tests/edit_log_native.c path_id)."""
    cz, cx = divmod(i, 64)
    return cz * 64 + (63 - cx if cz & 1 else cx)


if __name__ == '__main__':
    unittest.main()
