"""Convert smoke snapshots (C:\\*.RAW: 768-byte VGA palette + 320x240 indices)
from a private large-world HDD into PNG files for inspection."""
import argparse
from pathlib import Path
import struct
import zlib

from game_media import large_volume


def png(path, width, height, rgb_rows, scale=2):
    raw = b''.join(b'\0' + bytes(c for x in row for c in x * scale) * 1
                   for row in rgb_rows for _ in range(scale))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack(
        '>IIBBBBB', width * scale, height * scale, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6))
        + chunk(b'IEND', b''))


def convert(data, out):
    palette = data[:768]
    pixels = data[768:768 + 320 * 240]
    rows = [[tuple(palette[i * 3:i * 3 + 3]) for i in pixels[y * 320:(y + 1) * 320]] for y in range(240)]
    png(out, 320, 240, [[c for c in row] for row in rows])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disk', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('names', nargs='*')
    args = parser.parse_args()
    volume = large_volume(args.disk.read_bytes())
    args.output.mkdir(parents=True, exist_ok=True)
    names = args.names or [e[0] for e in volume.directory() if e[0].endswith('.RAW')]
    for name in names:
        convert(volume.read(name), args.output / (Path(name).stem + '.png'))
        print(args.output / (Path(name).stem + '.png'))


if __name__ == '__main__':
    main()
