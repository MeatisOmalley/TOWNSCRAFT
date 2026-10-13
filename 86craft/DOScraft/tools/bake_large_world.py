"""Bake one deterministic 1024-block terrain seed into the DOS HDD layout.

This writes a new file exclusively. The first slot of every column is an
immutable raw base; the other two slots are reserved for guest edits.
"""

import argparse
from functools import lru_cache
from math import cos, isqrt, pi, sin
from pathlib import Path
import struct

WIDTH = 1024
CS = 16
WH = 48
SEA = 20
COLUMNS = (WIDTH // CS) ** 2
CELLS = CS * CS * WH
SECTORS = CELLS // 512
BASE_SECTOR = 33
IMAGE_BYTES = (BASE_SECTOR + COLUMNS * SECTORS * 3) * 512
MAGIC = b'DOSCRAFT-1024-V1'
assert len(MAGIC) == 16

AIR, STONE, GRASS, DIRT, LOG, LEAVES = 0, 1, 2, 3, 6, 7
SAND, WATER, BEDROCK, COAL, IRON, GRAVEL = 8, 9, 11, 12, 13, 22
FLOWER, TALLGRASS, GOLD, DIAMOND = 23, 24, 28, 29
CAVE_TOP = 14


def hash3(x, y, z, seed):
    mask = 0xffffffff
    h = seed ^ (x * 0x27d4eb2d & mask)
    h = ((h ^ (h >> 15)) * 0x85ebca6b) & mask
    h ^= y * 0x165667b1 & mask
    h = ((h ^ (h >> 13)) * 0xc2b2ae35) & mask
    h ^= z * 0x9e3779b1 & mask
    h = ((h ^ (h >> 16)) * 0x85ebca6b) & mask
    return h ^ (h >> 13)


@lru_cache(maxsize=8)
def lattice(shift, seed):
    side = (WIDTH >> shift) + 2
    return side, bytes(hash3(x, z, 0, seed) & 255
                       for z in range(side) for x in range(side))


def noise(x, z, shift, seed):
    side, values = lattice(shift, seed)
    cx, cz = x >> shift, z >> shift
    fx = (x & ((1 << shift) - 1)) << (8 - shift)
    fz = (z & ((1 << shift) - 1)) << (8 - shift)
    fx = fx * fx * (768 - 2 * fx) >> 16
    fz = fz * fz * (768 - 2 * fz) >> 16
    at = cz * side + cx
    a, b, c, d = values[at], values[at + 1], values[at + side], values[at + side + 1]
    ab = a + ((b - a) * fx >> 8)
    cd = c + ((d - c) * fx >> 8)
    return ab + ((cd - ab) * fz >> 8)


def height(x, z, seed):
    n = (noise(x, z, 5, seed) * 5 + noise(x, z, 4, seed + 1) * 3 +
         noise(x, z, 3, seed + 2) * 2) // 10
    m = noise(x, z, 6, seed + 3)
    h = SEA - 2 + (n - 100) // 7
    if m > 150:
        h += (m - 150) // 5
    dx, dz = x - WIDTH // 2, z - WIDTH // 2
    distance = isqrt((dx * dx + dz * dz) * 65536 // ((WIDTH // 2) ** 2))
    if distance > 150:
        h -= (distance - 150) // 8
    return max(4, min(WH - 10, h))


def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def base_offset(x, z, y=0):
    column = (z // CS) * (WIDTH // CS) + x // CS
    cell = (z % CS) * CS + x % CS
    return (BASE_SECTOR + column * SECTORS * 3) * 512 + cell * WH + y


class CaveRng:
    def __init__(self, seed):
        self.state = seed ^ 0x5EEDCAFE

    def next(self, limit):
        self.state = (self.state * 1103515245 + 12345) & 0xffffffff
        return (self.state >> 16) % limit


def carve_underground(image, heights, spawn_x, spawn_z, seed):
    """Bake the original game's bottom-layer worms, entrances, and deep ore."""
    rng = CaveRng(seed)
    min_height = bytearray(WIDTH * WIDTH)
    for z in range(1, WIDTH - 1):
        for x in range(1, WIDTH - 1):
            min_height[z * WIDTH + x] = min(
                heights[(z + dz) * WIDTH + x + dx]
                for dz in (-1, 0, 1) for dx in (-1, 0, 1))

    sine = [round(sin(i * 2 * pi / 1024) * 16384) for i in range(1024)]
    cosine = [round(cos(i * 2 * pi / 1024) * 16384) for i in range(1024)]

    def carve(x, y, z):
        if not (1 <= x < WIDTH - 1 and 1 <= z < WIDTH - 1 and 2 <= y <= CAVE_TOP):
            return
        if min_height[z * WIDTH + x] < y + 5:
            return
        at = base_offset(x, z, y)
        if image[at] in (STONE, DIRT, GRAVEL, COAL, IRON):
            image[at] = AIR

    for _ in range(WIDTH * WIDTH // 350):
        px, pz = rng.next(WIDTH) * 16, rng.next(WIDTH) * 16
        py = (3 + rng.next(CAVE_TOP - 4)) * 16
        yaw, length, radius = rng.next(1024), 40 + rng.next(90), 20 + rng.next(12)
        for _ in range(length):
            px += (sine[yaw & 1023] * 16) >> 14
            pz += (cosine[yaw & 1023] * 16) >> 14
            py = max(3 * 16, min((CAVE_TOP - 1) * 16, py + (rng.next(3) - 1) * 5))
            yaw += rng.next(81) - 40
            if rng.next(8) == 0:
                radius = max(18, min(44, radius + rng.next(13) - 6))
            blocks = (radius + 15) // 16
            for y in range(py // 16 - blocks, py // 16 + blocks + 1):
                dy2 = (y * 16 + 8 - py) ** 2
                if dy2 > radius * radius:
                    continue
                for z in range(pz // 16 - blocks, pz // 16 + blocks + 1):
                    dz2 = (z * 16 + 8 - pz) ** 2
                    if dy2 + dz2 > radius * radius:
                        continue
                    for x in range(px // 16 - blocks, px // 16 + blocks + 1):
                        if (x * 16 + 8 - px) ** 2 + dy2 + dz2 <= radius * radius:
                            carve(x, y, z)

    def entrance(x, z, h, dx, dz, end_y, length, write):
        for step in range(length + 1):
            sx, sz = x + dx * step, z + dz * step
            sy = max(end_y, h - 1 - step)
            if not write and (min_height[sz * WIDTH + sx] <= SEA + 1 or
                              abs(sx - spawn_x) < 4 and abs(sz - spawn_z) < 4):
                return False
            for zz in range(sz - 1, sz + 2):
                for xx in range(sx - 1, sx + 2):
                    for yy in range(sy, sy + 3):
                        at = base_offset(xx, zz, yy)
                        if write:
                            image[at] = AIR
                        elif image[at] in (WATER, LOG, BEDROCK):
                            return False
        return True

    wanted = max(2, WIDTH * WIDTH // 4000)
    count, tries = 0, wanted * 120
    while count < wanted and tries > 0:
        tries -= 1
        x, z = 3 + rng.next(WIDTH - 6), 3 + rng.next(WIDTH - 6)
        if count == 0 and tries > wanted * 60:
            x = max(3, min(WIDTH - 4, spawn_x + rng.next(49) - 24))
            z = max(3, min(WIDTH - 4, spawn_z + rng.next(49) - 24))
        if abs(x - spawn_x) < 6 and abs(z - spawn_z) < 6:
            continue
        h = heights[z * WIDTH + x]
        if h <= SEA + 2 or image[base_offset(x, z, h - 1)] != GRASS:
            continue
        for _ in range(12):
            heading, length = rng.next(4), 18 + rng.next(18)
            dx, dz = int(heading == 0) - int(heading == 1), int(heading == 2) - int(heading == 3)
            ex, ez = x + dx * length, z + dz * length
            if not (3 <= ex < WIDTH - 3 and 3 <= ez < WIDTH - 3):
                continue
            for end_y in range(CAVE_TOP - 2, 2, -1):
                at = base_offset(ex, ez, end_y)
                if image[at] == AIR and image[at + 1] == AIR and image[at - 1] not in (AIR, WATER):
                    break
            else:
                continue
            if length < h - 1 - end_y or not entrance(x, z, h, dx, dz, end_y, length, False):
                continue
            entrance(x, z, h, dx, dz, end_y, length, True)
            count += 1
            break

    def deep_ore(ore, max_y, size):
        x, z, y = rng.next(WIDTH), rng.next(WIDTH), 2 + rng.next(max_y - 1)
        for _ in range(size):
            if 0 <= x < WIDTH and 0 <= z < WIDTH and 0 <= y < WH:
                at = base_offset(x, z, y)
                if image[at] == STONE:
                    image[at] = ore
            direction = rng.next(6)
            x += int(direction == 0) - int(direction == 1)
            y += int(direction == 2) - int(direction == 3)
            z += int(direction == 4) - int(direction == 5)

    for _ in range(WIDTH * WIDTH // 200):
        deep_ore(IRON, CAVE_TOP, 4 + rng.next(4))
    for _ in range(WIDTH * WIDTH // 320):
        deep_ore(GOLD, 12, 3 + rng.next(4))
    for _ in range(WIDTH * WIDTH // 700):
        deep_ore(DIAMOND, 8, 2 + rng.next(4))
    return count


def bake(seed):
    image = bytearray(IMAGE_BYTES)
    heights = bytearray(WIDTH * WIDTH)
    for cz in range(WIDTH // CS):
        for cx in range(WIDTH // CS):
            for lz in range(CS):
                z = cz * CS + lz
                for lx in range(CS):
                    x = cx * CS + lx
                    h = height(x, z, seed)
                    heights[z * WIDTH + x] = h
                    beach = h <= SEA + 1
                    top = (GRAVEL if h < SEA - 2 and noise(x, z, 3, seed + 9) > 170
                           else SAND) if beach else GRASS
                    start = base_offset(x, z)
                    image[start] = BEDROCK
                    image[start + 1:start + max(1, h - 4)] = bytes([STONE]) * max(0, h - 5)
                    image[start + max(1, h - 4):start + h - 1] = bytes([SAND if beach else DIRT]) * (h - 1 - max(1, h - 4))
                    image[start + h - 1] = top
                    if h < SEA:
                        image[start + h:start + SEA] = bytes([WATER]) * (SEA - h)
                    image[start + 1] = BEDROCK if hash3(x, 1, z, seed) & 3 == 0 else image[start + 1]
                    image[start + 2] = BEDROCK if hash3(x, 2, z, seed) & 7 == 0 else image[start + 2]
                    for y in range(3, h - 5):
                        rare = hash3(x, y, z, seed ^ 0x0E5)
                        if rare % 127 == 0:
                            image[start + y] = IRON if y < SEA - 4 and rare & 1 else COAL

    # Trees and plants are baked once; travel never regenerates terrain.
    for z in range(3, WIDTH - 3):
        for x in range(3, WIDTH - 3):
            h = heights[z * WIDTH + x]
            if h <= SEA + 1 or h + 8 >= WH:
                continue
            seed_value = hash3(x, z, 77, seed)
            if seed_value % 1000 < (45 if noise(x, z, 5, seed + 5) > 150 else 8):
                trunk = 4 + hash3(x, z, 5, seed) % 3
                for dy in range(trunk - 2, trunk + 2):
                    radius = 1 if dy >= trunk else 2
                    for dz in range(-radius, radius + 1):
                        for dx in range(-radius, radius + 1):
                            tx, tz = x + dx, z + dz
                            at = base_offset(tx, tz, h + dy)
                            if image[at] == AIR:
                                image[at] = LEAVES
                for dy in range(trunk):
                    image[base_offset(x, z, h + dy)] = LOG
                image[base_offset(x, z, h - 1)] = DIRT
            else:
                at = base_offset(x, z, h)
                if image[at] == AIR:
                    if seed_value % 100 < 3:
                        image[at] = FLOWER
                    elif seed_value % 100 < 12:
                        image[at] = TALLGRASS

    spawn_x = spawn_z = WIDTH // 2
    if heights[spawn_z * WIDTH + spawn_x] <= SEA + 1:
        for radius in range(1, WIDTH // 2):
            found = False
            for z in range(max(1, spawn_z - radius), min(WIDTH - 1, spawn_z + radius + 1)):
                for x in (spawn_x - radius, spawn_x + radius):
                    if 1 <= x < WIDTH - 1 and heights[z * WIDTH + x] > SEA + 1:
                        spawn_x, spawn_z = x, z
                        found = True
                        break
                if found:
                    break
            if found:
                break

    entrances = carve_underground(image, heights, spawn_x, spawn_z, seed)
    if entrances < 2:
        raise ValueError('Prebaked world has fewer than two cave entrances')

    sums = bytearray(COLUMNS * 4)
    for column in range(COLUMNS):
        start = (BASE_SECTOR + column * SECTORS * 3) * 512
        struct.pack_into('<I', sums, column * 4, checksum(image[start:start + CELLS]))
    image[:16] = MAGIC
    struct.pack_into('<9I', image, 16, 1, WIDTH, COLUMNS, SECTORS,
                     BASE_SECTOR, seed, spawn_x, spawn_z, checksum(sums))
    image[512:512 + len(sums)] = sums
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=int, default=4242)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    with args.output.open('xb') as file:
        file.write(bake(args.seed))
    print(f'{args.output}: {IMAGE_BYTES} bytes, seed {args.seed}')


if __name__ == '__main__':
    main()
