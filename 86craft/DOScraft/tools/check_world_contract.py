"""Check a baked 1024 world against the early-game world contract.

Gameplay depends on these (EARLY_GAME.md, World-generation contract); a seed or
baker change must not quietly break them. Reads a V1 base, lit or distant
terrain image (the base columns are the same) and fails with the first broken
rule. Distances are in blocks from the spawn column.
"""
import argparse
import json
from pathlib import Path
import struct

from bake_large_world import (AIR, WATER, LOG, COBBLE, GRAVEL, CLAY, COPPER, TIN_BIT,
                              WIDTH, WH, base_offset)

# Rule: (material, nearest within, at least this far) in blocks from spawn.
NEAR = (('water', 64, 0), ('log', 32, 0), ('cobble', 64, 0), ('gravel', 96, 0),
        ('clay', 96, 0), ('copper', 200, 0), ('tin_gravel', 400, 96))
# Rule: material count across the world, at least.
TOTALS = (('clay', 20000), ('copper', 5000), ('cobble', 20000), ('gravel', 20000), ('tin_gravel', 2000))


def scan(image):
    if image[:16] != b'DOSCRAFT-1024-V1':
        raise ValueError('Expected a V1 1024 base, lit or distant terrain image')
    spawn = struct.unpack_from('<2I', image, 40)
    counts = dict.fromkeys(('water', 'log', 'cobble', 'gravel', 'clay', 'copper', 'tin_gravel'), 0)
    nearest = dict.fromkeys(counts, None)
    sx, sz = spawn
    for z in range(WIDTH):
        for x in range(WIDTH):
            at = base_offset(x, z)
            column = image[at:at + WH]
            found = set()
            if WATER in column:
                found.add('water')
            if LOG in column:
                found.add('log')
            if CLAY in column:
                found.add('clay')
                counts['clay'] += column.count(CLAY)
            if COPPER in column:
                found.add('copper')
                counts['copper'] += column.count(COPPER)
            if COBBLE in column:
                # Loose stone is what a hand can reach: the top two solid cells.
                top = len(column.rstrip(bytes([AIR])))
                if COBBLE in column[max(0, top - 2):top]:
                    found.add('cobble')
                counts['cobble'] += column.count(COBBLE)
            plain, tin = column.count(GRAVEL), column.count(GRAVEL | TIN_BIT)
            if plain or tin:
                found.add('gravel')
                counts['gravel'] += plain + tin
            if tin:
                found.add('tin_gravel')
                counts['tin_gravel'] += tin
            if found:
                distance = max(abs(x - sx), abs(z - sz))
                for name in found:
                    if nearest[name] is None or distance < nearest[name]:
                        nearest[name] = distance
            if 'water' in found:
                counts['water'] += 1
            if 'log' in found:
                counts['log'] += 1
    return dict(spawn=list(spawn), counts=counts, nearest=nearest)


def check(report):
    failures = []
    for name, within, beyond in NEAR:
        d = report['nearest'][name]
        if d is None or d > within:
            failures.append(f'{name}: nearest {d} blocks from spawn, needs at most {within}')
        elif d < beyond:
            failures.append(f'{name}: nearest {d} blocks from spawn, needs at least {beyond}')
    for name, minimum in TOTALS:
        if report['counts'][name] < minimum:
            failures.append(f'{name}: {report["counts"][name]} in the world, needs at least {minimum}')
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--terrain', required=True, type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = scan(args.terrain.read_bytes())
    failures = check(report)
    report['failures'] = failures
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if failures:
        raise SystemExit('World contract broken:\n' + '\n'.join(failures))
    print('WORLD_CONTRACT=PASS')


if __name__ == '__main__':
    main()
