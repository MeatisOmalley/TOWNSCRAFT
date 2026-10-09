"""Compile the immutable Towns import for i486 DJGPP; no link or guest run.

Only the explicitly listed inputs are staged. Game/math/rasterizer bodies are
copied verbatim; the bounded adaptations below are DOS declarations and COFF
metadata/symbol spelling. Platform calls intentionally remain unresolved.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / 'vendor/towns'
OUTPUT = ROOT / 'build/pc/486dx25/imported'
COMPILER = ROOT / 'build/deps/djgpp/djgpp/bin/i586-pc-msdosdjgpp-gcc.exe'
PINNED_COMMIT = 'd706a67db6242627c6233e5593fd1a1e1ddc69af'
PORTABLE_C = tuple(f'{name}.c' for name in (
    'blocks', 'fmath', 'font', 'game', 'inventory', 'mobs', 'physics',
    'player', 'raster', 'render', 'textures', 'ui', 'world', 'libc'))
HEADERS = tuple(f'{name}.h' for name in (
    'bench', 'blocks', 'common', 'fmath', 'game', 'gfx', 'hdd', 'inventory',
    'mobs', 'physics', 'player', 'raster', 'render', 'save', 'sound', 'sys',
    'textures', 'ui', 'video', 'world'))
INCLUDES = ('column_store.inc', 'column_hdd.inc', 'column_cache.inc')
SELECTED = PORTABLE_C + HEADERS + INCLUDES + ('trap.S',)
EXCLUDED = ('bench.c', 'sys.c', 'video.c', 'gfx.c', 'save.c', 'sound.c',
            'hdd.c', 'crt0.S', 'isr.S', 'hw.h', 'kernel.ld')
TRAP_SYMBOLS = tuple(f'trap_{kind}{scale}{wrap}' for wrap in ('', 'w')
                     for kind in ('opaque', 'transp') for scale in (1, 2))

# Preserve the Towns integer-only compilation contract, changing CPU to i486.
# In particular fno-builtin prevents our unchanged libc loops becoming calls
# back to themselves; no fast-math, LTO, alternative math, or new game defines.
CFLAGS = (
    '-march=i486', '-mtune=i486', '-O2', '-ffreestanding', '-fno-pic',
    '-fno-pie', '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
    '-fno-builtin', '-mno-80387', '-mno-mmx', '-mno-sse', '-mgeneral-regs-only',
    '-fno-tree-loop-distribute-patterns', '-Wall', '-Wextra',
    '-Wno-unused-function', '-std=gnu99', '-Werror=implicit-function-declaration',
)
ASFLAGS = ('-march=i486', '-mtune=i486', '-O2')

DOS_COMPAT = '''/* Generated declaration-only DOS compatibility, no Towns I/O. */
#ifndef DOSCRAFT_IMPORTED_COMPAT_H
#define DOSCRAFT_IMPORTED_COMPAT_H
#include <stddef.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef short s16;
typedef int s32;
typedef char dos_u8_width[(sizeof(u8)==1) ? 1 : -1];
typedef char dos_u16_width[(sizeof(u16)==2) ? 1 : -1];
typedef char dos_u32_width[(sizeof(u32)==4) ? 1 : -1];
typedef char dos_int_width[(sizeof(int)==4) ? 1 : -1];
typedef char dos_long_width[(sizeof(long)==4) ? 1 : -1];
typedef char dos_pointer_width[(sizeof(void *)==4) ? 1 : -1];
typedef char dos_size_width[(sizeof(size_t)==4) ? 1 : -1];
#endif
'''
LIBC_SIGNATURES = (
    ('void *memset(void *d,int c,unsigned int n)',
     'void *memset(void *d,int c,size_t n)'),
    ('void *memcpy(void *d,const void *s,unsigned int n)',
     'void *memcpy(void *d,const void *s,size_t n)'),
    ('void *memmove(void *d,const void *s,unsigned int n)',
     'void *memmove(void *d,const void *s,size_t n)'),
    ('int memcmp(const void *a,const void *b,unsigned int n)',
     'int memcmp(const void *a,const void *b,size_t n)'),
    ('int strlen(const char *s)', 'size_t strlen(const char *s)'),
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode('utf-8')


def replace_once(text, before, after, recipe):
    """Fail closed on missing/duplicated expected source, recording exact edits."""
    count = text.count(before)
    if count != 1:
        raise ValueError(f'Expected exactly one {before!r}; found {count}')
    recipe.append(dict(operation='replace', before=before, after=after, count=1))
    return text.replace(before, after, 1)


def adapt_source(name, data):
    if name not in ('common.h', 'hdd.h', 'libc.c', 'trap.S'):
        return data, []
    text = data.decode('utf-8')
    recipe = []
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n',
                           count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    if name == 'common.h':
        text = replace_once(text, '#include "hw.h"',
                            '#include "dos_compat.h"', recipe)
        text = replace_once(text, '#define NULL ((void *)0)\n', '', recipe)
        for before, _ in LIBC_SIGNATURES:
            text = replace_once(text, before + ';\n', '', recipe)
    elif name == 'hdd.h':
        # Keep the block API required by world.c, without importing port I/O.
        text = replace_once(text, '#include "hw.h"', '#include "common.h"', recipe)
    elif name == 'libc.c':
        for before, after in LIBC_SIGNATURES:
            text = replace_once(text, before + '\n', after + '\n', recipe)
    else:
        # Rename only the eight macro invocations exporting C functions.
        # Local labels, instructions, offsets, and macro bodies are untouched.
        for symbol in TRAP_SYMBOLS:
            text = replace_once(text, f'TRAP {symbol},', f'TRAP _{symbol},', recipe)
        text = replace_once(text, 'g_recip14(,%ecx,4)', '_g_recip14(,%ecx,4)', recipe)
        # ELF-only metadata is removable only in these explicit forms.
        for line in text.splitlines(keepends=True):
            stripped = line.strip()
            if stripped.startswith(('.type', '.size')):
                allowed = any(re.fullmatch(
                    rf'\.type\s+{symbol}\s*,\s*@function|'
                    rf'\.size\s+{symbol}\s*,\s*\.\s*-\s*{symbol}', stripped)
                    for symbol in TRAP_SYMBOLS)
                if not allowed:
                    raise ValueError(f'Unrecognized ELF metadata: {stripped}')
                text = replace_once(text, line, '', recipe)
            elif '.note' in stripped and stripped.startswith('.section'):
                if stripped != '.section .note.GNU-stack,"",@progbits':
                    raise ValueError(f'Unrecognized ELF note: {stripped}')
                text = replace_once(text, line, '', recipe)
    return text.encode('utf-8'), recipe


def abi_probe():
    """Check the real private raster.c Trap layout against trap.S's offsets."""
    fields = ('row', 'stride', 'rows', 'lx', 'ldx', 'lu', 'ldu', 'lv', 'ldv',
              'rx', 'rdx', 'ru', 'rdu', 'rv', 'rdv', 'tile', 'maxX', 'pixels')
    lines = ['/* Compile-time ABI check only; never linked. */',
             '#include "raster.c"']
    for index, field in enumerate(fields):
        lines.append(f'typedef char trap_offset_{field}['
                     f'(offsetof(Trap,{field})=={index * 4}) ? 1 : -1];')
    lines.append('typedef char trap_size[(sizeof(Trap)==72) ? 1 : -1];')
    return ('\n'.join(lines) + '\n').encode('ascii')


def stage_sources(vendor, output):
    """Validate pinned provenance before writing anything; never mutate vendor."""
    manifest_data = (vendor / 'MANIFEST.json').read_bytes()
    provenance = json.loads(manifest_data)
    if provenance.get('source_commit') != PINNED_COMMIT:
        raise ValueError('Vendor snapshot is not the pinned Towns HEAD')
    expected = {entry['path']: entry for entry in provenance['files']}
    inputs = {}
    for relative in [*(f'src/{name}' for name in SELECTED), 'tools/gentables.py']:
        data = (vendor / relative).read_bytes()
        entry = expected.get(relative)
        if not entry or entry['sha256'] != sha256(data) or entry['size'] != len(data):
            raise ValueError(f'Vendor checksum mismatch: {relative}')
        inputs[relative] = data
    staged = []
    # Prepare every adaptation first, so an unexpected source cannot half-stage.
    for name in SELECTED:
        data = inputs[f'src/{name}']
        result, recipe = adapt_source(name, data)
        staged.append((name, result, dict(
            source=f'src/{name}', staged=f'src/{name}',
            source_sha256=sha256(data), staged_sha256=sha256(result),
            transformations=recipe, transformations_sha256=sha256(canonical_json(recipe)))))
    source_dir = output / 'src'
    source_dir.mkdir(parents=True, exist_ok=True)
    records = []
    for name, result, record in staged:
        (source_dir / name).write_bytes(result)
        records.append(record)
    generated = []
    for name, data in (('dos_compat.h', DOS_COMPAT.encode('ascii')),
                       ('abi_check.c', abi_probe())):
        (source_dir / name).write_bytes(data)
        generated.append(dict(staged=f'src/{name}', sha256=sha256(data),
                              exact_content=data.decode('ascii')))
    generator = output / 'tools/gentables.py'
    generator.parent.mkdir(parents=True, exist_ok=True)
    generator.write_bytes(inputs['tools/gentables.py'])
    return dict(source_commit=PINNED_COMMIT, vendor_directory=str(vendor.resolve()),
                vendor_manifest_sha256=sha256(manifest_data), files=records,
                generated=generated, excluded=list(EXCLUDED),
                generator_sha256=sha256(inputs['tools/gentables.py']))


def coff_object(data):
    # Raw 32-bit i386 COFF, not ELF, PE executable, or an empty placeholder.
    if len(data) < 20:
        raise ValueError('Truncated COFF object')
    machine, sections, _, _, _, optional, flags = struct.unpack_from('<HHIIIHH', data)
    if machine != 0x14c or not sections or optional or flags & 0x0002:
        raise ValueError('Expected relocatable i386 COFF object')
    return dict(format='coff-i386', sections=sections)


def parse_nm(text):
    result = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 2 and len(fields[-2]) == 1:
            result[fields[-1]] = fields[-2]
    return result


def verify_symbols(symbols):
    trap, raster = symbols['trap.o'], symbols['raster.o']
    for name in TRAP_SYMBOLS:
        if trap.get('_' + name) != 'T' or raster.get('_' + name) != 'U':
            raise ValueError(f'COFF trap ABI mismatch: {name}')
        if name in trap or name in raster:
            raise ValueError(f'Undecorated trap symbol: {name}')
    if trap.get('_g_recip14') != 'U' or raster.get('_g_recip14') not in ('B', 'C', 'D'):
        raise ValueError('COFF reciprocal table ABI mismatch')
    for name in ('memset', 'memcpy', 'memmove', 'memcmp', 'strlen',
                 'itoa_dec', 'rnd', 'rnd_seed', 'rnd_range', 'hash3'):
        if symbols['libc.o'].get('_' + name) != 'T':
            raise ValueError(f'Missing imported libc API: {name}')
    for name in ('g_sinTab', 'g_atanTab'):
        # DJGPP COFF places const data in .text (nm T); ELF commonly uses R.
        if symbols['tables.o'].get('_' + name) not in ('R', 'D', 'T'):
            raise ValueError(f'Missing generated table: {name}')


def build(vendor=VENDOR, output=OUTPUT, compiler=COMPILER):
    vendor, output, compiler = map(lambda p: Path(p).resolve(), (vendor, output, compiler))
    # CLI always uses the ignored checkpoint directory; callers/tests may use
    # private temporary directories. No cleanup of other agents' build files.
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / 'build.json'
    report = dict(stage='direct-port compiler/ABI checkpoint only', target='486dx25',
                  status='in_progress', linked=False, executable=False, playable=False,
                  cflags=list(CFLAGS), commands=[], objects=[])

    def save_report():
        manifest_path.write_bytes((json.dumps(report, indent=2) + '\n').encode('utf-8'))

    def run(command, label):
        command = list(map(str, command))
        process = subprocess.run(command, capture_output=True, text=True, errors='replace')
        logs = output / 'logs'
        logs.mkdir(exist_ok=True)
        for stream in ('stdout', 'stderr'):
            (logs / f'{label}.{stream}.txt').write_bytes(getattr(process, stream).encode('utf-8'))
        report['commands'].append(dict(command=command, returncode=process.returncode,
                                       stdout=f'logs/{label}.stdout.txt',
                                       stderr=f'logs/{label}.stderr.txt'))
        if process.returncode:
            raise RuntimeError(f'{label} failed ({process.returncode}): {process.stderr.strip()}')
        return process.stdout

    save_report()
    try:
        report.update(stage_sources(vendor, output))
        if not compiler.is_file():
            raise FileNotFoundError(f'DJGPP compiler missing: {compiler}')
        nm = compiler.with_name(compiler.name.replace('gcc', 'nm'))
        if not nm.is_file():
            raise FileNotFoundError(f'DJGPP nm missing: {nm}')
        report['compiler_sha256'] = sha256(compiler.read_bytes())
        report['nm_sha256'] = sha256(nm.read_bytes())
        report['compiler_version'] = run([compiler, '--version'], 'compiler-version').strip()
        if run([compiler, '-dumpmachine'], 'compiler-target').strip() != 'i586-pc-msdosdjgpp':
            raise ValueError('Compiler is not the DJGPP target')
        source_dir = output / 'src'
        tables = source_dir / 'tables.c'
        # Remove only our known generated table to prevent a stale success.
        if tables.exists():
            tables.unlink()
        run([sys.executable, output / 'tools/gentables.py', tables], 'generate-tables')
        report['generated'].append(dict(staged='src/tables.c', sha256=sha256(tables.read_bytes()),
                                         generator='tools/gentables.py'))
        run([compiler, *CFLAGS, '-fsyntax-only', source_dir / 'abi_check.c'], 'abi-check')
        report['trap_layout_verified'] = True
        object_dir = output / 'obj'
        object_dir.mkdir(exist_ok=True)
        symbols = {}
        for name in (*PORTABLE_C, 'tables.c', 'trap.S'):
            obj = object_dir / (Path(name).stem + '.o')
            if obj.exists():
                obj.unlink()
            flags = ASFLAGS if name.endswith('.S') else CFLAGS
            run([compiler, *flags, '-c', source_dir / name, '-o', obj], obj.stem)
            metadata = coff_object(obj.read_bytes())
            symbols[obj.name] = parse_nm(run([nm, '-g', obj], obj.stem + '-symbols'))
            report['objects'].append(dict(path=f'obj/{obj.name}', sha256=sha256(obj.read_bytes()),
                                          **metadata, symbols=symbols[obj.name]))
        verify_symbols(symbols)
        defined = {name for group in symbols.values() for name, kind in group.items() if kind != 'U'}
        report['unresolved_symbols'] = sorted({name for group in symbols.values()
                                               for name, kind in group.items()
                                               if kind == 'U' and name not in defined})
        report['limitations'] = ('Objects only. Platform/benchmark services remain unresolved; '
                                 'no link, DOS boot, rendering, or gameplay validation performed.')
        # Re-read inputs after compile to catch concurrent edits to the snapshot.
        for entry in report['files']:
            if sha256((vendor / entry['source']).read_bytes()) != entry['source_sha256']:
                raise ValueError(f'Vendor changed during build: {entry["source"]}')
        if sha256((vendor / 'tools/gentables.py').read_bytes()) != report['generator_sha256']:
            raise ValueError('Vendor generator changed during build')
        report['status'] = 'verified_objects'
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = str(error)
        raise
    finally:
        save_report()
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    try:
        report = build()
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Compile-only checkpoint failed: {error}', file=sys.stderr)
        return 1
    print(f'Verified {len(report["objects"])} i486 DJGPP COFF objects in {OUTPUT / "obj"}')
    print(f'Manifest: {OUTPUT / "build.json"}')
    print('Compile-only: no executable, playable game, or guest validation claimed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
