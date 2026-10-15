"""Download the pinned N64 toolchain, libdragon and ares into build/deps.

Nothing is installed system-wide. Archives are checked against the SHA-256
values below before extraction, and libdragon is checked out at an exact
commit. Results are recorded in build/deps/acquired.json.

--randnet-ares also checks the ares fork with the Randnet keyboard (built
locally by tools/build_randnet_ares.py; it has no download) against its pin
and records it. Stock ares v148 stays the default emulator.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / 'build/deps'

# libdragon's Windows MIPS GCC build. The release tag is "continuous": the
# asset is replaced when the toolchain changes, so a hash mismatch means a
# new upload, not corruption. Re-pin deliberately (this one is the
# 2026-09-15 upload, built with the VR4300 mulmul fix).
TOOLCHAIN = ('https://github.com/DragonMinded/libdragon/releases/download/'
             'toolchain-continuous-prerelease/gcc-toolchain-mips64-win64.zip',
             'bd201c35afee4aceb9d2a80c6dc9d25eb2a4d426b1b26ef38e33881641b4d1d0')
# ares v148 (2026-05-30): N64 Gamepad, Mouse and 64DD; no Randnet keyboard.
ARES = ('https://github.com/ares-emulator/ares/releases/download/v148/ares-windows-x64.zip',
        'fa97958e1d359e3fe254353f87b6508f2762a959d7eb264c69965ad002037298')
# libdragon "preview" branch head, 2026-09-14.
LIBDRAGON_REPO = 'https://github.com/DragonMinded/libdragon.git'
LIBDRAGON_COMMIT = '39d0d6096130836a65710da6730290701d060272'
# ares fork: v148 plus patches/ares-v148-randnet-keyboard.patch, on a local
# branch of a clone of the upstream repository (not published).
RANDNET_ARES_UPSTREAM = 'https://github.com/ares-emulator/ares.git'
RANDNET_ARES_BRANCH = 'townscraft64-randnet-keyboard'
RANDNET_ARES_COMMIT = 'a69c076b5b7c74cf03bad23018f0cb29ec534cb1'
RANDNET_ARES_DIR = 'build/deps/ares-randnet'
RANDNET_ARES_EXE_SHA256 = 'a6f256df910dc368dbdf47581362e1c4e60b8a75c1b6b2aaa908b9dfb7008615'


def fetch_zip(name, url, expected):
    dest = DEPS / name
    dest.mkdir(parents=True, exist_ok=True)
    archive = dest / 'download.zip'
    if not archive.exists():
        partial = archive.with_suffix('.part')
        request = urllib.request.Request(url, headers={'User-Agent': 'Townscraft64-development'})
        with urllib.request.urlopen(request, timeout=120) as response, partial.open('wb') as output:
            while data := response.read(1 << 20):
                output.write(data)
        partial.replace(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != expected:
        raise RuntimeError(f'{name}: checksum {digest} does not match pin; not extracted')
    with zipfile.ZipFile(archive) as zipped:
        for info in zipped.infolist():
            if not (dest / info.filename).resolve().is_relative_to(dest.resolve()):
                raise RuntimeError(f'{name}: unsafe archive path {info.filename}')
        zipped.extractall(dest)
    print(f'{name}: {digest}', flush=True)
    return dict(name=name, url=url, sha256=digest, bytes=archive.stat().st_size)


def fetch_libdragon():
    dest = DEPS / 'libdragon'
    if not (dest / '.git').exists():
        subprocess.run(['git', 'clone', '--filter=blob:none', '--no-checkout',
                        LIBDRAGON_REPO, str(dest)], check=True)
    subprocess.run(['git', '-C', str(dest), 'fetch', 'origin', LIBDRAGON_COMMIT], check=True)
    subprocess.run(['git', '-C', str(dest), '-c', 'advice.detachedHead=false',
                    'checkout', '--force', LIBDRAGON_COMMIT], check=True)
    head = subprocess.run(['git', '-C', str(dest), 'rev-parse', 'HEAD'], check=True,
                          capture_output=True, text=True).stdout.strip()
    if head != LIBDRAGON_COMMIT:
        raise RuntimeError(f'libdragon: checked out {head}, expected {LIBDRAGON_COMMIT}')
    print(f'libdragon: {head}', flush=True)
    return dict(name='libdragon', url=LIBDRAGON_REPO, commit=head)


def check_randnet_ares():
    exe = ROOT / RANDNET_ARES_DIR / 'ares.exe'
    if not exe.is_file():
        raise RuntimeError(f'{exe} missing: build it with tools/build_randnet_ares.py')
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    if digest != RANDNET_ARES_EXE_SHA256:
        raise RuntimeError(f'ares-randnet: ares.exe {digest} does not match pin')
    print(f'ares-randnet: {digest}', flush=True)
    return dict(name='ares-randnet', upstream=RANDNET_ARES_UPSTREAM, branch=RANDNET_ARES_BRANCH,
                commit=RANDNET_ARES_COMMIT, exe_sha256=digest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--randnet-ares', action='store_true',
                        help='also check and record the locally built ares fork')
    args = parser.parse_args()
    results = [fetch_zip('toolchain', *TOOLCHAIN), fetch_zip('ares', *ARES), fetch_libdragon()]
    if args.randnet_ares:
        results.append(check_randnet_ares())
    (DEPS / 'acquired.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
