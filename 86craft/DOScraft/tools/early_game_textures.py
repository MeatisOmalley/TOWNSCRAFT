"""Generate the early-game tiles offline (assets/textures/early_game.inc).

Sources are the local Eaglercraft resource pack, recolours of it (bronze,
tin, the flint spear) and hand-drawn tiles (stone anvil, clay furnace,
bloomery, bellows) reviewed on 2026-10-15. Every tile is converted to the
game palette with the terracotta ramp, using an explicit set of ramps per
tile so nearest-colour matching never drifts into unrelated hues. The game
copies these tiles at startup in both texture styles.
"""
import argparse
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
from PIL import Image
from compare_eagler_textures import resources
from import_eagler_textures import ROOT, palette_bytes

GRAY, DIRT, GRASS, LEAF, SAND, BARK, PLANK, WATER, SKY, RED, PINK, FLAME, CYAN, BLUE, PEACH, TERRA = range(16)

CLAY = [(0, (112, 52, 36)), (0.5, (140, 66, 46)), (0.85, (158, 80, 56)), (1, (172, 94, 66))]
CLAY_LUMP = [(0, (80, 34, 24)), (0.5, (138, 64, 44)), (0.85, (170, 88, 62)), (1, (188, 108, 80))]
BRICK = [(0, (96, 40, 26)), (0.45, (168, 80, 52)), (0.8, (200, 104, 70)), (1, (222, 132, 96))]
BRONZE = [(0, (52, 32, 16)), (0.35, (122, 80, 36)), (0.7, (184, 136, 62)), (1, (236, 200, 128))]
TIN = [(0, (22, 18, 16)), (0.5, (64, 54, 46)), (0.85, (120, 108, 96)), (1, (196, 190, 182))]
FLINT = [(0, (24, 24, 30)), (0.5, (58, 58, 66)), (0.85, (100, 100, 110)), (1, (160, 160, 170))]
STONE = [(0, (82, 82, 80)), (0.6, (128, 128, 124)), (1, (176, 176, 170))]
BASALT = (112, 112, 116)
FURNACE_BODY = (214, 128, 92)
FURNACE_RIM = (244, 168, 128)
BLOOMERY_BODY = (150, 84, 60)
SOOT = (64, 52, 44)
DARK = (26, 18, 14)


def noise(x, y, seed):
    h = (x * 374761393 + y * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 255) / 255.0


def shade(color, k):
    return tuple(max(0, min(255, round(c * k))) for c in color) + (255,)


def lum(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def gradient(stops, t):
    t = max(0.0, min(1.0, t))
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if t <= b:
            k = (t - a) / (b - a) if b > a else 0
            return tuple(round(x + (y - x) * k) for x, y in zip(ca, cb))
    return stops[-1][1]


def recolor(image, stops, mask=None):
    """Map brightness onto a colour gradient inside the mask (default: all)."""
    out = image.copy()
    px = out.load()
    cells = [(x, y) for y in range(image.height) for x in range(image.width)
             if px[x, y][3] >= 128 and (mask is None or (x, y) in mask)]
    if not cells:
        return out
    values = [lum(px[x, y]) for x, y in cells]
    lo, hi = min(values), max(values)
    for (x, y), v in zip(cells, values):
        px[x, y] = gradient(stops, (v - lo) / (hi - lo) if hi > lo else 0.5) + (255,)
    return out


def differ(a, b):
    """Pixels where two same-shape textures disagree: the tool head."""
    pa, pb = a.load(), b.load()
    return {(x, y) for y in range(a.height) for x in range(a.width)
            if pa[x, y][3] >= 128 and pa[x, y] != pb[x, y]}


class Source:
    def __init__(self, path):
        self.files = resources(path)

    def __call__(self, name):
        path = 'assets/minecraft/textures/' + (name if name.startswith('block/') else 'item/' + name) + '.png'
        image = Image.open(BytesIO(self.files[path])).convert('RGBA')
        return image.crop((0, 0, image.width, image.width)).resize((16, 16), Image.Resampling.NEAREST)

    def sha(self, name):
        path = 'assets/minecraft/textures/' + (name if name.startswith('block/') else 'item/' + name) + '.png'
        return sha256(self.files[path]).hexdigest()


def anvil(face):
    """The chosen second-pass stone anvil: a knapping station."""
    from PIL import Image as I
    img = I.new('RGBA', (16, 16))
    px = img.load()

    def stone_px(x, y, seed, k=1.0):
        return shade(BASALT, k * (0.9 + 0.16 * noise(x, y, seed)))
    if face == 'top':
        for y in range(16):
            for x in range(16):
                edge = min(x, y, 15 - x, 15 - y)
                corner = min(x, 15 - x) + min(y, 15 - y)
                k = 0.62 if edge == 0 or corner <= 2 else 0.8 if edge == 1 else 1.0
                px[x, y] = stone_px(x, y, 21, k)
                if ((x - 6.5) / 5.2) ** 2 + ((y - 8.5) / 4.6) ** 2 < 1:
                    px[x, y] = shade((150, 150, 152), 0.96 + 0.06 * noise(x, y, 22))
        for x, y in ((4, 7), (8, 10), (5, 11), (9, 6)):
            px[x, y] = shade((150, 150, 152), 0.72)
        for x, y in ((5, 8), (6, 8), (7, 8), (5, 9), (6, 9), (7, 9)):
            px[x, y] = (44, 44, 54, 255)
        px[4, 8] = px[4, 9] = (214, 210, 200, 255)
        px[6, 8] = (92, 92, 108, 255)
        px[6, 10] = (30, 30, 36, 255)
        for y in range(2, 7):
            for x in range(9, 15):
                d = ((x - 11.5) / 2.6) ** 2 + ((y - 4.2) / 2.0) ** 2
                if d < 1:
                    k = 1.12 if d < 0.25 and x <= 11 else 0.9 if y >= 5 else 1.0
                    px[x, y] = shade((214, 176, 140), k * (0.96 + 0.06 * noise(x, y, 23)))
        for x in (10, 11, 12, 13):
            px[x, 7] = (46, 44, 42, 255)
        for x, y in ((2, 4), (13, 11), (10, 13), (3, 13), (12, 9)):
            px[x, y] = (58, 58, 70, 255)
        px[11, 13] = (196, 192, 184, 255)
    else:
        for y in range(16):
            for x in range(16):
                side = min(x, 15 - x)
                k = 0.66 if side == 0 else 0.82 if side == 1 else 1.0
                if y == 0:
                    k *= 1.18
                elif y >= 13:
                    k *= 0.78 if y < 15 else 0.6
                px[x, y] = stone_px(x, y, 24, k)
        for x in (3, 4, 9, 12):
            px[x, 1] = shade(BASALT, 1.28)
            px[x, 2] = shade(BASALT, 0.62)
        for x, y in ((6, 7), (7, 7), (8, 8)):
            px[x, y] = shade(BASALT, 0.7)
        for x in range(16):
            if noise(x, 15, 25) > 0.45:
                px[x, 15] = (96, 70, 44, 255)
    return img


def furnace(face, body=FURNACE_BODY, glow=True):
    """Clay furnace: sharp edges, arched firing hole with a soot ring, round top."""
    img = Image.new('RGBA', (16, 16))
    px = img.load()
    for y in range(16):
        for x in range(16):
            k = 1.06 if y <= 1 else 0.86 if y >= 14 else 1.0
            if y == 15:
                k = 0.7
            if x in (0, 15):
                k *= 0.84
            k *= 0.9 + 0.2 * noise(x, y, {'front': 1, 'side': 2, 'top': 3}[face])
            px[x, y] = shade(body, k)
    if face == 'front':
        def inside(x, y):
            if y == 6:
                return 6 <= x <= 9
            return 6 < y <= 14 and 5 <= x <= 10
        ring = ((0, 1), (1, 0), (-1, 0), (1, 1), (-1, 1))
        for y in range(16):
            for x in range(16):
                if inside(x, y):
                    px[x, y] = DARK + (255,)
                elif y < 15 and any(inside(x + dx, y + dy) for dx, dy in ring):
                    px[x, y] = SOOT + (255,)
                elif y < 15 and any(inside(x + 2 * dx, y + 2 * dy) for dx, dy in ring):
                    px[x, y] = shade(SOOT, 1.45)   # The heavier outer soot ring.
        if glow:
            fire = {13: (5, 10, (214, 88, 30)), 12: (6, 9, (240, 140, 40)),
                    11: (7, 8, (252, 200, 70)), 10: (7, 7, (240, 140, 40))}
            for y, (a, b, color) in fire.items():
                for x in range(a, b + 1):
                    px[x, y] = color + (255,)
        for x in (5, 6, 8, 9, 10):
            px[x, 14] = (96, 62, 34, 255)
        px[7, 14] = (230, 110, 30, 255) if glow else (60, 40, 30, 255)
    elif face == 'side':
        for x, y in ((3, 4), (4, 4), (5, 4), (9, 9), (10, 9), (11, 9), (6, 12), (7, 12)):
            px[x, y] = shade(body, 1.08)
        for x, y in ((11, 3), (12, 4), (12, 5), (13, 6)):
            px[x, y] = shade(body, 0.72)
    else:
        for y in range(16):
            for x in range(16):
                d = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
                if d < 3.2:
                    px[x, y] = (150, 50, 20, 255) if d < 1.2 else DARK + (255,)
                elif d < 4.4:
                    px[x, y] = shade(FURNACE_RIM if body == FURNACE_BODY else body, 0.94 + 0.08 * noise(x, y, 9))
                elif d < 5.1:
                    px[x, y] = shade(body, 0.8)
    return img


def bellows():
    """Two boards with a gathered leather/wool bag and a nozzle."""
    img = Image.new('RGBA', (16, 16))
    px = img.load()
    plank, dark, bag, nozzle = (176, 132, 78), (110, 80, 48), (214, 204, 190), (120, 120, 126)
    for y in range(3, 13):
        w = 6 - abs(y - 8) // 2
        for x in range(2, 2 + 2 * w):
            px[x, y] = shade(bag, 0.85 + 0.15 * noise(x, y, 31)) if y not in (3, 12) else shade(plank, 1)
    for x in range(1, 13):
        px[x, 3] = plank + (255,)
        px[x, 12] = plank + (255,)
        px[x, 4] = dark + (255,)
        px[x, 13] = dark + (255,)
    for y in range(6, 10):
        px[13, y] = nozzle + (255,)
        px[14, y] = nozzle + (255,)
    px[15, 7] = px[15, 8] = shade(nozzle, 0.7)
    for y in (6, 9):
        for x in range(3, 11, 2):
            px[x, y] = shade(bag, 0.7)
    return img


def palette():
    data = palette_bytes()
    return [tuple(data[i:i + 3]) for i in range(0, 768, 3)]


def to_palette(image, ramps, rgb):
    cands = [r * 16 + s for r in ramps for s in range(1, 16)]
    tile, cache = [], {}
    for r, g, b, a in image.getdata():
        if a < 128:
            tile.append(0)
            continue
        if (r, g, b) not in cache:
            cache[(r, g, b)] = min(cands, key=lambda i: 2 * (r - rgb[i][0]) ** 2 +
                                   4 * (g - rgb[i][1]) ** 2 + 3 * (b - rgb[i][2]) ** 2)
        tile.append(cache[(r, g, b)])
    return bytes(tile)


WOOD = (BARK, PLANK, DIRT)


def tiles(src):
    """(texture name, RGBA image, allowed ramps, rule) for every early-game tile."""
    iron_spear, stone_spear, copper_spear = src('iron_spear'), src('stone_spear'), src('copper_spear')
    copper = {t: src('copper_' + t) for t in ('pickaxe', 'axe', 'shovel', 'sword')}
    iron = {t: src('iron_' + t) for t in ('pickaxe', 'axe', 'shovel', 'sword')}
    rows = [
        ('T_CLAY', recolor(src('block/clay'), CLAY), (TERRA,), 'clay.png on the "less noise" red-clay gradient'),
        ('T_COPPER_ORE', src('block/copper_ore'), (GRAY, TERRA, CYAN), 'copper_ore.png'),
        ('T_FIRE_LOG', src('block/campfire_log_lit'), (BARK, PLANK, DIRT, FLAME, RED, TERRA), 'campfire_log_lit.png, frame 1'),
        ('T_FIRE_FLAME', src('block/campfire_fire'), (FLAME, RED, TERRA), 'campfire_fire.png, frame 1'),
        ('T_FIRE_ASH', src('block/campfire_log'), (BARK, PLANK, DIRT, GRAY), 'campfire_log.png (unlit)'),
        ('T_ANVIL_TOP', anvil('top'), (GRAY, PEACH, BARK), 'hand-drawn second-pass anvil, top'),
        ('T_ANVIL_SIDE', anvil('side'), (GRAY, PEACH, BARK), 'hand-drawn second-pass anvil, side'),
        ('T_CLAYF_FRONT', furnace('front'), (TERRA, RED, FLAME, BARK), 'hand-drawn terracotta furnace front, heavier soot ring'),
        ('T_CLAYF_SIDE', furnace('side'), (TERRA, BARK), 'hand-drawn terracotta furnace side'),
        ('T_CLAYF_TOP', furnace('top'), (TERRA, RED, BARK), 'hand-drawn terracotta furnace top'),
        ('T_BRICKS', recolor(src('block/bricks'), BRICK, {(x, y) for y in range(16) for x in range(16)
                                                          if lum(src('block/bricks').getpixel((x, y))) < 150}),
         (TERRA, GRAY), 'bricks.png, bricks on the fired-brick gradient, grey mortar'),
        ('T_BLOOMERY_FRONT', furnace('front', BLOOMERY_BODY), (TERRA, RED, FLAME, BARK),
         'darker fired-clay furnace front'),
        ('T_BLOOMERY_SIDE', furnace('side', BLOOMERY_BODY), (TERRA, BARK), 'darker fired-clay furnace side'),
        ('T_I_CLAY_LUMP', recolor(src('clay_ball'), CLAY_LUMP), (TERRA,), 'clay_ball.png, higher-contrast red clay'),
        ('T_I_BRICK', recolor(src('brick'), BRICK), (TERRA,), 'brick.png, brighter fired brick'),
        ('T_I_FLINT', src('flint'), (GRAY,), 'flint.png'),
        ('T_I_CHARCOAL', src('charcoal'), (GRAY, BARK), 'charcoal.png'),
        ('T_I_COPPER_INGOT', src('copper_ingot'), (TERRA,), 'copper_ingot.png'),
        ('T_I_TIN', recolor(src('raw_copper'), TIN), (GRAY, BARK, DIRT), 'raw_copper.png shape, dark cassiterite'),
        ('T_I_BRONZE_INGOT', recolor(src('copper_ingot'), BRONZE), (PLANK, FLAME, BARK), 'copper_ingot.png recoloured bronze'),
        ('T_I_STICK_SPEAR', src('wooden_spear'), WOOD, 'wooden_spear.png'),
        ('T_I_FLINT_SPEAR', recolor(stone_spear, FLINT, differ(stone_spear, iron_spear)), (GRAY, *WOOD),
         'stone_spear.png with a flint head'),
        ('T_I_COPPER_SPEAR', copper_spear, (TERRA, *WOOD), 'copper_spear.png'),
        ('T_I_BRONZE_SPEAR', recolor(copper_spear, BRONZE, differ(copper_spear, iron_spear)), (PLANK, FLAME, *WOOD),
         'copper_spear.png with a bronze head'),
        ('T_I_IRON_SPEAR', iron_spear, (GRAY, *WOOD), 'iron_spear.png'),
        ('T_I_COPPER_PICK', copper['pickaxe'], (TERRA, *WOOD), 'copper_pickaxe.png'),
        ('T_I_COPPER_AXE', copper['axe'], (TERRA, *WOOD), 'copper_axe.png'),
        ('T_I_COPPER_SHOVEL', copper['shovel'], (TERRA, *WOOD), 'copper_shovel.png'),
        ('T_I_IRON_AXE', iron['axe'], (GRAY, *WOOD), 'iron_axe.png'),
        ('T_I_IRON_SHOVEL', iron['shovel'], (GRAY, *WOOD), 'iron_shovel.png'),
        ('T_I_DIAMOND_AXE', src('diamond_axe'), (CYAN, GRAY, *WOOD), 'diamond_axe.png'),
        ('T_I_DIAMOND_SHOVEL', src('diamond_shovel'), (CYAN, GRAY, *WOOD), 'diamond_shovel.png'),
        ('T_I_FIRE', src('campfire'), (BARK, PLANK, DIRT, FLAME, RED, GRAY), 'campfire.png'),
        ('T_I_BELLOWS', bellows(), (BARK, PLANK, GRAY, SAND), 'hand-drawn bellows'),
    ]
    for tool, label in (('pickaxe', 'PICK'), ('axe', 'AXE'), ('shovel', 'SHOVEL'), ('sword', 'SWORD')):
        rows.append(('T_I_BRONZE_' + label, recolor(copper[tool], BRONZE, differ(copper[tool], iron[tool])),
                     (PLANK, FLAME, *WOOD), 'copper_' + tool + '.png with a bronze head'))
    return rows


def write(assets, output=None):
    output = Path(output or ROOT / 'assets/textures')
    rgb = palette()
    src = Source(assets)
    records, lines = [], []
    for name, image, ramps, rule in tiles(src):
        tile = to_palette(image, ramps, rgb)
        records.append(dict(texture=name, ramps=list(ramps), rule=rule, sha256=sha256(tile).hexdigest(),
                            colors=len(set(tile)), transparent_pixels=tile.count(0)))
        lines.append('    { /* ' + name + ' */')
        lines.extend('        ' + ','.join(str(c) for c in tile[r * 16:(r + 1) * 16]) + ',' for r in range(16))
        lines.append('    },')
    head = ['/* Generated by tools/early_game_textures.py; see assets/textures/early_game.json. */',
            'static const u8 dos_early_ids[]={' + ','.join(r['texture'] for r in records) + '};',
            'static const u8 dos_early_tiles[][256]={']
    tail = ['};', 'static void dos_tex_import_early(void)', '{',
            '    for(unsigned i=0;i<sizeof(dos_early_ids);++i)',
            '        memcpy(g_tex[dos_early_ids[i]],dos_early_tiles[i],256);', '}', '']
    (output / 'early_game.inc').write_text('\n'.join(head + lines + tail))
    (output / 'early_game.json').write_text(json.dumps(dict(
        assets_sha256=sha256(Path(assets).read_bytes()).hexdigest(),
        palette_sha256=sha256(bytes(c for color in rgb for c in color)).hexdigest(),
        conversion='nearest shade 1-15 of the listed ramps; no dithering', tiles=records), indent=2) + '\n')
    print('Generated', len(records), 'early-game tiles')
    return records


def names():
    """Texture names in generation order, without needing the resource pack."""
    return [line.split('/* ')[1].split(' */')[0] for line in
            (ROOT / 'assets/textures/early_game.inc').read_text().splitlines() if line.startswith('    { /* ')]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    write(args.assets, args.output)
