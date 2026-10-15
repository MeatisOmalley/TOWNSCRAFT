"""V3 1024-world medium: the V2 layout with its fixed edit pool replaced by a log.

  sectors 0..log_sector-1  identical to V2 (indexes, base and light records,
                           distant tiles); magic DOSCRAFT-1024-V3, version 3
  edit log                 log_sectors preallocated sectors. A column edited
                           past the per-block limit is one sector-aligned
                           record in the game's column codec (RLE, raw 12 KiB
                           when that is not smaller), placed wherever the
                           game's allocator finds room.

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

Version 8 saves (the current game) follow the directory with the per-block
edits of columns whose record is still the base:
  u32 columns   then per column, in increasing id order:
  u32 id | count << 16   count 1..DELTA_COLUMN_MAX distinct cells
  u32 cell << 8 | block  per cell; cell is the column codec's cell index
A column past that many edits is a log record instead, so most edits never
use the log. Version 5 and 6 saves have no such section.
"""
import argparse
from pathlib import Path
import struct

from bake_large_world import checksum
from convert_large_v2 import V2_MAGIC, COLUMNS, CELLS, check_v2, decode, sectors
from leaf_flags_media import SAVE_VERSION, SAVE_VERSIONS  # noqa: F401 - SAVE_VERSION for callers

V3_MAGIC = b'DOSCRAFT-1024-V3'
SECTOR = 512
RECORD_SECTORS = CELLS // SECTOR          # 24, the largest record
# Per-block edits keep most edited columns out of the log; it holds only
# columns edited past DELTA_COLUMN_MAX cells (or past the game's RAM pool).
DEFAULT_LOG_BYTES = 4 * 1024 * 1024     # new media; existing logs keep their size
# V2 player migrations move every pool record into the log; a full 384-pair
# pool needs up to 9,216 sectors, so they keep the original size.
LEGACY_LOG_BYTES = 24 * 1024 * 1024
LOG_MIN_SECTORS = 2048                    # 1 MiB; the game refuses less
LOG_MAX_SECTORS = 65536                   # 32 MiB; the game's bitmap size
SAVE_MAGIC = 0x46435354
BANK = 77 * 8192
DIRECTORY = 20 + 64 + 72
DIRECTORY_END = DIRECTORY + 12 + COLUMNS * 16
DELTA_COLUMN_MAX = 512                    # src/platform/dos/large_world.inc
DELTA_ENTRIES = 4096


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


def save_deltas(save, start):
    """{column: {cell: block}} of a committed version 8 bank; {} before that."""
    if struct.unpack_from('<I', save, start + 4)[0] < 8:
        return {}
    at = start + DIRECTORY_END
    columns = struct.unpack_from('<I', save, at)[0]
    if columns > COLUMNS:
        raise ValueError('Invalid per-block edit count')
    at += 4
    deltas, last, total = {}, -1, 0
    for _ in range(columns):
        head = struct.unpack_from('<I', save, at)[0]
        column, count = head & 0xffff, head >> 16
        if column <= last or column >= COLUMNS or not 1 <= count <= DELTA_COLUMN_MAX:
            raise ValueError('Invalid per-block edit column')
        cells = {}
        for entry in struct.unpack_from(f'<{count}I', save, at + 4):
            if entry >> 8 >= CELLS or entry >> 8 in cells:
                raise ValueError(f'Invalid per-block edit in column {column}')
            cells[entry >> 8] = entry & 0xff
        deltas[column] = cells
        last, total, at = column, total + count, at + 4 + 4 * count
    if total > DELTA_ENTRIES:
        raise ValueError('More per-block edits than the game can hold')
    return deltas


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
        if any(c in records for c in save_deltas(save, start)):
            raise ValueError('A log-record column also has per-block edits')
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


# A shrunk log must leave the game's save-time compaction threshold free (two
# 24-sector runs per resident column plus one; large_log_compact), so neither
# the first save compacts nor play refuses records.
SHRINK_RESERVE_SECTORS = 2 * 26 * RECORD_SECTORS


def directory(save, start):
    """The 4096 (offset, length, sum, previous) entries of a committed bank."""
    return [struct.unpack_from('<4I', save, start + DIRECTORY + 12 + c * 16) for c in range(COLUMNS)]


def shrink_log(v3, save, log_sectors=DEFAULT_LOG_BYTES // SECTOR):
    """V3 terrain and save -> the same world and saves on a smaller edit log.

    Every log record either committed bank's directory references is copied
    byte for byte, packed from the start of the new log in offset order. Both
    banks' offsets and previous-record refs are rewritten and each bank is
    resealed; nothing else in the save changes. The newest bank's previous
    refs must name exactly the older bank's records, as the game writes them.
    The older bank's refs name a bank that no longer exists: one that matches
    a live record moves with it, any other becomes 0 (nothing to pin). Refused
    when the live records and the reserve do not fit."""
    banks = v3_records(v3, save)
    first, count = log_extent(v3)
    if len(save) != 2 * BANK:
        raise ValueError('Expected a complete save medium')
    if not LOG_MIN_SECTORS <= log_sectors < count:
        raise ValueError(f'The new log must be {LOG_MIN_SECTORS} sectors or more and '
                         f'smaller than the current {count}')
    entries = {start: directory(save, start) for start, _ in banks}
    live = {}                                   # (offset, length) -> column
    for start, records in banks:
        for c, (off, n, total, previous) in records.items():
            if live.setdefault((off, n), c) != c:
                raise ValueError('Two columns share a log record')
    owner = {}
    for off, n in live:
        for s in range(off // SECTOR, off // SECTOR + sectors(n)):
            if owner.setdefault(s, off) != off:
                raise ValueError('Live log records overlap')
    newest = entries[banks[0][0]] if banks else []
    older = entries[banks[1][0]] if len(banks) > 1 else None
    for c, (off, n, total, previous) in enumerate(newest):
        expected = pack_ref(older[c][0], older[c][1]) if older and older[c][0] else 0
        if previous != expected:
            raise ValueError(f'The newest bank does not pin the older bank\'s record for column {c}')
    used = sum(sectors(n) for _, n in live)
    if used + SHRINK_RESERVE_SECTORS > log_sectors:
        raise ValueError(f'Live log records ({used} sectors) and the {SHRINK_RESERVE_SECTORS}-sector '
                         f'reserve do not fit a {log_sectors}-sector log')
    image = bytearray(v3[:first * SECTOR])
    struct.pack_into('<2I', image, 136, log_sectors, first + log_sectors)
    image.extend(bytes(log_sectors * SECTOR))
    place, at = {}, first * SECTOR
    for off, n in sorted(live):
        place[off, n] = at
        image[at:at + n] = v3[off:off + n]
        at += sectors(n) * SECTOR
    refs = {pack_ref(off, n): pack_ref(place[off, n], n) for off, n in live}
    updated, dropped = bytearray(save), 0
    for start, length, generation in committed_banks(save):
        for c, (off, n, total, previous) in enumerate(entries[start]):
            moved = place[off, n] if off else 0
            if previous and previous not in refs:
                dropped += 1        # only the older bank can hold such refs
            struct.pack_into('<4I', updated, start + DIRECTORY + 12 + c * 16,
                             moved, n, total, refs.get(previous, 0))
        struct.pack_into('<I', updated, start + 8, save_sum(updated[start + 20:start + 20 + length]))
    image, updated = bytes(image), bytes(updated)
    check_shrink(v3, save, image, updated)
    return image, updated, dict(
        save_banks=len(banks), live_records=len(live), live_sectors=used,
        moved_records=sum(place[k] != k[0] for k in live), dropped_previous_refs=dropped,
        previous_log_sectors=count, log_sectors=log_sectors,
        previous_bytes=len(v3), log_bytes=len(image))


def column_digests(v3, save, start, cache):
    """SHA-256 of each column a committed bank describes: its record decoded,
    then its per-block edits applied. cache maps (offset, length) to decoded
    records of this image."""
    import hashlib
    bases, deltas, out = base_records(v3), save_deltas(save, start), []
    for c, (off, n, total, previous) in enumerate(directory(save, start)):
        at = off or bases[c][0]
        if (at, n) not in cache:
            cache[at, n] = decode(v3[at:at + n], CELLS)
        column = cache[at, n]
        if c in deltas:
            column = bytearray(column)
            for cell, block in deltas[c].items():
                column[cell] = block
        out.append(hashlib.sha256(column).digest())
    return out


def check_shrink(v3, save, image, updated):
    """Independent check of shrink_log: the immutable bytes and everything in
    the save but directory offsets and refs are unchanged, every column of
    both banks decodes the same, and the new refs pin what the game needs."""
    first, count = log_extent(v3)
    _, size = log_extent(image)
    if log_extent(image)[0] != first or len(image) != (first + size) * SECTOR or \
            image[:136] != v3[:136] or image[144:first * SECTOR] != v3[144:first * SECTOR] or \
            struct.unpack_from('<I', image, 140)[0] != first + size:
        raise ValueError('Log shrink changed immutable terrain')
    banks = committed_banks(save)
    if committed_banks(updated) != banks or len(updated) != len(save):
        raise ValueError('Log shrink changed which save banks are committed')
    after = v3_records(image, updated, checked=True)
    before_cache, after_cache = {}, {}
    for start, length, generation in banks:
        if updated[start:start + 8] != save[start:start + 8] or \
                updated[start + 12:start + DIRECTORY + 12] != save[start + 12:start + DIRECTORY + 12] or \
                updated[start + DIRECTORY_END:start + BANK] != save[start + DIRECTORY_END:start + BANK]:
            raise ValueError('Player, inventory, chests, mobs, edits or bank generation changed')
        for (off, n, total, _), (moved, n2, total2, _) in zip(directory(save, start), directory(updated, start)):
            if (n2, total2) != (n, total) or bool(off) != bool(moved):
                raise ValueError('Saved record length, checksum or kind changed')
        if column_digests(v3, save, start, before_cache) != column_digests(image, updated, start, after_cache):
            raise ValueError('A column decodes differently after the log shrink')
    for start in range(0, 2 * BANK, BANK):
        if struct.unpack_from('<I', save, start)[0] != SAVE_MAGIC and \
                updated[start:start + BANK] != save[start:start + BANK]:
            raise ValueError('Empty save bank changed')
    owner, live = {}, set()
    for start, records in after:
        live.update(pack_ref(off, n) for off, n, _, _ in records.values())
        for off, n, _, _ in records.values():
            for s in range(off // SECTOR, off // SECTOR + sectors(n)):
                if owner.setdefault(s, off) != off:
                    raise ValueError('Live log records overlap after the shrink')
    newest = directory(updated, after[0][0]) if after else []
    older = directory(updated, after[1][0]) if len(after) > 1 else None
    for c, (off, n, total, previous) in enumerate(newest):
        if previous != (pack_ref(older[c][0], older[c][1]) if older and older[c][0] else 0):
            raise ValueError(f'The newest bank no longer pins the older record of column {c}')
    if older and any(previous and previous not in live for _, _, _, previous in older):
        raise ValueError('The older bank pins sectors no live record holds')


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
