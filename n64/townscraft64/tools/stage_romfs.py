"""Stage the game ROM's files into build/romfs.

terrain.bin is DOScraft's compact V2 crown-leaf 1024 world, byte for byte
(see FIXTURE in stage_doscraft_tests.py for where it comes from); the game
reads its word tables little-endian (n64_adapt.medium_le32).

Usage: python tools/stage_romfs.py
"""
import hashlib
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'build/fixtures/terrain1024-crown-leaves-v2.bin'
FIXTURE_SHA256 = '45d5e9a149c55b994f7f1505c96b094b227010417d9dd280544feeb97fb6093d'
OUTPUT = ROOT / 'build/romfs'


def main():
    if not FIXTURE.is_file():
        sys.exit(f'{FIXTURE} missing: run DOScraft tools/convert_large_v2.py on its '
                 'terrain1024-crown-leaves.bin (see research/n2b note)')
    if hashlib.sha256(FIXTURE.read_bytes()).hexdigest() != FIXTURE_SHA256:
        sys.exit(f'{FIXTURE} is not the pinned compact world')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    target = OUTPUT / 'terrain.bin'
    if not target.exists() or target.stat().st_size != FIXTURE.stat().st_size:
        shutil.copy(FIXTURE, target)
    print(f'romfs: terrain.bin ({target.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
