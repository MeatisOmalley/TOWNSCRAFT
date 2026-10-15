"""Download the pinned N64 toolchain, libdragon and ares into build/deps.

Nothing is installed system-wide. Archives are checked against the SHA-256
values below before extraction, and libdragon is checked out at an exact
commit. Results are recorded in build/deps/acquired.json.
"""
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


def main():
    results = [fetch_zip('toolchain', *TOOLCHAIN), fetch_zip('ares', *ARES), fetch_libdragon()]
    (DEPS / 'acquired.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
