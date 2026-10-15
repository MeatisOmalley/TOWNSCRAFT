"""Attach a newly baked 1024-world HDD to a private June-1990 test VM."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from build_game import ROOT
from fat_image import populate, scratch_hdd
from save_fixture import save_image
from large_world_media import check_terrain,compact_terrain


def attach_large_disk(vm, terrain_path, *, player=False):
    vm = Path(vm).resolve(strict=True)
    prefix = 'game-play-' if player else 'game-smoke-'
    if not vm.is_relative_to(ROOT / 'runtime') or not vm.name.startswith(prefix):
        raise ValueError('Expected a private ' + ('player' if player else 'smoke') + ' VM')
    record_path = vm / 'build.json'
    record = json.loads(record_path.read_text())
    if bool(record.get('smoke')) == player or record.get('hardware_era') != '1990-06-30':
        raise ValueError('Expected the selected June-1990 VM type')
    source = Path(terrain_path).read_bytes()
    terrain = compact_terrain(source)
    disk = scratch_hdd(cylinders=800, heads=8, sectors=63)
    start = struct.unpack_from('<I', disk, 454)[0] * 512
    disk[start:] = populate(disk[start:start + 512],
                            [('TERRAIN.TMP', terrain), ('WORLD.SAV', save_image())])
    target = vm / 'scratch-large.img'
    with target.open('xb') as file:
        file.write(disk)
    config_path = vm / '86box.cfg'
    config = config_path.read_text()
    before = 'hdd_01_parameters = 17, 4, 615, 0, scsi\n'
    after = 'hdd_01_parameters = 63, 8, 800, 0, scsi\n'
    if config.count(before) != 1 or config.count('hdd_01_fn = scratch.img\n') != 1:
        raise ValueError('Unexpected private VM disk configuration')
    config = config.replace(before, after).replace('hdd_01_fn = scratch.img\n',
                                                  'hdd_01_fn = scratch-large.img\n')
    config_path.write_text(config)
    record['large_world_experiment'] = True
    record['large_world'] = True
    record['terrain_sha256'] = hashlib.sha256(terrain).hexdigest()
    record['source_terrain_sha256'] = hashlib.sha256(source).hexdigest()
    record['terrain_format']='V2 RLE'
    end=97 if terrain[:16]==b'DOSCRAFT-1024-V2' else 33
    record['terrain_header_sha256'] = hashlib.sha256(terrain[:end * 512]).hexdigest()
    record['disk_note'] = 'Private 206 MiB SCSI disk with prebaked 1024-block seed and V5 save medium'
    record_path.write_text(json.dumps(record, indent=2) + '\n')
    print(f'VM={vm}\nDISK={target}\nBYTES={target.stat().st_size}')
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vm', required=True, type=Path)
    parser.add_argument('--terrain', required=True, type=Path)
    args = parser.parse_args()
    attach_large_disk(args.vm, args.terrain)


if __name__ == '__main__':
    main()
