"""Build the Townscraft64 ares fork: v148 plus a Randnet keyboard device.

The fork branch below is ares v148 (0aafd857) plus one commit
(patches/ares-v148-randnet-keyboard.patch):
  - "Randnet Keyboard" on the N64 controller ports (identifier 0x0002,
    command 0x13 with LED byte, 3 key slots and a status byte);
  - host keyboard mapped onto it by key position (JIS one to one, US
    fallbacks for the JIS-only keys), read while keyboard capture is on;
  - Japanese keys (Katakana, Ro, Henkan, Muhenkan, Yen) appended to the
    Windows raw-input keyboard;
  - settings Nintendo64/ControllerPort1..4 choosing the device connected at
    load (Gamepad, Mouse or Randnet Keyboard); a Randnet keyboard chosen
    this way turns keyboard capture on.

It is built like ares's own windows-x64 release: MSYS2 CLANG64 with
clang, cmake and ninja (pacman -S mingw-w64-clang-x86_64-{clang,cmake,ninja})
and the windows-ci-mingw-native preset, from a clean checkout at
RANDNET_ARES_COMMIT. The run directory is copied to RANDNET_ARES_DIR without
debug symbols. Prints the ares.exe SHA-256 to pin as
fetch_dependencies.RANDNET_ARES_EXE_SHA256.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
from fetch_dependencies import (RANDNET_ARES_BRANCH, RANDNET_ARES_COMMIT, RANDNET_ARES_DIR,
                                RANDNET_ARES_UPSTREAM)

ROOT = Path(__file__).resolve().parents[1]
MSYS2 = Path(os.environ.get('MSYS2_ROOT', r'C:\msys64'))


def clang64(command, cwd):
    """Run one command in a CLANG64 login shell at cwd."""
    posix = '/' + cwd.drive[0].lower() + cwd.as_posix()[2:]
    # A login shell started from a non-MSYS parent can lose PROCESSOR_ARCHITECTURE,
    # leaving CMAKE_SYSTEM_PROCESSOR empty, which ares's libco CMake rejects.
    script = f'export PROCESSOR_ARCHITECTURE=AMD64; cd "{posix}" && {command}'
    env = dict(os.environ, MSYSTEM='CLANG64', CHERE_INVOKING='1')
    subprocess.run([str(MSYS2 / 'usr/bin/bash.exe'), '-lc', script], env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--source', type=Path, default=ROOT.parent / 'ares-randnet',
                        help=f'checkout of branch {RANDNET_ARES_BRANCH} '
                             f'(ares {RANDNET_ARES_UPSTREAM} v148 plus the patch)')
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    if ' ' in str(source):
        raise SystemExit(f'{source}: ares does not build from a path with spaces')
    head = subprocess.run(['git', '-C', str(source), 'rev-parse', 'HEAD'], check=True,
                          capture_output=True, text=True).stdout.strip()
    if head != RANDNET_ARES_COMMIT:
        raise SystemExit(f'Source is at {head}, expected pinned {RANDNET_ARES_COMMIT}')
    if subprocess.run(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no'],
                      check=True, capture_output=True, text=True).stdout.strip():
        raise SystemExit('Source has uncommitted changes; build only the pinned commit')
    clang64('cmake --preset windows-ci-mingw-native && cmake --build build --config RelWithDebInfo',
            source)
    rundir = source / 'build/desktop-ui/rundir'
    output = ROOT / RANDNET_ARES_DIR
    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(rundir, output, ignore=shutil.ignore_patterns('*.pdb'))
    digest = hashlib.sha256((output / 'ares.exe').read_bytes()).hexdigest()
    print(f'RUNTIME={output}\nEXE_SHA256={digest}')


if __name__ == '__main__':
    main()
