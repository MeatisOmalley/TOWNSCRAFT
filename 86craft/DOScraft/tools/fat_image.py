"""Small, deliberately limited FAT12/16 image reader and development-media writer.

No host drives, mounts, formatting commands, or existing image mutation. Writers
return bytes; callers must create new files exclusively. Only root 8.3 files are
supported by the writer. The reader also handles ordinary subdirectories.
"""
import struct


class FatImage:
    def __init__(self, data, offset=0):
        self.data = data
        self.base = offset
        b = data[offset:offset + 512]
        if len(b) != 512 or b[510:512] != b'\x55\xaa':
            raise ValueError('Missing boot signature')
        self.sector, self.spc, self.reserved, self.copies, self.entries = struct.unpack_from('<HBHBH', b, 11)
        total16, self.media, self.fat_sectors = struct.unpack_from('<HBH', b, 19)
        self.total = total16 or struct.unpack_from('<I', b, 32)[0]
        if self.sector != 512 or self.spc not in (1, 2, 4, 8, 16, 32, 64, 128):
            raise ValueError('Unsupported sector/cluster size')
        if not self.reserved or not self.copies or not self.fat_sectors or not self.entries:
            raise ValueError('Not FAT12/16')
        self.root_sector = self.reserved + self.copies * self.fat_sectors
        self.root_sectors = (self.entries * 32 + 511) // 512
        self.data_sector = self.root_sector + self.root_sectors
        self.clusters = (self.total - self.data_sector) // self.spc
        if not 0 < self.clusters < 65525 or offset + self.total * 512 > len(data):
            raise ValueError('Truncated or unsupported image')
        self.bits = 12 if self.clusters < 4085 else 16
        self.fat = data[offset + self.reserved * 512:offset + (self.reserved + self.fat_sectors) * 512]

    def chain(self, start):
        seen = set()
        cluster = start
        while cluster < (0xff8 if self.bits == 12 else 0xfff8):
            if cluster < 2 or cluster >= self.clusters + 2 or cluster in seen:
                raise ValueError('Invalid or cyclic FAT chain')
            seen.add(cluster)
            yield cluster
            at = cluster * 3 // 2 if self.bits == 12 else cluster * 2
            if at + 2 > len(self.fat):
                raise ValueError('FAT entry out of bounds')
            word = struct.unpack_from('<H', self.fat, at)[0]
            cluster = ((word >> 4) if cluster & 1 else word & 0xfff) if self.bits == 12 else word

    def cluster_bytes(self, start):
        chunks = []
        for cluster in self.chain(start):
            at = self.base + (self.data_sector + (cluster - 2) * self.spc) * 512
            chunks.append(self.data[at:at + self.spc * 512])
        return b''.join(chunks)

    def directory(self, cluster=0):
        if cluster:
            data = self.cluster_bytes(cluster)
        else:
            at = self.base + self.root_sector * 512
            data = self.data[at:at + self.entries * 32]
        result = []
        for at in range(0, len(data), 32):
            e = data[at:at + 32]
            if not e[0]:
                break
            if e[0] == 0xe5 or e[11] & 0x08:  # deleted, volume labels, LFN
                continue
            stem, ext = e[:8].decode('ascii').rstrip(), e[8:11].decode('ascii').rstrip()
            result.append((stem + ('.' + ext if ext else ''), e[11],
                           struct.unpack_from('<H', e, 26)[0], struct.unpack_from('<I', e, 28)[0]))
        return result

    def read(self, path):
        parts = path.replace('\\', '/').strip('/').upper().split('/')
        cluster = 0
        for i, part in enumerate(parts):
            found = next((e for e in self.directory(cluster) if e[0] == part), None)
            if found is None:
                raise FileNotFoundError(path)
            name, attr, cluster, size = found
            if i != len(parts) - 1:
                if not attr & 0x10:
                    raise NotADirectoryError(name)
            else:
                if attr & 0x10:
                    raise IsADirectoryError(name)
                content = self.cluster_bytes(cluster) if size else b''
                if len(content) < size:
                    raise ValueError('File chain shorter than directory size')
                return content[:size]


def short_name(name):
    parts = name.upper().split('.')
    if len(parts) > 2 or not 1 <= len(parts[0]) <= 8 or (len(parts) == 2 and not 1 <= len(parts[1]) <= 3):
        raise ValueError('Only 8.3 filenames supported')
    if any(c not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for p in parts for c in p):
        raise ValueError('Unsafe short filename')
    return (parts[0].ljust(8) + (parts[1] if len(parts) == 2 else '').ljust(3)).encode('ascii')


def populate(boot, files):
    """Create a fresh filesystem using a caller-supplied BPB/boot sector."""
    total = struct.unpack_from('<H', boot, 19)[0] or struct.unpack_from('<I', boot, 32)[0]
    data = bytearray(total * 512)
    data[:512] = boot
    fs = FatImage(data)
    fat = bytearray(fs.fat_sectors * 512)
    fat[:3 if fs.bits == 12 else 4] = bytes([fs.media, 255, 255] + ([] if fs.bits == 12 else [255]))

    def entry(cluster, value):
        at = cluster * 3 // 2 if fs.bits == 12 else cluster * 2
        if fs.bits == 12:
            old = struct.unpack_from('<H', fat, at)[0]
            word = (old & 0x000f) | (value << 4) if cluster & 1 else (old & 0xf000) | value
        else:
            word = value
        struct.pack_into('<H', fat, at, word)

    next_cluster = 2
    if len(files) > fs.entries:
        raise ValueError('Root directory full')
    names = set()
    for i, (name, contents) in enumerate(files):
        encoded = short_name(name)
        if encoded in names:
            raise ValueError('Duplicate filename')
        names.add(encoded)
        count = (len(contents) + fs.spc * 512 - 1) // (fs.spc * 512)
        if next_cluster + count > fs.clusters + 2:
            raise ValueError('Filesystem full')
        first = next_cluster if count else 0
        for j in range(count):
            entry(next_cluster, (0xfff if fs.bits == 12 else 0xffff) if j == count - 1 else next_cluster + 1)
            next_cluster += 1
        if count:
            at = (fs.data_sector + (first - 2) * fs.spc) * 512
            data[at:at + len(contents)] = contents
        at = fs.root_sector * 512 + i * 32
        data[at:at + 11] = encoded
        data[at + 11] = 0x20
        struct.pack_into('<HI', data, at + 26, first, len(contents))
    for i in range(fs.copies):
        at = (fs.reserved + i * fs.fat_sectors) * 512
        data[at:at + len(fat)] = fat
    return data


def scratch_hdd(cylinders=615, heads=4, sectors=17):
    """Fresh 20 MiB development disk. Not a claim of final Wren-V geometry.

    A nonbootable MBR and single FAT16 partition: boot DOS from A:, test C:.
    Final large-world disk sizing/timing belongs to the next storage milestone.
    """
    total = cylinders * heads * sectors
    start = sectors
    size = total - start
    spc = 4
    fat_sectors = 1
    while True:
        count = (size - 1 - 2 * fat_sectors - 32) // spc
        required = ((count + 2) * 2 + 511) // 512
        if required == fat_sectors:
            break
        fat_sectors = required
    b = bytearray(512)
    b[:11] = b'\xeb\xfe\x90DOSCRAFT'
    struct.pack_into('<HBHBHHBH', b, 11, 512, spc, 1, 2, 512, size if size < 65536 else 0, 0xf8, fat_sectors)
    struct.pack_into('<HHII', b, 24, sectors, heads, start, size if size >= 65536 else 0)
    b[36:39] = b'\x80\x00\x29'
    struct.pack_into('<I', b, 39, 0x198933)
    b[43:62] = b'DOSCRAFTDEVFAT16   '
    b[510:512] = b'\x55\xaa'
    volume = populate(b, [])
    disk = bytearray(total * 512)
    # CHS: first partition starts cylinder 0, head 1, sector 1.
    disk[446:454] = bytes([0, 1, 1, 0, 6, heads - 1,
                           sectors | (((cylinders - 1) >> 2) & 0xc0), (cylinders - 1) & 255])
    struct.pack_into('<II', disk, 454, start, size)
    disk[510:512] = b'\x55\xaa'
    disk[start * 512:] = volume
    return disk
