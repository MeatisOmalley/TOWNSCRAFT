"""Convert a checked V1 1024-world terrain image into the compact V2 layout.

V1 stores every column raw in three fixed 12 KiB slots (one base, two edit),
lighting raw at 13 sectors per column and the distant map row-major: 178 MB.
V2 keeps the same column codec and data, packed:

  sector 0        header (V1 fields kept; V2 fields from byte 120)
  sectors 1..96   base index: per column u32 byte offset, u32 length, u32 sum
  sectors 97..192 light index: the same for the lighting records
  base region     each column run-length encoded with the game's column codec
                  (count,value pairs; raw when that is not smaller), sector aligned
  light region    each 6400-byte lighting record encoded the same way
  distant map     1024 tiles of 16x16 cells (one 512-byte sector each)
  edit pool       pool_pairs pairs of 24-sector edit slots, assigned to a
                  column on its first edit (the save records the pair)

Sums are column_checksum (FNV-1a) of the stored bytes. Only fresh V1 images
(no edits in the V1 slots) convert; player disks keep their V1 medium.
"""
import argparse
from pathlib import Path
import struct

from bake_large_world import checksum
from large_world_media import FAR_BYTES, FAR_MAGIC, LIGHT_MAGIC, check_terrain

V2_MAGIC = b'DOSCRAFT-1024-V2'
COLUMNS = 4096
CELLS = 12288
LIGHT_RECORD = 6400
INDEX_SECTOR = 1
INDEX_SECTORS = COLUMNS * 12 // 512   # 96 per index
SLOT_SECTORS = CELLS // 512           # 24
TILE_CELLS = 16
DEFAULT_POOL_PAIRS = 384


def rle(data):
    """The game's column codec (column_encode): (count, value) byte pairs."""
    out = bytearray()
    last, n = -1, 0
    for v in data:
        if v != last or n == 255:
            if n:
                out += bytes((n, last))
            last, n = v, 0
        n += 1
    if n:
        out += bytes((n, last))
    return out


def unrle(data, size):
    out = bytearray()
    for i in range(0, len(data), 2):
        out += bytes((data[i + 1],)) * data[i]
    if len(out) != size:
        raise ValueError('Run-length record does not expand to its size')
    return bytes(out)


def encode(data):
    """Stored bytes: the run-length form when smaller, else raw."""
    packed = rle(data)
    return bytes(packed) if len(packed) < len(data) else bytes(data)


def decode(stored, size):
    return bytes(stored) if len(stored) == size else unrle(stored, size)


def sectors(n):
    return (n + 511) // 512


def far_tiles(row_major):
    """Row-major 512x512 cell map (2 bytes per cell) to 16x16-cell tiles."""
    tiles = bytearray(len(row_major))
    for tz in range(32):
        for tx in range(32):
            base = (tz * 32 + tx) * 512
            for z in range(TILE_CELLS):
                src = ((tz * 16 + z) * 512 + tx * 16) * 2
                tiles[base + z * 32:base + z * 32 + 32] = row_major[src:src + 32]
    return tiles


def convert(v1, pool_pairs=DEFAULT_POOL_PAIRS):
    check_terrain(v1)
    if len(v1) != FAR_BYTES or struct.unpack_from('<I', v1, 52)[0] != LIGHT_MAGIC or \
            struct.unpack_from('<I', v1, 72)[0] != FAR_MAGIC:
        raise ValueError('Expected a lit V1 image with a distant map and no tree appendix')
    light_base = struct.unpack_from('<I', v1, 56)[0]
    far_sector = struct.unpack_from('<I', v1, 76)[0]
    base_index, light_index = bytearray(COLUMNS * 12), bytearray(COLUMNS * 12)
    base_data, light_data = [], []
    for column in range(COLUMNS):
        first = (33 + column * SLOT_SECTORS * 3) * 512
        for slot in (1, 2):
            if any(v1[first + slot * CELLS:first + (slot + 1) * CELLS]):
                raise ValueError('V1 image holds player edits; convert a fresh bake')
        base_data.append(encode(v1[first:first + CELLS]))
        light = light_base * 512 + column * 13 * 512
        light_data.append(encode(v1[light:light + LIGHT_RECORD]))
    base_sector = INDEX_SECTOR + 2 * INDEX_SECTORS
    light_sector = base_sector + sum(sectors(len(d)) for d in base_data)
    tile_sector = light_sector + sum(sectors(len(d)) for d in light_data)
    pool_sector = tile_sector + 1024
    total_sectors = pool_sector + pool_pairs * 2 * SLOT_SECTORS
    image = bytearray(total_sectors * 512)
    for data, index, start in ((base_data, base_index, base_sector),
                               (light_data, light_index, light_sector)):
        at = start
        for column, stored in enumerate(data):
            image[at * 512:at * 512 + len(stored)] = stored
            struct.pack_into('<3I', index, column * 12, at * 512, len(stored), checksum(stored))
            at += sectors(len(stored))
    tiles = far_tiles(v1[far_sector * 512:far_sector * 512 + 512 * 512 * 2])
    image[tile_sector * 512:(tile_sector + 1024) * 512] = tiles
    image[512:512 + len(base_index)] = base_index
    image[512 + len(base_index):512 + 2 * len(base_index)] = light_index
    header = bytearray(v1[:512])
    header[:16] = V2_MAGIC
    struct.pack_into('<I', header, 16, 2)                       # version
    struct.pack_into('<I', header, 48, checksum(base_index))    # index sum
    struct.pack_into('<5I', header, 52, LIGHT_MAGIC, light_sector, 0,
                     checksum(light_index), LIGHT_RECORD)
    magic, _, size, _, _ = struct.unpack_from('<5I', v1, 72)
    struct.pack_into('<5I', header, 72, magic, tile_sector, size, checksum(tiles), 2)
    struct.pack_into('<6I', header, 120, base_sector, INDEX_SECTOR,
                     INDEX_SECTOR + INDEX_SECTORS, pool_sector, pool_pairs, total_sectors)
    image[:512] = header
    return bytes(image)


def check_v2(v2, v1=None):
    """Structural check, and when V1 is given, exact equivalence."""
    if v2[:16] != V2_MAGIC or len(v2) % 512:
        raise ValueError('Not a V2 1024-world image')
    base_sector, base_index, light_index, pool_sector, pool_pairs, total = \
        struct.unpack_from('<6I', v2, 120)
    if total * 512 != len(v2) or checksum(v2[512:512 + COLUMNS * 12]) != struct.unpack_from('<I', v2, 48)[0]:
        raise ValueError('V2 size or index checksum mismatch')
    sums = {}  # columns may share one immutable record; hash each extent once
    for column in range(COLUMNS):
        for index, size in ((base_index, CELLS), (light_index, LIGHT_RECORD)):
            off, length, sum_ = struct.unpack_from('<3I', v2, index * 512 + column * 12)
            stored = v2[off:off + length]
            if (off, length) not in sums:
                sums[off, length] = checksum(stored)
            if off % 512 or sums[off, length] != sum_ or off + length > pool_sector * 512:
                raise ValueError(f'Bad V2 record for column {column}')
            if v1 is not None:
                data = decode(stored, size)
                if index == base_index:
                    first = (33 + column * SLOT_SECTORS * 3) * 512
                    expected = v1[first:first + CELLS]
                else:
                    light = struct.unpack_from('<I', v1, 56)[0] * 512 + column * 13 * 512
                    expected = v1[light:light + LIGHT_RECORD]
                if data != expected:
                    raise ValueError(f'V2 column {column} differs from V1')
    if v1 is not None:
        far = struct.unpack_from('<I', v1, 76)[0] * 512
        tile_sector = struct.unpack_from('<I', v2, 76)[0]
        if v2[tile_sector * 512:(tile_sector + 1024) * 512] != far_tiles(v1[far:far + 512 * 512 * 2]):
            raise ValueError('V2 distant map differs from V1')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--terrain', type=Path, required=True, help='V1 image')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pool-pairs', type=int, default=DEFAULT_POOL_PAIRS,
                        help=f'edit slot pairs (24 KiB each; default {DEFAULT_POOL_PAIRS})')
    args = parser.parse_args()
    v1 = args.terrain.read_bytes()
    v2 = convert(v1, args.pool_pairs)
    check_v2(v2, v1)
    with args.output.open('xb') as f:
        f.write(v2)
    print(f'{args.output}: {len(v2)} bytes ({len(v2) / 1e6:.1f} MB), '
          f'{args.pool_pairs} edit slot pairs')


if __name__ == '__main__':
    main()
