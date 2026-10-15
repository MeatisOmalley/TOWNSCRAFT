"""V3 edit log: the actual world cache, save codec and DOS adapters on private media.

Each scenario step is a separate native process (tests/edit_log_native.c), so
a reload never relies on RAM from the run that wrote the media. The medium is
synthetic but has the real layout and realistic record sizes: 64 shared base
columns of 7 sectors each (the baked seed averages 3,287 bytes).
"""
from pathlib import Path
import os
import re
import struct
import subprocess
import sys
import tempfile
import time
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_adapters
import dos_sources
from bake_large_world import checksum
from convert_large_v2 import V2_MAGIC, encode, check_v2
from fat_image import FatImage, populate, scratch_hdd
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
                     ROOT / 'src/platform/dos/early_game.c',
                     *(src / s for s in ('inventory.c', 'blocks.c', 'fmath.c', 'libc.c')),
                     tables, *objects, '-o', cls.exe])
        cls.v2 = synthetic_v2()
        cls.case = 0

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @staticmethod
    def run_cmd(args, ok=True, env=None):
        result = subprocess.run(list(map(str, args)), capture_output=True, text=True, errors='replace',
                                timeout=900, env=env)
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

    def game(self, mode, terrain, save, *args, ok=True, delta_max=None):
        """delta_max: per-block edits per column before a full log record
        (the native default, 0, makes every edited column a log record)."""
        env = None if delta_max is None else {**os.environ, 'DELTA_MAX': str(delta_max)}
        result = self.run_cmd([self.exe, mode, terrain, save, *args], ok=False, env=env)
        if ok:
            self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-3000:])
            self.assertIn('PASS', result.stdout)
        return result

    @staticmethod
    def stat(output, name):
        return int(re.findall(rf'\b{name}=(\d+)', output)[-1])

    def banks(self, terrain, save):
        return v3.v3_records(terrain.read_bytes(), save.read_bytes(), checked=True)

    def test_bitmap_scans_match_bit_by_bit_reference(self):
        self.assertIn('PASS: bitmap scans', self.run_cmd([self.exe, 'bits']).stdout)

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
        # Full records for every column: the log size V3 media used before
        # per-block edits, and still the size of migrated V2 players.
        self.every_column(v3.LEGACY_LOG_BYTES // 512)

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

    # Per-block edits (save version 8), with the game's own limit of 512.

    def test_per_block_edits_need_no_log_records(self):
        terrain, save = self.media(log_sectors=v3.DEFAULT_LOG_BYTES // 512)
        before = terrain.read_bytes()
        out = self.game('cells', terrain, save, 0, 100, 1, 3, delta_max=512).stdout
        self.assertEqual((self.stat(out, 'live_records'), self.stat(out, 'writes')), (0, 0))
        self.assertEqual((self.stat(out, 'entries'), self.stat(out, 'columns'), self.stat(out, 'switched')),
                         (300, 100, 0))
        self.assertEqual(terrain.read_bytes(), before, 'evictions and the save left the log untouched')
        self.game('cellcheck', terrain, save, 0, 100, 1, 3)
        start, records = self.banks(terrain, save)[0]  # newest
        self.assertEqual(records, {})
        deltas = v3.save_deltas(save.read_bytes(), start)
        self.assertEqual(len(deltas), 100)
        self.assertTrue(all(len(cells) == 3 for cells in deltas.values()))

    def test_every_column_edited_once_fits_the_pool(self):
        terrain, save = self.media(log_sectors=v3.DEFAULT_LOG_BYTES // 512)
        out = self.game('cells', terrain, save, 0, 4096, 2, 1, delta_max=512).stdout
        self.assertEqual((self.stat(out, 'live_records'), self.stat(out, 'entries')), (0, 4096))
        print('\nall columns, per-block:', re.findall(r'SAVE ms=\d+', out)[-1], re.findall(r'DELTA .*', out)[-1])
        self.game('cellcheck', terrain, save, 0, 4096, 2, 1)

    def test_heavily_edited_columns_switch_to_log_records(self):
        terrain, save = self.media(log_sectors=v3.DEFAULT_LOG_BYTES // 512)
        out = self.game('cells', terrain, save, 0, 3, 3, 40, delta_max=16).stdout
        self.assertEqual((self.stat(out, 'live_records'), self.stat(out, 'switched'),
                          self.stat(out, 'entries')), (3, 3, 0))
        out = self.game('cells', terrain, save, 3, 2, 4, 10, delta_max=16).stdout
        self.assertEqual((self.stat(out, 'live_records'), self.stat(out, 'entries')), (3, 20))
        # A log column stays one: later edits go into its record.
        out = self.game('cells', terrain, save, 0, 1, 5, 2, delta_max=16).stdout
        self.assertEqual((self.stat(out, 'live_records'), self.stat(out, 'entries')), (3, 20))
        self.game('cellcheck', terrain, save, 1, 2, 3, 40)
        self.game('cellcheck', terrain, save, 0, 1, 5, 2)
        self.game('cellcheck', terrain, save, 3, 2, 4, 10)
        newest = self.banks(terrain, save)[0]
        self.assertEqual(len(newest[1]), 3)
        self.assertEqual(sorted(v3.save_deltas(save.read_bytes(), newest[0])), [v3_path(3), v3_path(4)])

    def test_empty_pool_switches_columns_to_log_records(self):
        terrain, save = self.media(log_sectors=v3.DEFAULT_LOG_BYTES // 512)
        out = self.game('cells', terrain, save, 0, 9, 1, 500, delta_max=512).stdout
        self.assertGreater(self.stat(out, 'switched'), 0)
        self.assertLessEqual(self.stat(out, 'entries'), v3.DELTA_ENTRIES)
        self.assertEqual(self.stat(out, 'live_records'), self.stat(out, 'switched'))
        self.game('cellcheck', terrain, save, 0, 9, 1, 500)

    def test_only_the_current_save_version_loads(self):
        terrain, save = self.media(log_sectors=v3.DEFAULT_LOG_BYTES // 512)
        self.game('edit', terrain, save, 0, 6, 1, 6)  # log records; no per-block edits
        written = bytearray(save.read_bytes())
        start, records = self.banks(terrain, save)[0]  # newest
        self.assertEqual(len(records), 6)
        self.assertEqual(struct.unpack_from('<I', written, start + 4)[0], v3.SAVE_VERSION)
        self.game('check', terrain, save, 0, 6, 1)
        for version in (7, 6, 5):
            # Older banks hold other item IDs or another layout: the game skips
            # them and loads the older bank, which lacks these edits.
            data = bytearray(written)
            struct.pack_into('<I', data, start + 4, version)
            save.write_bytes(data)
            self.assertNotEqual(self.game('check', terrain, save, 0, 6, 1, ok=False).returncode, 0, version)

    def test_corrupt_per_block_edits_are_refused(self):
        terrain, save = self.media(log_sectors=v3.DEFAULT_LOG_BYTES // 512)
        self.game('cells', terrain, save, 0, 4, 1, 2, delta_max=512)
        start = self.banks(terrain, save)[0][0]  # newest
        good = save.read_bytes()
        at = start + v3.DIRECTORY_END + 4 + 4  # first column's first cell
        for bad in (struct.pack('<I', 12288 << 8 | 4),             # cell past the column
                    good[at + 4:at + 8]):                          # the same cell twice
            data = bytearray(good)
            data[at:at + 4] = bad
            reseal(data, start)
            save.write_bytes(data)
            self.assertIn('refused', self.game('fail', terrain, save).stdout)
            with self.assertRaisesRegex(ValueError, 'per-block'):
                self.banks(terrain, save)
        save.write_bytes(good)
        self.game('cellcheck', terrain, save, 0, 4, 1, 2)

    def test_populated_384_pair_v2_disk_migrates(self):
        from large_world_media import upgrade_disk
        from log_player_vm import migrate_disk
        terrain, save = self.media(v2=True)
        self.game('edit', terrain, save, 0, 384, 1, 128)
        self.game('edit', terrain, save, 0, 100, 2, 100)
        full = self.game('edit', terrain, save, 384, 1, 3, 1, ok=False)
        self.assertNotEqual(full.returncode, 0, 'the V2 pool is full at 384 columns')
        disk = scratch_hdd(cylinders=977, heads=5, sectors=17)
        part = struct.unpack_from('<I', disk, 454)[0] * 512
        disk[part:] = populate(disk[part:part + 512], [('TERRAIN.TMP', terrain.read_bytes()),
                                                       ('WORLD.SAV', save.read_bytes())])
        before = bytes(disk)
        t0 = time.time()
        result, log, proof = migrate_disk(before)
        print(f'\nmigration: {time.time() - t0:.1f} s', {k: proof[k] for k in ('log_sectors', 'log_used_sectors', 'edited_records')})
        self.assertEqual(bytes(disk), before)
        self.assertEqual(len(result), len(before))
        self.assertEqual((proof['edited_columns'], proof['edited_records'], proof['save_banks']), (384, 484, 2))
        volume = FatImage(result, part)
        migrated, migrated_save = terrain.with_suffix('.v3'), save.with_suffix('.v3')
        migrated.write_bytes(volume.read('TERRAIN.TMP'))
        migrated_save.write_bytes(volume.read('WORLD.SAV'))
        self.game('check', migrated, migrated_save, 0, 100, 2)
        self.game('check', migrated, migrated_save, 100, 284, 1)
        # The older bank migrated too: with the newer one damaged, it loads.
        newest = v3.newest_first(v3.committed_banks(migrated_save.read_bytes()))[0][0]
        older_only = save.with_suffix('.older')
        damaged = bytearray(migrated_save.read_bytes())
        damaged[newest + 300] ^= 0x5a
        older_only.write_bytes(damaged)
        self.game('check', migrated, older_only, 0, 100, 1)
        self.game('check', migrated, older_only, 100, 284, 1)
        # Past the old 384-column limit, then a republish keeps the log.
        self.game('edit', migrated, migrated_save, 384, 600, 3, 200)
        self.game('check', migrated, migrated_save, 384, 600, 3)
        disk = bytearray(result)
        disk[part:] = populate(disk[part:part + 512], [('TERRAIN.TMP', migrated.read_bytes()),
                                                       ('WORLD.SAV', migrated_save.read_bytes())])
        republished = FatImage(upgrade_disk(bytes(disk), self.v2), part)
        self.assertEqual(republished.read('TERRAIN.TMP'), migrated.read_bytes())
        self.assertEqual(republished.read('WORLD.SAV'), migrated_save.read_bytes())
        self.game('check', migrated, migrated_save, 0, 100, 2)
        bad = bytearray(save.read_bytes())
        bad[300] ^= 1
        with self.assertRaisesRegex(ValueError, 'committed'):
            v3.migrate_player(terrain.read_bytes(), bytes(bad))

    # A V3 player disk with a 24 MiB log moves onto new media's 4 MiB log.

    @staticmethod
    def player_disk(terrain, save):
        disk = scratch_hdd(cylinders=977, heads=5, sectors=17)
        part = struct.unpack_from('<I', disk, 454)[0] * 512
        disk[part:] = populate(disk[part:part + 512], [('TERRAIN.TMP', terrain.read_bytes()),
                                                       ('WORLD.SAV', save.read_bytes())])
        return bytes(disk), part

    def unpack(self, disk, part, terrain, save, suffix):
        volume = FatImage(disk, part)
        moved, moved_save = terrain.with_suffix(suffix), save.with_suffix(suffix)
        moved.write_bytes(volume.read('TERRAIN.TMP'))
        moved_save.write_bytes(volume.read('WORLD.SAV'))
        return moved, moved_save

    def older_only(self, save):
        """A copy of the save whose newest bank fails its checksum."""
        data = bytearray(save.read_bytes())
        data[v3.newest_first(v3.committed_banks(bytes(data)))[0][0] + 300] ^= 0x5a
        older = save.with_suffix('.older')
        older.write_bytes(data)
        return older

    def test_v3_player_disk_moves_onto_a_4_mib_log(self):
        from log_player_vm import migrate_disk
        terrain, save = self.media(log_sectors=v3.LEGACY_LOG_BYTES // 512)
        # Log records, saved each round; the fourth round lies past 4 MiB.
        self.game('edit', terrain, save, 0, 400, 1, 400, 4)
        self.game('cells', terrain, save, 1000, 50, 2, 3, delta_max=512)  # per-block edits
        self.game('edit', terrain, save, 0, 400, 5, 400)            # banks now differ in records
        newest, older = self.banks(terrain, save)
        first = v3.log_extent(terrain.read_bytes())[0] * 512
        small = first + v3.DEFAULT_LOG_BYTES
        self.assertTrue(any(off + n > small for off, n, _, _ in older[1].values()),
                        'records lie past the 4 MiB the shrunk log keeps')
        self.assertEqual((len(newest[1]), len(older[1])), (400, 400))
        self.assertEqual(len(v3.save_deltas(save.read_bytes(), older[0])), 50)
        before, part = self.player_disk(terrain, save)
        t0 = time.time()
        result, log, proof = migrate_disk(before)
        print(f'\nlog shrink: {time.time() - t0:.1f} s', {k: proof[k] for k in (
            'live_records', 'live_sectors', 'moved_records', 'free_bytes_before', 'free_bytes_after')})
        self.assertEqual(len(result), len(before))
        self.assertEqual(before, self.player_disk(terrain, save)[0], 'the source disk is unchanged')
        self.assertEqual((proof['migration'], proof['save_banks'], proof['live_records'], proof['log_sectors']),
                         ('V3 log shrink', 2, 800, v3.DEFAULT_LOG_BYTES // 512))
        self.assertGreater(proof['moved_records'], 0)
        self.assertEqual(proof['dropped_previous_refs'], 0)
        self.assertEqual(len(terrain.read_bytes()) - len(log), v3.LEGACY_LOG_BYTES - v3.DEFAULT_LOG_BYTES)
        self.assertGreaterEqual(proof['free_bytes_after'] - proof['free_bytes_before'],
                                v3.LEGACY_LOG_BYTES - v3.DEFAULT_LOG_BYTES)
        moved, moved_save = self.unpack(result, part, terrain, save, '.small')
        self.assertTrue(all(off + n <= small for _, bank in self.banks(moved, moved_save)
                            for off, n, _, _ in bank.values()))
        # Both banks load in the game with their own records and edits.
        self.game('check', moved, moved_save, 0, 400, 5)
        self.game('cellcheck', moved, moved_save, 1000, 50, 2, 3)
        older_save = self.older_only(moved_save)
        self.game('check', moved, older_save, 0, 400, 4)
        self.game('cellcheck', moved, older_save, 1000, 50, 2, 3)
        # Play continues on the small log, saving every 50 log columns: the
        # head wraps into released records and nothing is refused.
        out = self.game('edit', moved, moved_save, 0, 200, 0, 50, 2).stdout
        self.assertEqual((self.stat(out, 'refused'), self.stat(out, 'failed')), (0, 0))
        self.assertGreater(self.stat(out, 'wraps'), 0)
        self.game('check', moved, moved_save, 0, 200, 1)
        self.game('check', moved, moved_save, 200, 200, 5)
        self.game('cellcheck', moved, moved_save, 1000, 50, 2, 3)
        with self.assertRaisesRegex(ValueError, 'already has'):
            migrate_disk(self.player_disk(moved, moved_save)[0])

    def test_log_shrink_refuses_records_that_do_not_fit(self):
        from log_player_vm import migrate_disk
        terrain, save = self.media(log_sectors=v3.LEGACY_LOG_BYTES // 512)
        self.game('edit', terrain, save, 0, 1000, 1, 1000, 3)
        before, part = self.player_disk(terrain, save)
        with self.assertRaisesRegex(ValueError, 'do not fit'):
            migrate_disk(before)
        # A log with room takes them; the older bank's refs to the bank it
        # replaced name nothing live and are dropped.
        result, log, proof = migrate_disk(before, 16384)
        self.assertEqual((proof['live_records'], proof['dropped_previous_refs']), (2000, 1000))
        self.assertGreater(proof['live_sectors'], v3.DEFAULT_LOG_BYTES // 512 - v3.SHRINK_RESERVE_SECTORS)
        moved, moved_save = self.unpack(result, part, terrain, save, '.mid')
        self.game('check', moved, moved_save, 0, 1000, 3)
        self.game('check', moved, self.older_only(moved_save), 0, 1000, 2)

    def test_log_player_vm_stages_a_4_mib_clone(self):
        import hashlib
        import json
        from unittest import mock
        import log_player_vm
        terrain, save = self.media(log_sectors=v3.LEGACY_LOG_BYTES // 512)
        self.game('edit', terrain, save, 0, 30, 1, 30, 2)
        disk, part = self.player_disk(terrain, save)
        root = Path(self.tmp.name) / f'vms{self.case}'
        sha = lambda data: hashlib.sha256(data).hexdigest()

        def vm(name, exe):
            floppy = bytearray(512)
            struct.pack_into('<HBHBHHBH', floppy, 11, 512, 1, 1, 2, 224, 2880, 0xf0, 9)
            struct.pack_into('<HH', floppy, 24, 18, 2)
            floppy[510:512] = b'\x55\xaa'
            boot = bytes(populate(floppy, [('DOSCRAFT.EXE', exe)]))
            folder = root / 'runtime' / name
            folder.mkdir(parents=True)
            (folder / 'boot.img').write_bytes(boot)
            (folder / 'DOSCRAFT.ISO').write_bytes(b'iso ' + name.encode())
            (folder / 'build.json').write_text(json.dumps({
                'smoke': False, 'benchmark': False, 'vm_directory': str(folder), 'exe_sha256': sha(exe),
                'boot_sha256': sha(boot), 'iso_sha256': sha(b'iso ' + name.encode())}))
            return folder
        player = vm('game-play-old', b'MZ ' + v3.V3_MAGIC)
        (player / 'hdd.img').write_bytes(disk)
        release = vm('game-play-new', b'MZ ' + v3.V3_MAGIC + b' ' + log_player_vm.PER_BLOCK_EDITS_MARKER)
        pointer = root / 'latest-play-vm.txt'
        pointer.write_text(str(player))
        with mock.patch.object(log_player_vm, 'ROOT', root), \
                mock.patch.object(log_player_vm, 'running', lambda vm: False):
            checked = log_player_vm.install(pointer, check=True)
            self.assertFalse(checked['executable_has_per_block_edits'])
            with self.assertRaisesRegex(ValueError, 'per-block'):
                log_player_vm.install(pointer)
            self.assertEqual(sorted(p.name for p in (root / 'runtime').iterdir()), ['game-play-new', 'game-play-old'])
            staged = log_player_vm.install(pointer, boot_from=release)
        clone = Path(staged['vm'])
        self.assertIn('launcher unchanged', staged['status'])
        self.assertEqual(pointer.read_text(), str(player))
        self.assertEqual((player / 'hdd.img').read_bytes(), disk, 'the original VM is never written')
        record = json.loads((clone / 'build.json').read_text())
        self.assertEqual((record['edit_log_sectors'], record['exe_sha256']),
                         (v3.DEFAULT_LOG_BYTES // 512, json.loads((release / 'build.json').read_text())['exe_sha256']))
        self.assertEqual((clone / 'boot.img').read_bytes(), (release / 'boot.img').read_bytes())
        moved, moved_save = self.unpack((clone / 'hdd.img').read_bytes(), part, terrain, save, '.clone')
        self.assertEqual(sha(moved.read_bytes()), record['terrain_sha256'])
        self.game('check', moved, moved_save, 0, 30, 2)

    def test_log_shrink_refuses_damaged_or_inconsistent_saves(self):
        terrain, save = self.media(log_sectors=v3.LEGACY_LOG_BYTES // 512)
        self.game('edit', terrain, save, 0, 30, 1, 30, 2)
        image, good = terrain.read_bytes(), save.read_bytes()
        self.assertEqual(len(v3.shrink_log(image, good)[0]),
                         (v3.log_extent(image)[0] + v3.DEFAULT_LOG_BYTES // 512) * 512)
        newest, older = self.banks(terrain, save)
        start, records = newest
        column = sorted(records)[0]
        entry = start + v3.DIRECTORY + 12 + column * 16
        # The newest bank must pin the older bank's record, as the game writes it.
        data = bytearray(good)
        struct.pack_into('<I', data, entry + 12, 0)
        reseal(data, start)
        with self.assertRaisesRegex(ValueError, 'pin'):
            v3.shrink_log(image, bytes(data))
        data = bytearray(good)
        data[start + 300] ^= 1                   # a damaged bank
        with self.assertRaisesRegex(ValueError, 'committed'):
            v3.shrink_log(image, bytes(data))
        damaged = bytearray(image)
        damaged[records[column][0] + 7] ^= 0x5a  # a damaged live record
        with self.assertRaisesRegex(ValueError, 'corrupt'):
            v3.shrink_log(bytes(damaged), good)
        with self.assertRaisesRegex(ValueError, 'smaller'):
            v3.shrink_log(image, good, v3.log_extent(image)[1])


def v3_path(i):
    """Column id at serpentine index i (tests/edit_log_native.c path_id)."""
    cz, cx = divmod(i, 64)
    return cz * 64 + (63 - cx if cz & 1 else cx)


if __name__ == '__main__':
    unittest.main()
