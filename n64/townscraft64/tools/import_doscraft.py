"""Import the pinned DOScraft snapshot (and the pinned Towns tests) into vendor/.

Files are read from git objects at the pinned commits, never from a working
tree, so local edits in 86craft/DOScraft or fmtowns/townscraft cannot leak in.
Each imported file is listed in vendor/<name>/MANIFEST.json with its git blob,
SHA-256 and size. Re-running rewrites the two vendor directories exactly.

Usage: python tools/import_doscraft.py [--verify]
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / 'vendor'

# DOScraft master on 2026-10-15, 06:02: "DOScraft: record save version 6 and
# the block-ID release" (6-bit block IDs, large-world save version 6).
# Reference game for Townscraft64 (Q1). Re-pinned from 2ce6b443 at N2b,
# before any extraction depended on the older 5-bit block format.
DOSCRAFT_COMMIT = 'c4ce3b7b2638fd6ad62f1cb975786bd4fa9bda00'
DOSCRAFT_ROOT = '86craft/DOScraft'
# Game source, overlay patchers and offline tools, art/audio/music sources and
# design notes. Not imported: tests (22 MB of fixtures), research, ISO,
# profiles, guest and emulator patches, which are PC-specific.
DOSCRAFT_INCLUDE = ('vendor/', 'src/', 'tools/', 'assets/')
# DOScraft native tests for code the N64 runs (and their drivers, which record
# how each test stages the source). The rest of tests/ is PC-specific.
DOSCRAFT_TESTS = tuple(f'tests/{name}' for name in (
    'chickens_native.c', 'test_chickens.py',
    'mob_codec_test.c', 'test_mob_codec.py',
    'immediate_light_native.c', 'test_immediate_light.py',
    'stream_light_native.c', 'test_stream_light.py',
    'far_world_native.c', 'test_far_field.py'))
DOSCRAFT_DOCS = ('README.md', 'PORT_PLAN.md', 'BIOMES_AND_TEXTURES.md', 'CHICKENS.md',
                 'DISTANT_TERRAIN.md', 'LARGE_WORLD_STORAGE.md', 'KNOWN_PORT_ISSUES.md',
                 'OPTIMIZATION_BACKLOG.md', 'PERFORMANCE.md', 'EARLY_GAME.md')

# The Towns snapshot DOScraft vendors; its native tests are not in that snapshot.
TOWNS_COMMIT = 'd706a67db6242627c6233e5593fd1a1e1ddc69af'
TOWNS_ROOT = 'fmtowns/townscraft'
TOWNS_TESTS = ('tests/',)
TOWNS_TEST_SUFFIXES = ('.c', '.inc')


def git(*args, binary=False):
    result = subprocess.run(['git', '-C', str(ROOT), *args], check=True, capture_output=True)
    return result.stdout if binary else result.stdout.decode('utf-8')


def tree(commit, root):
    """(relative path, blob id, size) for every file under root at commit."""
    rows = []
    # --full-tree: root is repository-relative, whatever directory git runs in.
    for line in git('ls-tree', '--full-tree', '-r', '-l', '-z', commit, '--', root).split('\0'):
        if not line:
            continue
        meta, path = line.split('\t', 1)
        mode, kind, blob, size = meta.split()
        if kind == 'blob':
            rows.append((path[len(root) + 1:], blob, int(size), mode))
    return rows


def select_doscraft(rel):
    return rel.startswith(DOSCRAFT_INCLUDE) or rel in DOSCRAFT_DOCS or rel in DOSCRAFT_TESTS


def select_towns(rel):
    return rel.startswith(TOWNS_TESTS) and rel.endswith(TOWNS_TEST_SUFFIXES)


def import_tree(name, commit, root, select):
    dest = VENDOR / name
    rows = [row for row in tree(commit, root) if select(row[0])]
    if not rows:
        raise RuntimeError(f'{name}: nothing selected at {commit}')
    files = []
    staged = {}
    for rel, blob, size, mode in rows:
        data = git('cat-file', 'blob', blob, binary=True)
        if len(data) != size:
            raise RuntimeError(f'{name}: size mismatch for {rel}')
        staged[rel] = data
        files.append(dict(path=rel, source_path=f'{root}/{rel}', git_blob=blob, git_mode=mode,
                          sha256=hashlib.sha256(data).hexdigest(), size=size))
    if dest.exists():
        shutil.rmtree(dest)
    for rel, data in staged.items():
        path = dest / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    manifest = dict(schema_version=1, source_commit=commit, source_root=root, files=files)
    (dest / 'MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'{name}: {len(files)} files, {sum(f["size"] for f in files)} bytes from {commit[:8]}')


def verify(name):
    dest = VENDOR / name
    manifest = json.loads((dest / 'MANIFEST.json').read_text(encoding='utf-8'))
    listed = set()
    for entry in manifest['files']:
        data = (dest / entry['path']).read_bytes()
        if hashlib.sha256(data).hexdigest() != entry['sha256'] or len(data) != entry['size']:
            raise RuntimeError(f'{name}: {entry["path"]} differs from manifest')
        listed.add(entry['path'])
    extra = {p.relative_to(dest).as_posix() for p in dest.rglob('*') if p.is_file()}
    extra -= listed | {'MANIFEST.json'}
    if extra:
        raise RuntimeError(f'{name}: unlisted files {sorted(extra)[:5]}')
    print(f'{name}: {len(listed)} files match MANIFEST.json')


def main(argv):
    if '--verify' not in argv:
        import_tree('doscraft', DOSCRAFT_COMMIT, DOSCRAFT_ROOT, select_doscraft)
        import_tree('towns-tests', TOWNS_COMMIT, TOWNS_ROOT, select_towns)
    verify('doscraft')
    verify('towns-tests')


if __name__ == '__main__':
    main(sys.argv[1:])
