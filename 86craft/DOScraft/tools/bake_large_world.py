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


@lru_cache(maxsize=32)
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


COBBLE, LOG, LEAVES = 4, 6, 7
CLAY, COPPER = 33, 34          # 6-bit block IDs (tools/dos_early_game.py)
TIN_BIT = 64                   # Gravel metadata bit 0: tin country
HIGHLAND = SEA + 6
TIN_CLEAR = 96                 # No tin country this close to spawn


def box_min(heights, radius):
    """Lowest height within a (2r+1)-square of every column."""
    rows = bytearray(WIDTH * WIDTH)
    for z in range(WIDTH):
        row = heights[z * WIDTH:(z + 1) * WIDTH]
        for x in range(WIDTH):
            rows[z * WIDTH + x] = min(row[max(0, x - radius):x + radius + 1])
    out = bytearray(WIDTH * WIDTH)
    for x in range(WIDTH):
        column = rows[x::WIDTH]
        for z in range(WIDTH):
            out[z * WIDTH + x] = min(column[max(0, z - radius):z + radius + 1])
    return out


def early_materials(image, heights, biomes, seed, spawn):
    """Overlay the early-game materials (EARLY_GAME.md, Where materials
    generate) after all other generation, so terrain, trees, caves and the
    other ores stay exactly as before. Only listed blocks are replaced."""
    from terrain_biomes import FOREST, DESERT
    near_water = box_min(heights, 3)
    sx, sz = spawn
    counts = dict(clay=0, gravel=0, tin_gravel=0, cobble=0, copper=0, boulders=0)

    def put(at, block, name):
        image[at] = block
        counts[name] += 1

    for z in range(1, WIDTH - 1):
        for x in range(1, WIDTH - 1):
            h = heights[z * WIDTH + x]
            col = base_offset(x, z)
            top = col + h - 1
            biome = biomes[z * WIDTH + x] if biomes is not None else None
            forest = biome == FOREST if biomes is not None else noise(x, z, 5, seed + 5) > 150
            slope = max(abs(h - heights[(z + dz) * WIDTH + x + dx]) for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if h < SEA:
                # Lake and river beds: more gravel, and clay settled in still water.
                if image[top] in (SAND, GRAVEL):
                    n = noise(x, z, 3, seed + 50)
                    if n > 160:
                        put(top, GRAVEL, 'gravel')
                    elif n < 75:
                        put(top, CLAY, 'clay')
                        if image[top - 1] == SAND:
                            put(top - 1, CLAY, 'clay')
            elif h <= SEA + 1:
                # Shores: cobble beds and gravel bars.
                if image[top] == SAND:
                    n = noise(x, z, 3, seed + 52)
                    if n > 175:
                        put(top, COBBLE, 'cobble')
                    elif n < 50:
                        put(top, GRAVEL, 'gravel')
            else:
                # Banks: a clay layer under the soil near water.
                if h <= SEA + 6 and near_water[z * WIDTH + x] < SEA and noise(x, z, 3, seed + 53) > 105:
                    for y in (h - 3, h - 4):
                        if image[col + y] in (DIRT, STONE):
                            put(col + y, CLAY, 'clay')
                # Forest subsoil lenses.
                if forest and noise(x, z, 3, seed + 54) > 200:
                    for y in (h - 3, h - 4):
                        if image[col + y] in (DIRT, STONE):
                            put(col + y, CLAY, 'clay')
                # Rare dry lakebeds in the desert.
                if biome == DESERT and image[top] == SAND and noise(x, z, 4, seed + 55) > 215:
                    put(top, CLAY, 'clay')
                above = image[top + 1]
                # Talus on steep slopes, with copper showing in the highlands.
                if slope >= 3 and image[top] in (GRASS, STONE, DIRT, GRAVEL) and above == AIR:
                    if h >= HIGHLAND and noise(x, z, 2, seed + 57) > 220:
                        put(top, COPPER, 'copper')
                    elif noise(x, z, 2, seed + 56) > 105:
                        put(top, COBBLE, 'cobble')
                # A few loose boulders on open ground.
                elif image[top] == GRASS and above == AIR and hash3(x, z, 91, seed) % 1000 < 3:
                    put(top + 1, COBBLE, 'boulders')
                    if hash3(x, z, 92, seed) & 1 and image[top + 2] == AIR:
                        put(top + 2, COBBLE, 'boulders')
                # Shallow copper veins under the highlands.
                if h >= HIGHLAND:
                    for y in range(max(3, h - 10), h - 2):
                        if image[col + y] == STONE and hash3(x, y, z, seed ^ 0xC0) % 70 == 0:
                            put(col + y, COPPER, 'copper')
                            for dx, dy, dz in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
                                at = base_offset(x + dx, z + dz, y + dy)
                                if image[at] == STONE and hash3(x + dx, y + dy, z + dz, seed ^ 0xC1) & 1:
                                    put(at, COPPER, 'copper')
            # Tin country: a few regions' gravel carries tin, never near spawn
            # (finding it is the bronze-age search).
            if max(abs(x - sx), abs(z - sz)) >= TIN_CLEAR and noise(x, z, 7, seed + 60) > 180:
                for y in range(1, min(WH, h + 1)):
                    if image[col + y] == GRAVEL:
                        image[col + y] = GRAVEL | TIN_BIT
                        counts['tin_gravel'] += 1
    return counts


def bake(seed, *, style='legacy', report=None, early=True):
    if style not in ('legacy','biomes'): raise ValueError('Unknown terrain style')
    from terrain_biomes import sample, surface, vegetation, NAMES
    image = bytearray(IMAGE_BYTES)
    heights = bytearray(WIDTH * WIDTH)
    biomes = bytearray(WIDTH * WIDTH) if style=='biomes' else None
    if biomes is not None:
        for z in range(WIDTH):
            for x in range(WIDTH):
                at=z*WIDTH+x
                heights[at],biomes[at]=sample(x,z,seed,noise,WIDTH,SEA,WH)
        print('CLIMATE_AND_HEIGHTS=READY',flush=True)
    for cz in range(WIDTH // CS):
        for cx in range(WIDTH // CS):
            for lz in range(CS):
                z = cz * CS + lz
                for lx in range(CS):
                    x = cx * CS + lx
                    h = heights[z*WIDTH+x] if biomes is not None else height(x, z, seed)
                    heights[z * WIDTH + x] = h
                    beach = h <= SEA + 1
                    top = (GRAVEL if h < SEA - 2 and noise(x, z, 3, seed + 9) > 170
                           else SAND) if beach else GRASS
                    if biomes is not None:
                        slope=max(abs(h-heights[max(0,z-1)*WIDTH+x]),
                            abs(h-heights[min(WIDTH-1,z+1)*WIDTH+x]),
                            abs(h-heights[z*WIDTH+max(0,x-1)]),
                            abs(h-heights[z*WIDTH+min(WIDTH-1,x+1)]))
                        top=surface(h,biomes[z*WIDTH+x],slope,noise(x,z,4,seed+36),SEA)
                    start = base_offset(x, z)
                    image[start] = BEDROCK
                    image[start + 1:start + max(1, h - 4)] = bytes([STONE]) * max(0, h - 5)
                    subsoil=(SAND if beach else DIRT) if biomes is None else (
                        SAND if beach or top==SAND else STONE if top in (STONE,GRAVEL) else DIRT)
                    image[start + max(1, h - 4):start + h - 1] = bytes([subsoil]) * (h - 1 - max(1, h - 4))
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
            trees,flowers,plants=(vegetation(biomes[z*WIDTH+x]) if biomes is not None
                else (45 if noise(x,z,5,seed+5)>150 else 8,3,12))
            if biomes is not None and image[base_offset(x,z,h-1)]!=GRASS:
                continue
            if seed_value % 1000 < trees:
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
                    if seed_value % 100 < flowers:
                        image[at] = FLOWER
                    elif seed_value % 100 < plants:
                        image[at] = TALLGRASS

    spawn_x = spawn_z = WIDTH // 2
    if biomes is not None:
        def safe_spawn(x,z):
            if not (2<=x<WIDTH-2 and 2<=z<WIDTH-2): return False
            h=heights[z*WIDTH+x]
            if h<=SEA+1 or biomes[z*WIDTH+x] not in (0,1): return False
            if image[base_offset(x,z,h-1)]!=GRASS: return False
            if image[base_offset(x,z,h)]!=AIR or image[base_offset(x,z,h+1)]!=AIR: return False
            return all(abs(h-heights[(z+dz)*WIDTH+x+dx])<=1
                for dx,dz in ((1,0),(-1,0),(0,1),(0,-1)))
        found=False
        for radius in range(WIDTH//2):
            for offset in range(-radius,radius+1):
                for x,z in ((WIDTH//2+offset,WIDTH//2-radius),(WIDTH//2+offset,WIDTH//2+radius),
                            (WIDTH//2-radius,WIDTH//2+offset),(WIDTH//2+radius,WIDTH//2+offset)):
                    if safe_spawn(x,z):
                        spawn_x,spawn_z=x,z; found=True; break
                if found: break
            if found: break
        if not found: raise ValueError('Biome world has no safe grassy spawn')
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
    materials = early_materials(image, heights, biomes, seed, (spawn_x, spawn_z)) if early else None

    sums = bytearray(COLUMNS * 4)
    for column in range(COLUMNS):
        start = (BASE_SECTOR + column * SECTORS * 3) * 512
        struct.pack_into('<I', sums, column * 4, checksum(image[start:start + CELLS]))
    image[:16] = MAGIC
    struct.pack_into('<9I', image, 16, 1, WIDTH, COLUMNS, SECTORS,
                     BASE_SECTOR, seed, spawn_x, spawn_z, checksum(sums))
    image[512:512 + len(sums)] = sums
    if report is not None:
        report.update(seed=seed,style=style,spawn=[spawn_x,spawn_z],cave_entrances=entrances,early_materials=materials,
            height_min=min(heights),height_max=max(heights))
        if biomes is not None:
            report['biome_cells']={name:biomes.count(i) for i,name in enumerate(NAMES)}
            report['land_cells']={name:sum(h>SEA+1 and b==i for h,b in zip(heights,biomes))
                for i,name in enumerate(NAMES)}
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=int, default=4242)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--style',choices=('legacy','biomes'),default='legacy')
    parser.add_argument('--report',type=Path)
    parser.add_argument('--no-early',action='store_true',help='Bake the world as it was before the early-game materials')
    args = parser.parse_args()
    report={}
    with args.output.open('xb') as file:
        file.write(bake(args.seed,style=args.style,report=report,early=not args.no_early))
    if args.report:
        import json
        with args.report.open('x') as file: file.write(json.dumps(report,indent=2)+'\n')
    print(f'{args.output}: {IMAGE_BYTES} bytes, seed {args.seed}')


if __name__ == '__main__':
    main()
