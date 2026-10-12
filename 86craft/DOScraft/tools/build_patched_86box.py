"""Build the DOScraft 86Box runtime: v6.0 plus the pinned DOScraft hardware commits.

The fork branch below is 86Box v6.0 plus:
  - ET4000AX ISA registered as a 16-bit ISA card (v6.0 gave it byte-only
    memory handlers, turning each 32-bit write into four 8-bit transfers);
  - planar/latched VGA word and dword accesses charged as one bus transfer;
  - i486DX/33 ISA bus at 11.1 MHz (33.33 MHz / 3), a period AT-bus overclock;
  - a build-only rename in 808x.c for current MinGW headers.
It is built with the same MSYS2 UCRT64 recipe as 86Box's release builds and
assembled into PATCHED_EMULATOR_DIR: the patched static 86Box.exe plus the
official runtime's support files. ROMs stay in the official runtime
(build/deps/86box/roms) and are passed with -R. Prints the executable SHA-256
to pin as fetch_dependencies.PATCHED_EMULATOR_EXE_SHA256.

Needs C:/msys64 with 86Box's .ci/dependencies_msys.txt packages (ucrt64) and a
clean checkout of the fork at PATCHED_86BOX_COMMIT.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
from pathlib import Path
from fetch_dependencies import (PATCHED_86BOX_REPO, PATCHED_86BOX_BRANCH,
                                PATCHED_86BOX_COMMIT, PATCHED_EMULATOR_DIR)

ROOT = Path(__file__).resolve().parents[1]
MSYS = Path('C:/msys64')
OFFICIAL = ROOT / 'build/deps/86box'


def msys(command, cwd):
    """Run one command in a UCRT64 login shell at cwd."""
    posix = '/' + cwd.drive[0].lower() + cwd.as_posix()[2:]
    return subprocess.run([str(MSYS / 'usr/bin/bash.exe'), '-lc', f'cd "{posix}" && {command}'],
                          env={**os.environ, 'MSYSTEM': 'UCRT64', 'CHERE_INVOKING': '1'},
                          check=True, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT.parent / '86Box-v6.0-timing',
                        help=f'Checkout of {PATCHED_86BOX_REPO} branch {PATCHED_86BOX_BRANCH}')
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    head = subprocess.run(['git', '-C', str(source), 'rev-parse', 'HEAD'], check=True,
                          capture_output=True, text=True).stdout.strip()
    if head != PATCHED_86BOX_COMMIT:
        raise SystemExit(f'Source is at {head}, expected pinned {PATCHED_86BOX_COMMIT}')
    if subprocess.run(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no'],
                      check=True, capture_output=True, text=True).stdout.strip():
        raise SystemExit('Source has uncommitted changes; build only the pinned commit')
    if not (OFFICIAL / '86Box.exe').is_file():
        raise SystemExit('Official runtime missing; run fetch_dependencies.py first')
    msys(f'cmake -G Ninja --preset=regular -D CMAKE_BUILD_TYPE=Release '
         f'-D CMAKE_TOOLCHAIN_FILE=cmake/flags-gcc-x86_64.cmake -D ARCH=x86_64 '
         f'-D EMU_GIT_HASH={head[:9]} -D EMU_BUILD=doscraft '
         f'-S . -B build-ucrt64 && cmake --build build-ucrt64', source)
    built = source / 'build-ucrt64/src/86Box.exe'
    linked = msys(f'ldd "{built.as_posix()}"', source).stdout
    foreign = [line.strip() for line in linked.splitlines() if '/ucrt64/' in line]
    if foreign:
        raise SystemExit('Expected a static build; links: ' + '; '.join(foreign))
    output = ROOT / PATCHED_EMULATOR_DIR
    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(OFFICIAL, output, ignore=shutil.ignore_patterns('roms', '86Box.exe', 'download.zip'))
    shutil.copy2(built, output / '86Box.exe')
    digest = hashlib.sha256((output / '86Box.exe').read_bytes()).hexdigest()
    print(f'RUNTIME={output}\nEXE_SHA256={digest}')


if __name__ == '__main__':
    main()
