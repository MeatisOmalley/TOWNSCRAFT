"""Deterministic ISO9660 level-1 data CD with an El Torito floppy boot image.

build_iso returns bytes and performs no host I/O. The caller supplies the entire
bootable 1.44-MiB floppy (for example, from fat_image.populate) and a mapping of
root-only 8.3 filenames to bytes. The ISO filesystem is read-only; DOS boot files,
CD drivers, launch commands and HDD save setup belong to the caller.
"""
from collections.abc import Mapping
import struct


SECTOR_SIZE = 2048
FLOPPY_SIZE = 1440 * 1024
_RECORD_DATE = bytes((126, 10, 9, 0, 0, 0, 0))
_VOLUME_DATE = b'2026100900000000\x00'
_UINT32_MAX = 0xffffffff


def _both16(value):
    return struct.pack('<H', value) + struct.pack('>H', value)


def _both32(value):
    return struct.pack('<I', value) + struct.pack('>I', value)


def _bytes(value, label):
    if not isinstance(value, (bytes, bytearray, memoryview)):
        raise TypeError(label + ' must be bytes-like')
    if isinstance(value, memoryview):
        size = value.nbytes
    else:
        size = len(value)
    if size > _UINT32_MAX:
        raise ValueError(label + ' exceeds ISO9660 single-extent size')
    return bytes(value)


def _identifier(name):
    if not isinstance(name, str):
        raise TypeError('Filename must be a string')
    if not name.isascii():
        raise ValueError('Filename must be ASCII')
    parts = name.upper().split('.')
    if (len(parts) > 2 or not 1 <= len(parts[0]) <= 8
            or (len(parts) == 2 and not 1 <= len(parts[1]) <= 3)):
        raise ValueError('Only root 8.3 filenames supported')
    if any(c not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_' for p in parts for c in p):
        raise ValueError('Filename must use ISO9660 letters, digits or underscore')
    # ISO level 1 keeps the extension separator even for extensionless files.
    return (parts[0] + '.' + (parts[1] if len(parts) == 2 else '') + ';1').encode('ascii')


def _record(identifier, extent, size, directory=False):
    length = 33 + len(identifier) + (not len(identifier) % 2)
    record = bytearray(length)
    record[0] = length
    record[2:10] = _both32(extent)
    record[10:18] = _both32(size)
    record[18:25] = _RECORD_DATE
    record[25] = 2 if directory else 0
    record[28:32] = _both16(1)
    record[32] = len(identifier)
    record[33:33 + len(identifier)] = identifier
    return record


def _directory(records):
    data = bytearray()
    for record in records:
        remaining = SECTOR_SIZE - len(data) % SECTOR_SIZE
        if len(record) > remaining:
            data.extend(bytes(remaining))
        data.extend(record)
    data.extend(bytes((-len(data)) % SECTOR_SIZE))
    return data


def build_iso(boot_floppy, files, volume_id='DOSCRAFT'):
    """Return a new ISO, sorting files so mapping insertion order is immaterial.

    Filenames accept ASCII lowercase and are normalized to uppercase ISO9660
    identifiers with version ;1. Case-insensitive duplicates, paths and names
    outside letters/digits/underscore 8.3 are rejected. Payloads accept bytes,
    bytearray or memoryview. Invalid types raise TypeError; invalid values raise
    ValueError. All recorded dates are 2026-10-09 00:00:00 UTC.

    A 55 AA signature is required at floppy offsets 510-511. This verifies media
    shape, not its DOS boot code or filesystem. The x86 catalog selects 1.44-MiB
    floppy emulation, default load segment 07C0 and one initial 512-byte sector.
    """
    boot = _bytes(boot_floppy, 'Boot floppy')
    if len(boot) != FLOPPY_SIZE:
        raise ValueError('Boot floppy must be exactly 1.44 MiB (1474560 bytes)')
    if boot[510:512] != b'\x55\xaa':
        raise ValueError('Boot floppy is missing its 55 AA boot signature')
    if not isinstance(files, Mapping):
        raise TypeError('Files must be a filename-to-bytes mapping')
    if not isinstance(volume_id, str):
        raise TypeError('Volume ID must be a string')
    if (not 1 <= len(volume_id) <= 32
            or any(c not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_' for c in volume_id)):
        raise ValueError('Volume ID must be 1-32 uppercase ISO9660 characters')

    payloads = {}
    for name, contents in files.items():
        identifier = _identifier(name)
        if identifier in payloads:
            raise ValueError('Duplicate filename after uppercase normalization')
        payloads[identifier] = _bytes(contents, 'File payload')
    # ISO9660 orders the filename and extension separately. In particular,
    # A.;1 precedes A.BIN;1 despite the semicolon's ASCII sort position.
    ordered = sorted(payloads.items(), key=lambda item: item[0][:-2].split(b'.'))

    # Descriptors 16-18, one little/big-endian root path table each at 19/20.
    root_lba = 21
    placeholders = [_record(b'\x00', 0, 0, True), _record(b'\x01', 0, 0, True)]
    placeholders.extend(_record(name, 0, 0) for name, _ in ordered)
    root_size = len(_directory(placeholders))
    catalog_lba = root_lba + root_size // SECTOR_SIZE
    boot_lba = catalog_lba + 1
    next_lba = boot_lba + FLOPPY_SIZE // SECTOR_SIZE
    entries = []
    for name, content in ordered:
        entries.append((name, next_lba, content))
        # Reserve a valid extent even for empty files.
        next_lba += max(1, (len(content) + SECTOR_SIZE - 1) // SECTOR_SIZE)
    if root_size > _UINT32_MAX or next_lba > _UINT32_MAX:
        raise ValueError('Image exceeds ISO9660 volume limits')

    image = bytearray(next_lba * SECTOR_SIZE)

    def put(lba, data):
        at = lba * SECTOR_SIZE
        image[at:at + len(data)] = data

    root_record = _record(b'\x00', root_lba, root_size, True)
    pvd = bytearray(SECTOR_SIZE)
    pvd[:7] = b'\x01CD001\x01'
    pvd[8:40] = b'DOSCRAFT'.ljust(32, b' ')
    pvd[40:72] = volume_id.encode('ascii').ljust(32, b' ')
    pvd[80:88] = _both32(next_lba)
    pvd[120:124] = _both16(1)  # volume set size
    pvd[124:128] = _both16(1)  # volume sequence number
    pvd[128:132] = _both16(SECTOR_SIZE)
    pvd[132:140] = _both32(10)  # root path table length
    struct.pack_into('<I', pvd, 140, 19)
    struct.pack_into('>I', pvd, 148, 20)
    pvd[156:190] = root_record
    pvd[190:813] = b' ' * (813 - 190)  # textual identifiers/file references
    pvd[813:830] = _VOLUME_DATE
    pvd[830:847] = _VOLUME_DATE
    pvd[847:864] = b'0' * 16 + b'\x00'  # unspecified expiration
    pvd[864:881] = _VOLUME_DATE
    pvd[881] = 1
    put(16, pvd)

    boot_record = bytearray(SECTOR_SIZE)
    boot_record[:7] = b'\x00CD001\x01'
    boot_record[7:39] = b'EL TORITO SPECIFICATION'.ljust(32, b'\x00')
    struct.pack_into('<I', boot_record, 71, catalog_lba)
    put(17, boot_record)
    put(18, b'\xffCD001\x01' + bytes(SECTOR_SIZE - 7))
    put(19, b'\x01\x00' + struct.pack('<IH', root_lba, 1) + b'\x00\x00')
    put(20, b'\x01\x00' + struct.pack('>IH', root_lba, 1) + b'\x00\x00')
    records = [root_record, _record(b'\x01', root_lba, root_size, True)]
    records.extend(_record(name, extent, len(content)) for name, extent, content in entries)
    put(root_lba, _directory(records))

    catalog = bytearray(SECTOR_SIZE)
    catalog[0] = 1  # validation header, platform 0 = x86
    catalog[4:28] = b'DOSCRAFT'.ljust(24, b'\x00')
    catalog[30:32] = b'\x55\xaa'
    checksum = (-sum(struct.unpack('<16H', catalog[:32]))) & 0xffff
    struct.pack_into('<H', catalog, 28, checksum)
    catalog[32:34] = b'\x88\x02'  # bootable, 1.44-MiB floppy emulation
    struct.pack_into('<H', catalog, 38, 1)
    struct.pack_into('<I', catalog, 40, boot_lba)
    put(catalog_lba, catalog)
    put(boot_lba, boot)
    for _, extent, content in entries:
        put(extent, content)
    return bytes(image)
