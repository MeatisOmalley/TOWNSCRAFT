"""Build the pinned Trident 16-bit ISA runtime for the private 386 VM.

Requires the existing MSYS2 UCRT64 86Box build dependencies. Other emulator
directories and their launchers are not modified.
"""
import argparse
import hashlib
import json
import shutil
import subprocess

from build_game import ROOT
from build_patched_86box import msys

COMMIT = 'cc2ff8fe3058c4f8eec70cfac82b1909894d458f'
REPO = 'https://github.com/MeatisOmalley/86Box.git'
SOURCE = ROOT / 'build/deps/86box-386-source'
OUTPUT = ROOT / 'build/deps/86box-doscraft-386'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assemble-only', action='store_true', help='Package an already completed build of this pinned source')
    args = parser.parse_args()
    if not SOURCE.exists():
        subprocess.run(['git', 'clone', '--filter=blob:none', '--no-checkout', '--single-branch',
            '--branch', 'claude/beautiful-mayer-r77zh9', REPO, str(SOURCE)], check=True)
        subprocess.run(['git', '-C', str(SOURCE), 'checkout', '--detach', COMMIT], check=True)
    head = subprocess.check_output(['git', '-C', str(SOURCE), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(SOURCE), 'status', '--porcelain', '--untracked-files=no'], text=True).strip()
    if head != COMMIT or dirty:
        raise ValueError('Expected the clean pinned Trident emulator source: ' + COMMIT)
    if not args.assemble_only:
        msys('cmake -G Ninja --preset=regular -D CMAKE_BUILD_TYPE=Release '
             '-D CMAKE_TOOLCHAIN_FILE=cmake/flags-gcc-x86_64.cmake -D ARCH=x86_64 '
             '-D EMU_GIT_HASH=cc2ff8fe3 -D EMU_BUILD=doscraft-386 -S . -B build-ucrt64 '
             '&& cmake --build build-ucrt64 --parallel 8', SOURCE)
    built = SOURCE / 'build-ucrt64/src/86Box.exe'
    OUTPUT.mkdir(parents=True, exist_ok=True)
    official = ROOT / 'build/deps/86box'
    for path in official.iterdir():
        if path.name in ('roms', '86Box.exe', 'download.zip'):
            continue
        if path.is_dir():
            shutil.copytree(path, OUTPUT / path.name, dirs_exist_ok=True)
        else:
            shutil.copy2(path, OUTPUT / path.name)
    shutil.copy2(built, OUTPUT / '86Box.exe')
    digest = hashlib.sha256(built.read_bytes()).hexdigest()
    (OUTPUT / 'build.json').write_text(json.dumps({'source_commit': COMMIT, 'repository': REPO,
        'exe_sha256': digest, 'patch': 'Trident TVGA 8900 registered as 16-bit ISA'}, indent=2) + '\n')
    print(f'EMULATOR={OUTPUT / "86Box.exe"}\nSHA256={digest}')


if __name__ == '__main__':
    main()
