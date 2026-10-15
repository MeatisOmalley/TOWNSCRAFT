"""Render DOScraft's music for the N64: build/romfs-src/MUSIC.N64.

Compiles tools/music_render (DOScraft's unchanged opl_music.c and
sound_assets.c with Nuked-OPL3 in place of the OPL2 chip) for 32-bit x86
with the Zig toolchain DOScraft's own native tests use, and runs it on the
pinned MUSIC.PAK. Needs build/deps/nuked-opl3 (tools/fetch_dependencies.py),
build/doscraft (tools/stage_doscraft.py) and build/romfs-src/MUSIC.PAK.

Usage: python tools/render_music.py
"""
import hashlib
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[2] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
ZIG_MAIN = Path('C:/Users/meat/Documents/craft/fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe')
PACK = ROOT / 'build/romfs-src/MUSIC.PAK'
# DOScraft's shipped MUSIC.PAK; build_music_pack.py at the pin reproduces it
# from the local MIDI files (not in git).
PACK_SHA256 = '07fde5bb5d945c47ff553d7010b27cffaaf17a1658f1f53b2aa537cb3ccecf2b'
OUT = ROOT / 'build/romfs-src/MUSIC.N64'


def main():
    zig = ZIG if ZIG.is_file() else ZIG_MAIN
    if not zig.is_file():
        sys.exit('Zig 0.13 (fmtowns/townscraft/build/tools) not found')
    if hashlib.sha256(PACK.read_bytes()).hexdigest() != PACK_SHA256:
        sys.exit(f'{PACK} is not the pinned music pack')
    exe = ROOT / 'build/music_render.exe'
    dos = ROOT / 'vendor/doscraft/src/platform/dos'
    staged = ROOT / 'build/doscraft'
    subprocess.run([str(zig), 'cc', '-target', 'x86-windows-gnu', '-O2', '-std=gnu99',
                    '-Wno-unused-function',
                    '-I', str(ROOT / 'tools/music_render/host'),
                    '-I', str(ROOT / 'build/deps/nuked-opl3'),
                    '-I', str(dos), '-I', str(staged / 'platform/src'),
                    str(ROOT / 'tools/music_render/music_render.c'),
                    str(dos / 'sound_assets.c'),
                    str(staged / 'imported/src/fmath.c'), str(staged / 'imported/src/tables.c'),
                    str(ROOT / 'build/deps/nuked-opl3/opl3.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(PACK), str(OUT)], check=True)
    print(f'{OUT}: {OUT.stat().st_size} bytes')


if __name__ == '__main__':
    main()
