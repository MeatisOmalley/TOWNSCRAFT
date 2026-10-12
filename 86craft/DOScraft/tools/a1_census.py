"""Analyze A1's visible coplanar-rectangle opportunity from a private replay.

The plane-wide packing is an optimistic geometry bound: it ignores painter
interleaving, composited-texture cost, near clipping, and affine subdivision.
The consecutive-item counts are stricter bounds that keep painter order;
one also keeps the original texture/light key.
"""

import argparse
import json
import statistics
import struct
from collections import defaultdict
from pathlib import Path

from run_vm import diagnostic_volume


SCENES = ('open_static', 'walk', 'walk_down', 'walk_up', 'house', 'block_edits')


def rectangle(a, b):
    x, z, y = a & 255, (a >> 8) & 255, (a >> 16) & 63
    direction = (a >> 22) & 7
    w, h = ((b >> 16) & 15) + 1, ((b >> 20) & 15) + 1
    if direction < 2:
        return (direction, x + (direction == 1)), (z, z + h, y, y + 1)
    if direction < 4:
        return (direction, y + (direction == 3)), (x, x + w, z, z + h)
    return (direction, z + (direction == 5)), (x, x + w, y, y + 1)


def read_scene(volume, number, sample):
    data = volume.read(f'A1S{number}P{sample}.BIN')
    if len(data) < 4:
        raise ValueError('Short A1 dump')
    count, = struct.unpack_from('<I', data)
    if len(data) != 4 + count * 16:
        raise ValueError('A1 record count does not match file length')
    return [struct.unpack_from('<IIII', data, 4 + i * 16) for i in range(count)]


def union_if_rectangle(left, right):
    a, b, c, d = left
    e, f, g, h = right
    if c == g and d == h and (b == e or f == a):
        return (min(a, e), max(b, f), c, d)
    if a == e and b == f and (d == g or h == c):
        return (a, b, min(c, g), max(d, h))
    return None


def greedy_rectangles(source, by_row):
    cells = set(source)
    rectangles = 0
    while cells:
        u, v = min(cells, key=(lambda cell: (cell[1], cell[0])) if by_row else
                   (lambda cell: (cell[0], cell[1])))
        if by_row:
            end = u + 1
            while (end, v) in cells:
                end += 1
            bottom = v + 1
            while all((x, bottom) in cells for x in range(u, end)):
                bottom += 1
            for yy in range(v, bottom):
                for xx in range(u, end):
                    cells.remove((xx, yy))
        else:
            bottom = v + 1
            while (u, bottom) in cells:
                bottom += 1
            end = u + 1
            while all((end, yy) in cells for yy in range(v, bottom)):
                end += 1
            for xx in range(u, end):
                for yy in range(v, bottom):
                    cells.remove((xx, yy))
        rectangles += 1
    return rectangles


def analyze(records):
    groups = defaultdict(list)
    visible = []
    all_terrain = 0
    for a, b, key, faces in records:
        if (a >> 25) & 3:
            continue
        all_terrain += 1
        if not faces:
            continue
        group, rect = rectangle(a, b)
        material = b & 65535
        groups[group].append((rect, material))
        visible.append((group, rect, material, faces))
    packed = 0
    overlaps = 0
    composite_cells = 0
    varied_groups = 0
    for entries in groups.values():
        cells = set()
        total_cells = 0
        materials = set()
        for (left, right, top, bottom), material in entries:
            materials.add(material)
            total_cells += (right - left) * (bottom - top)
            cells.update((u, v) for u in range(left, right) for v in range(top, bottom))
        overlaps += total_cells - len(cells)
        composite_cells += len(cells)
        varied_groups += len(materials) > 1
        packed += min(greedy_rectangles(cells, True), greedy_rectangles(cells, False))
    consecutive_count = 0
    same_material_count = 0
    previous_group = None
    previous_rect = None
    previous_material = None
    material_rect = None
    for group, rect, material, _ in visible:
        same_group = group == previous_group
        merged = union_if_rectangle(previous_rect, rect) if same_group else None
        if merged is not None:
            previous_rect = merged
        else:
            consecutive_count += 1
            previous_group, previous_rect = group, rect
        same_material_merged = (union_if_rectangle(material_rect, rect)
                                if same_group and material == previous_material
                                else None)
        if same_material_merged is not None:
            material_rect = same_material_merged
        else:
            same_material_count += 1
            material_rect = rect
        previous_material = material
    return {
        'static_items': len(records),
        'terrain_items': all_terrain,
        'visible_terrain_items': len(visible),
        'visible_terrain_faces_after_subdivision': sum(faces for _, _, _, faces in visible),
        'coplanar_direction_groups': len(groups),
        'groups_with_multiple_texture_or_light_keys': varied_groups,
        'visible_surface_cells': composite_cells,
        'overlapping_same_direction_cells': overlaps,
        'optimistic_plane_rectangles': packed,
        'optimistic_items_removed': len(visible) - packed,
        'consecutive_order_rectangles': consecutive_count,
        'consecutive_items_removed': len(visible) - consecutive_count,
        'consecutive_same_material_rectangles': same_material_count,
        'consecutive_same_material_items_removed': len(visible) - same_material_count,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vm', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    volume = diagnostic_volume((args.vm / 'scratch.img').read_bytes())
    scenes = {}
    for index, name in enumerate(SCENES):
        samples = [analyze(read_scene(volume, index, sample)) for sample in range(4)]
        means = {key: statistics.mean(sample[key] for sample in samples)
                 for key in samples[0]}
        scenes[name] = {'sample_frames': [0, 10, 21, 31],
                        'samples': samples, 'mean': means}
    result = {'vm': str(args.vm.resolve()), 'method': __doc__, 'scenes': scenes}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    for name, data in scenes.items():
        row = data['mean']
        print(f"{name}: visible={row['visible_terrain_items']:.1f} "
              f"painter-adjacent={row['consecutive_order_rectangles']:.1f} "
              f"optimistic-plane={row['optimistic_plane_rectangles']:.1f} "
              f"faces={row['visible_terrain_faces_after_subdivision']:.1f}")
    print('REPORT=' + str(args.output))


if __name__ == '__main__':
    main()
