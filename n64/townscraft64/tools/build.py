"""Build libdragon (once) and the Townscraft64 ROM with the pinned toolchain.

Usage:  python tools/build.py [--libdragon] [make targets...]

Requires MSYS2 (UCRT64 environment with gcc and make) for libdragon's host
tools and shell; set MSYS2_ROOT if it is not C:\\msys64. Run
tools/fetch_dependencies.py first.

GNU make and libdragon's n64.mk cannot handle spaces in paths. If the
project path contains one, the build runs through a directory junction in
the temp folder. The toolchain's gcc-ar cannot find its LTO plugin through
a junction, so N64_AR is replaced by plain ar with an explicit --plugin.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MSYS2 = Path(os.environ.get('MSYS2_ROOT', r'C:\msys64'))
GCC_VERSION = '16.2.0'


def space_free_root():
    if ' ' not in str(ROOT):
        return ROOT
    link = Path(tempfile.gettempdir()) / f'townscraft64-{hashlib.sha1(str(ROOT).encode()).hexdigest()[:8]}'
    if ' ' in str(link):
        raise RuntimeError(f'temp folder {link.parent} also contains a space')
    if link.exists():
        if Path(os.path.realpath(link)) != ROOT:
            raise RuntimeError(f'{link} exists and does not point to {ROOT}')
    else:
        subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(ROOT)], check=True,
                       stdout=subprocess.DEVNULL)
    return link


def msys_path(path):
    path = Path(path).resolve() if not str(path).startswith(tempfile.gettempdir()) else Path(path)
    drive, rest = str(path).split(':', 1)
    return f'/{drive.lower()}{rest.replace(os.sep, "/")}'


def run_bash(script):
    env = dict(os.environ, MSYSTEM='UCRT64', CHERE_INVOKING='1')
    return subprocess.run([str(MSYS2 / 'usr/bin/bash.exe'), '-lc', script], env=env).returncode


def main(argv):
    build_libdragon = '--libdragon' in argv
    targets = ' '.join(a for a in argv if a != '--libdragon')
    root = msys_path(space_free_root())
    inst = f'{root}/build/deps/toolchain'
    if not (ROOT / 'build/deps/toolchain/bin').exists():
        sys.exit('toolchain missing: run python tools/fetch_dependencies.py first')
    if not (ROOT / 'build/deps/toolchain/mips64-elf/lib/libdragon.a').exists():
        build_libdragon = True
    ar = (f'{inst}/bin/mips64-elf-ar --plugin '
          f'{inst}/libexec/gcc/mips64-elf/{GCC_VERSION}/liblto_plugin.dll')
    prologue = f'set -e; export N64_INST={inst}; export PATH={inst}/bin:$PATH; '
    if build_libdragon:
        lib = f'{root}/build/deps/libdragon'
        code = run_bash(prologue + f'cd {lib}; make CURDIR={lib} clean; '
                        f'make -j8 CURDIR={lib} "N64_AR={ar}" libdragon tools; '
                        f'make -j8 CURDIR={lib} "N64_AR={ar}" install tools-install')
        if code:
            return code
    return run_bash(prologue + f'cd {root}; make -j8 CURDIR={root} "N64_AR={ar}" {targets}')


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
