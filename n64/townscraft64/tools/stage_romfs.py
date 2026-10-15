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
SOUND_SHA256 = '45bc513c8449e17a43ec1a0edeff50e735565926886801b6fac5d5fb23e144c3'


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
    # DOScraft's sampled effects: tools/build_sound_bank.py at the pinned
    # commit (assets/audio) reproduces its shipped SOUND.PAK byte for byte.
    bank = ROOT / 'build/romfs-src/SOUND.PAK'
    if hashlib.sha256(bank.read_bytes()).hexdigest() != SOUND_SHA256:
        sys.exit(f'{bank} is not the pinned sound bank')
    shutil.copy(bank, OUTPUT / 'SOUND.PAK')
    print(f'romfs: SOUND.PAK ({bank.stat().st_size} bytes)')
    # DOScraft's music, rendered by tools/render_music.py (OPL2 -> ADPCM).
    music = ROOT / 'build/romfs-src/MUSIC.N64'
    if music.is_file():
        shutil.copy(music, OUTPUT / 'MUSIC.N64')
        print(f'romfs: MUSIC.N64 ({music.stat().st_size} bytes)')
    else:
        print('romfs: no MUSIC.N64 (run tools/render_music.py); the game plays without music')


if __name__ == '__main__':
    main()
