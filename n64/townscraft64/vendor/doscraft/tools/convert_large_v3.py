"""V3 1024-world medium: the V2 layout with its fixed edit pool replaced by a log.

  sectors 0..log_sector-1  identical to V2 (indexes, base and light records,
                           distant tiles); magic DOSCRAFT-1024-V3, version 3
  edit log                 log_sectors preallocated sectors. Each edited column
                           is one sector-aligned record in the game's column
                           codec (RLE, raw 12 KiB when that is not smaller),
                           placed wherever the game's allocator finds room.

Header words from byte 120: base_sector, base_index (1), light_index (97),
log_sector, log_sectors, total_sectors (= log_sector + log_sectors). Words
13..33 keep their V2 meaning, so the lighting and distant readers still see
log_sector as the end of the immutable data.

On a V3 medium each V5 save directory entry (16 bytes per column) is
  u32 offset    0 for the base record, else the record's byte offset in the file
  u32 length    stored bytes; 12288 means raw
  u32 sum       column_checksum (FNV-1a) of the stored bytes
  u32 previous  the column's record in the previous committed save, so a load
                knows both banks' records: 0 for none/base, else sector<<5|count
The game never writes into a record that either committed bank references.
"""
import argparse
from pathlib import Path
import struct

from bake_large_world import checksum
from convert_large_v2 import V2_MAGIC, COLUMNS, CELLS, check_v2, decode, sectors
from leaf_flags_media import SAVE_VERSIONS

V3_MAGIC = b'DOSCRAFT-1024-V3'
SECTOR = 512
RECORD_SECTORS = CELLS // SECTOR          # 24, the largest record
DEFAULT_LOG_BYTES = 24 * 1024 * 1024    # 486 media; the 386 disk takes what fits
LOG_MIN_SECTORS = 2048                    # 1 MiB; the game refuses less
LOG_MAX_SECTORS = 65536                   # 32 MiB; the game's bitmap size
SAVE_MAGIC = 0x46435354
BANK = 77 * 8192
DIRECTORY = 20 + 64 + 72
DIRECTORY_END = DIRECTORY + 12 + COLUMNS * 16


def log_extent(v3):
    """(first sector, sector count) of the edit log."""
    first, count, total = struct.unpack_from('<3I', v3, 132)
    return first, count


def pack_ref(offset, length):
    """Save-directory form of a log record extent: sector<<5 | sector count."""
    return (offset // SECTOR) << 5 | sectors(length)


def unpack_ref(ref):
    return ref >> 5, ref & 31


def v2_view(v3):
    """The immutable part of a V3 image as a pool-less V2 image."""
    first, _ = log_extent(v3)
    view = bytearray(v3[:first * SECTOR])
    view[:16] = V2_MAGIC
    struct.pack_into('<I', view, 16, 2)
    struct.pack_into('<3I', view, 132, first, 0, first)
    return bytes(view)


def from_v2(v2, log_sectors=DEFAULT_LOG_BYTES // SECTOR, *, checked=False):
    """Fresh V3 medium: V2's immutable data, then an empty edit log."""
    if not checked:
        check_v2(v2)
    if not LOG_MIN_SECTORS <= log_sectors <= LOG_MAX_SECTORS:
        raise ValueError(f'Edit log must be {LOG_MIN_SECTORS}..{LOG_MAX_SECTORS} sectors')
    first = struct.unpack_from('<I', v2, 132)[0]
    image = bytearray(v2[:first * SECTOR])
    image.extend(bytes(log_sectors * SECTOR))
    image[:16] = V3_MAGIC
    struct.pack_into('<I', image, 16, 3)
    struct.pack_into('<3I', image, 132, first, log_sectors, first + log_sectors)
    check_v3(image, records=False)  # the immutable bytes are the checked V2 bytes
    return bytes(image)


def convert(v1, log_sectors=DEFAULT_LOG_BYTES // SECTOR):
    from convert_large_v2 import convert as to_v2
    return from_v2(to_v2(v1, pool_pairs=0), log_sectors)


def check_v3(v3, v1=None, records=True):
    """Header, extents and (through check_v2) every immutable record."""
    if v3[:16] != V3_MAGIC or len(v3) % SECTOR or struct.unpack_from('<I', v3, 16)[0] != 3:
        raise ValueError('Not a V3 1024-world image')
    first, count, total = struct.unpack_from('<3I', v3, 132)
    if total != first + count or total * SECTOR != len(v3) or \
            not LOG_MIN_SECTORS <= count <= LOG_MAX_SECTORS:
        raise ValueError('V3 edit log extent mismatch')
    if records or v1 is not None:
        check_v2(v2_view(v3), v1)


def log_size_for_volume(volume, terrain_prefix_sectors, other_bytes, headroom=1024 * 1024):
    """Largest log that fits a FatImage volume beside the other files and headroom.

    The headroom leaves room for the small report files a smoke run writes."""
    cluster = volume.spc * SECTOR
    used = sum((n + cluster - 1) // cluster for n in other_bytes)
    room = (volume.clusters - used) * cluster - headroom - terrain_prefix_sectors * SECTOR
    return min(LOG_MAX_SECTORS, room // SECTOR)


def base_records(image):
    index = struct.unpack_from('<I', image, 124)[0] * SECTOR
    return [struct.unpack_from('<3I', image, index + c * 12) for c in range(COLUMNS)]


def save_sum(data):
    value = 0
    for b in data:
        value = (value * 31 + b) & 0xffffffff
    return value


def committed_banks(save):
    """(start, length, generation) of each checked committed bank."""
    banks = []
    for bank in range(2):
        start = bank * BANK
        magic, version, total, length, generation = struct.unpack_from('<5I', save, start)
        if magic != SAVE_MAGIC:
            continue
        if version not in SAVE_VERSIONS or not DIRECTORY_END - 20 <= length <= BANK - 20 or \
                save_sum(save[start + 20:start + 20 + length]) != total:
            raise ValueError('Invalid committed save bank')
        banks.append((start, length, generation))
    return banks


def newest_first(banks):
    """Order like the game's choose_bank: signed generation difference."""
    if len(banks) == 2:
        (a, b) = banks
        if ((b[2] - a[2]) & 0xffffffff) and ((b[2] - a[2]) & 0xffffffff) < 0x80000000:
            return [b, a]
    return list(banks)


def v3_records(v3, save, checked=False):
    """Per committed bank (newest first): {column: (offset, length, sum, previous)}.

    Verifies the identity, every extent and every edited record's checksum."""
    check_v3(v3, records=not checked)
    first, count = log_extent(v3)
    bases = base_records(v3)
    index_sum = checksum(b''.join(struct.pack('<I', s) for _, _, s in bases))
    result, sums = [], {}  # both banks usually share most records
    for start, length, generation in newest_first(committed_banks(save)):
        seed, index, columns = struct.unpack_from('<3I', save, start + DIRECTORY)
        if (seed, index, columns) != (struct.unpack_from('<I', v3, 36)[0], index_sum, COLUMNS):
            raise ValueError('Save does not belong to this V3 terrain')
        records, owner = {}, {}
        for c in range(COLUMNS):
            off, n, total, previous = struct.unpack_from('<4I', save, start + DIRECTORY + 12 + c * 16)
            if previous:
                at, span = unpack_ref(previous)
                if not 1 <= span <= RECORD_SECTORS or at < first or at + span > first + count:
                    raise ValueError(f'Invalid previous record for column {c}')
            if not off:
                if (n, total) != bases[c][1:]:
                    raise ValueError(f'Invalid saved base record for column {c}')
                continue
            if off % SECTOR or not 0 < n <= CELLS or off // SECTOR < first or \
                    off // SECTOR + sectors(n) > first + count:
                raise ValueError(f'Invalid saved log record for column {c}')
            for s in range(off // SECTOR, off // SECTOR + sectors(n)):
                if owner.setdefault(s, c) != c:
                    raise ValueError('Two columns share log sectors')
            if (off, n) not in sums:
                sums[off, n] = checksum(v3[off:off + n])
                decode(v3[off:off + n], CELLS)
            if sums[off, n] != total:
                raise ValueError(f'Saved log record for column {c} is corrupt')
            records[c] = (off, n, total, previous)
        result.append((start, records))
    return result


def migrate_player(v2, save, log_sectors=DEFAULT_LOG_BYTES // SECTOR):
    """V2 terrain and V5 save -> V3 terrain and save, both banks and all edits.

    Each distinct edited record is copied byte for byte into the log, so its
    length and checksum stay valid. Directory slot fields become log offsets,
    and the newer bank also records the older bank's record per column."""
    check_v2(v2)
    if len(save) != 2 * BANK:
        raise ValueError('Expected a complete V5 save medium')
    pool, pairs = struct.unpack_from('<2I', v2, 132)
    bases = base_records(v2)
    index_sum = checksum(b''.join(struct.pack('<I', s) for _, _, s in bases))
    banks = newest_first(committed_banks(save))
    placed, owners, plan = {}, {}, []
    log = bytearray()
    for start, length, generation in banks:
        seed, index, count = struct.unpack_from('<3I', save, start + DIRECTORY)
        if (seed, index, count) != (struct.unpack_from('<I', v2, 36)[0], index_sum, COLUMNS):
            raise ValueError('Save does not belong to this V2 terrain')
        entries = []
        for c in range(COLUMNS):
            field, n, total, raw = struct.unpack_from('<4I', save, start + DIRECTORY + 12 + c * 16)
            slot, pair = field & 3, field >> 2
            if slot > 2 or not 0 < n <= CELLS or raw > 1 or raw != (n == CELLS):
                raise ValueError(f'Invalid saved V2 record for column {c}')
            if not slot:
                if field or (n, total) != bases[c][1:]:
                    raise ValueError(f'Invalid saved V2 base record for column {c}')
                entries.append(None)
                continue
            if pair >= pairs or owners.setdefault(pair, c) != c:
                raise ValueError('Invalid or shared V2 edit pair')
            key = (c, field)
            if key not in placed:
                at = (pool + (pair * 2 + slot - 1) * RECORD_SECTORS) * SECTOR
                payload = v2[at:at + n]
                if checksum(payload) != total:
                    raise ValueError(f'Saved V2 edit slot for column {c} is corrupt')
                decode(payload, CELLS)
                placed[key] = (len(log), n, total)
                log += payload + bytes(sectors(n) * SECTOR - n)
            elif placed[key][1:] != (n, total):
                raise ValueError('Save banks disagree about a V2 edit slot')
            entries.append(placed[key])
        plan.append((start, length, entries))
    if len(log) // SECTOR > log_sectors:
        raise ValueError('Edit log too small for the saved edits')
    image = bytearray(from_v2(v2, log_sectors, checked=True))
    first, _ = log_extent(image)
    image[first * SECTOR:first * SECTOR + len(log)] = log
    updated = bytearray(save)
    for i, (start, length, entries) in enumerate(plan):
        older = plan[i + 1][2] if i + 1 < len(plan) else [None] * COLUMNS
        for c in range(COLUMNS):
            at = start + DIRECTORY + 12 + c * 16
            n, total = struct.unpack_from('<2I', save, at + 4)
            entry, prior = entries[c], older[c] if i == 0 else None
            off = first * SECTOR + entry[0] if entry else 0
            previous = pack_ref(first * SECTOR + prior[0], prior[1]) if prior else 0
            struct.pack_into('<4I', updated, at, off, n, total, previous)
        struct.pack_into('<I', updated, start + 8, save_sum(updated[start + 20:start + 20 + length]))
    check_migration(v2, save, bytes(image), bytes(updated))
    return bytes(image), bytes(updated), dict(
        edited_columns=len({c for c, _ in placed}), edited_records=len(placed),
        save_banks=len(banks), previous_pool_pairs=pairs, log_sectors=log_extent(image)[1],
        log_used_sectors=len(log) // SECTOR, previous_bytes=len(v2), log_bytes=len(image))


def check_migration(v2, save, v3, updated):
    """Independent check: same logical columns per bank; only directory words differ."""
    if v2[512:struct.unpack_from('<I', v2, 132)[0] * SECTOR] != v3[512:log_extent(v3)[0] * SECTOR] or \
            v2[20:132] != v3[20:132] or v2[144:512] != v3[144:512]:
        raise ValueError('Migration changed immutable terrain')
    pool = struct.unpack_from('<I', v2, 132)[0]
    after = {start: records for start, records in v3_records(v3, updated, checked=True)}
    for start, length, generation in committed_banks(save):
        if updated[start:start + 8] != save[start:start + 8] or \
                updated[start + 12:start + DIRECTORY + 12] != save[start + 12:start + DIRECTORY + 12] or \
                updated[start + DIRECTORY_END:start + BANK] != save[start + DIRECTORY_END:start + BANK]:
            raise ValueError('Player, inventory, chests, mobs or bank generation changed')
        for c in range(COLUMNS):
            at = start + DIRECTORY + 12 + c * 16
            field, n, total, raw = struct.unpack_from('<4I', save, at)
            if struct.unpack_from('<2I', updated, at + 4) != (n, total):
                raise ValueError('Saved record length or checksum changed')
            if not field:
                if c in after[start]:
                    raise ValueError('Base column became an edit')
                continue
            old = (pool + ((field >> 2) * 2 + (field & 3) - 1) * RECORD_SECTORS) * SECTOR
            off = after[start][c][0]
            if decode(v3[off:off + n], CELLS) != decode(v2[old:old + n], CELLS):
                raise ValueError(f'Edited column {c} changed during migration')
    for start in range(0, 2 * BANK, BANK):
        if struct.unpack_from('<I', save, start)[0] != SAVE_MAGIC and \
                updated[start:start + BANK] != save[start:start + BANK]:
            raise ValueError('Empty save bank changed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--terrain', type=Path, required=True, help='V1 or V2 image')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--log-mib', type=float, default=DEFAULT_LOG_BYTES / 1048576)
    args = parser.parse_args()
    data = args.terrain.read_bytes()
    log_sectors = int(args.log_mib * 2048)
    v3 = from_v2(data, log_sectors) if data[:16] == V2_MAGIC else convert(data, log_sectors)
    with args.output.open('xb') as f:
        f.write(v3)
    print(f'{args.output}: {len(v3)} bytes ({len(v3) / 1e6:.1f} MB), '
          f'{log_sectors * SECTOR / 1048576:.1f} MiB edit log')


if __name__ == '__main__':
    main()
