"""Clone a stopped V2 player VM onto V3 edit-log storage; keep the original.

Both committed save banks and every edited column move into the log, so all
4096 columns become editable. The original VM directory is never written: the
result is a new runtime/game-log-* clone. The launcher pointer changes only
with --update-pointer. --check converts in memory and reports without writing.
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import tempfile
from build_game import ROOT
from crown_leaves_media import running, digest
from convert_large_v3 import V3_MAGIC, migrate_player, v3_records
from fat_image import FatImage
from large_world_media import disk_log_sectors, replace_world_files
from publish_game import atomic_write


def migrate_disk(disk):
    """V2 player disk -> (V3 player disk, terrain, proof). The log is 24 MiB,
    or what the volume has room for (the 386's 40 MB disk)."""
    part = struct.unpack_from('<I', disk, 454)[0] * 512
    volume = FatImage(disk, part)
    terrain, save = volume.read('TERRAIN.TMP'), volume.read('WORLD.SAV')
    if terrain[:16] != b'DOSCRAFT-1024-V2':
        raise ValueError('Expected V2 compact player terrain')
    count = disk_log_sectors(disk, struct.unpack_from('<I', terrain, 132)[0])
    log, updated, proof = migrate_player(terrain, save, count)
    result = replace_world_files(disk, log, updated)
    checked = FatImage(result, part)
    if checked.read('TERRAIN.TMP') != log or checked.read('WORLD.SAV') != updated:
        raise ValueError('Migrated disk roundtrip failed')
    v3_records(log, updated, checked=True)
    proof.update(save_before_sha256=digest(save), save_after_sha256=digest(updated),
                 terrain_before_sha256=digest(terrain), terrain_sha256=digest(log))
    return result, log, proof


def install(pointer, boot_from=None, check=False, update_pointer=False):
    pointer = Path(pointer).resolve(strict=True)
    vm = Path(pointer.read_text().strip()).resolve(strict=True)
    if not vm.is_relative_to((ROOT / 'runtime').resolve()) or running(vm):
        raise ValueError('Save and close the player VM before converting')
    record = json.loads((vm / 'build.json').read_text())
    if record.get('smoke') or record.get('benchmark') or Path(record['vm_directory']).resolve() != vm:
        raise ValueError('Expected a player VM')
    files = [vm / n for n in ('hdd.img', 'scratch-large.img') if (vm / n).is_file()]
    if len(files) != 1:
        raise ValueError('Expected one large-world disk')
    source = files[0]
    original = source.read_bytes()
    disk, log, proof = migrate_disk(original)
    proof.update(source_vm=str(vm), source_disk_sha256=digest(original), disk_sha256=digest(disk))
    if check:
        return {'vm': str(vm), 'status': 'checked in memory; nothing written', **proof}
    boot_source = Path(boot_from).resolve(strict=True) if boot_from else vm
    boot_record = json.loads((boot_source / 'build.json').read_text())
    boot = (boot_source / 'boot.img').read_bytes()
    exe = FatImage(boot).read('DOSCRAFT.EXE')
    if digest(boot) != boot_record['boot_sha256'] or digest(exe) != boot_record['exe_sha256'] or \
            V3_MAGIC not in exe:
        raise ValueError('Supply --boot-from a tested release VM whose executable reads V3 media')
    if boot_from and boot_record.get('smoke'):
        raise ValueError('A smoke VM boots the diagnostic; use a player release VM')
    if running(vm) or source.read_bytes() != original:
        raise ValueError('Player changed during conversion; nothing written')
    result = Path(tempfile.mkdtemp(prefix='game-log-', dir=ROOT / 'runtime'))
    shutil.copytree(vm, result, dirs_exist_ok=True)
    if (result / source.name).read_bytes() != original:
        raise ValueError('Source changed during copy')
    (result / source.name).write_bytes(disk)
    if boot_from:
        iso = (boot_source / 'DOSCRAFT.ISO').read_bytes()
        if digest(iso) != boot_record['iso_sha256']:
            raise ValueError('Shared ISO checksum mismatch')
        (result / 'boot.img').write_bytes(boot)
        (result / 'DOSCRAFT.ISO').write_bytes(iso)
    updated = {**record, 'vm_directory': str(result), 'initial_hdd_sha256': digest(disk),
               'terrain_bytes': len(log), 'terrain_sha256': digest(log), 'terrain_format': 'V3 log',
               'edit_log_migration': proof, 'source_terrain_sha256': proof['terrain_before_sha256']}
    if boot_from:
        for key in ('exe_sha256', 'boot_sha256', 'iso_sha256', 'audio', 'sound_bank'):
            if key in boot_record:
                updated[key] = boot_record[key]
    if 'terrain_header_sha256' in updated:
        updated['terrain_header_sha256'] = digest(log[:97 * 512])
    (result / 'build.json').write_text(json.dumps(updated, indent=2) + '\n')
    status = 'staged V3 clone; launcher unchanged'
    if update_pointer:
        if running(vm) or source.read_bytes() != original or Path(pointer.read_text().strip()).resolve() != vm:
            raise ValueError('Player changed; the clone is staged but the pointer is unchanged')
        atomic_write(pointer, str(result).encode('utf-8'))
        status = 'staged V3 clone; launcher switched'
    return {'vm': str(result), 'status': status, **proof}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pointer', type=Path, required=True)
    p.add_argument('--boot-from', type=Path, help='Tested release VM whose executable reads V3 media')
    p.add_argument('--check', action='store_true', help='Convert in memory only')
    p.add_argument('--update-pointer', action='store_true', help='Switch the launcher to the clone')
    args = p.parse_args()
    print(json.dumps(install(args.pointer, args.boot_from, args.check, args.update_pointer), indent=2))


if __name__ == '__main__':
    main()
