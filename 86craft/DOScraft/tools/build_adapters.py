"""Compile direct-port platform adapters, not a playable game or optimization."""
import json
from pathlib import Path
import subprocess
import sys
import build_imported as imported

ROOT = imported.ROOT
OUTPUT = ROOT / 'build/pc/486dx25/adapters'
ADAPTERS = ('gfx.c', 'heap.c', 'video.c', 'vga_pack.c', 'keyboard.c', 'irq.c', 'irq_entry.S', 'system.c', 'input.c', 'hdd.c')


def gfx_drawing_source(vendor=imported.VENDOR):
    """Select exact portable drawing bytes; reject an unverified vendor file."""
    vendor = Path(vendor)
    manifest = json.loads((vendor / 'MANIFEST.json').read_bytes())
    data = (vendor / 'src/gfx.c').read_bytes()
    entry = next(e for e in manifest['files'] if e['path'] == 'src/gfx.c')
    if (manifest['source_commit'] != imported.PINNED_COMMIT or
            imported.sha256(data) != entry['sha256'] or len(data) != entry['size']):
        raise ValueError('Pinned gfx.c provenance mismatch')
    clear_start = data.index(b'void gfx_clear(')
    clear_end = data.index(b'/* Show the page just drawn', clear_start)
    draw_start = data.index(b'void gfx_rect(', clear_end)
    portable = data[clear_start:clear_end] + data[draw_start:]
    if b'outb(' in portable or b'VRAM' in portable or b'g_ticks' in portable:
        raise ValueError('Unexpected hardware dependency in drawing selection')
    return portable, dict(source='src/gfx.c', source_sha256=entry['sha256'],
                         staged='src/towns_gfx_draw.inc',
                         staged_sha256=imported.sha256(portable),
                         selections=[[clear_start, clear_end], [draw_start, len(data)]])


def stage(output, vendor=imported.VENDOR):
    output = Path(output)
    # Verify all inputs before creating a compile result. The vendor is read-only.
    portable, selection = gfx_drawing_source(vendor)
    record = imported.stage_sources(vendor, output)
    (output / selection['staged']).write_bytes(portable)
    record['gfx_selection'] = selection
    return record


def build(output=OUTPUT):
    output = Path(output)
    record = stage(output)
    record.update(stage='direct-port adapter objects only', playable=False,
                  linked=False, status='in_progress', commands=[], objects=[])
    (output / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    objects = output / 'obj'
    objects.mkdir(exist_ok=True)
    nm = imported.COMPILER.with_name(imported.COMPILER.name.replace('gcc', 'nm'))
    for name in ADAPTERS:
        source = ROOT / 'src/platform/dos' / name
        obj = objects / (Path(name).stem + '.o')
        command = [imported.COMPILER, *imported.CFLAGS,
                   '-I', output / 'src', '-I', ROOT / 'src/platform/dos',
                   '-c', source, '-o', obj]
        result = subprocess.run(list(map(str, command)), capture_output=True,
                                text=True, errors='replace')
        record['commands'].append(dict(command=list(map(str, command)),
                                       returncode=result.returncode))
        if result.returncode:
            record.update(status='failed', error=result.stderr)
            (output / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
            raise RuntimeError(result.stderr)
        symbols = subprocess.run([str(nm), '-g', str(obj)], check=True,
                                 capture_output=True, text=True).stdout
        record['objects'].append(dict(source=str(source.relative_to(ROOT)),
                                      source_sha256=imported.sha256(source.read_bytes()),
                                      path=f'obj/{obj.name}',
                                      sha256=imported.sha256(obj.read_bytes()),
                                      symbols=imported.parse_nm(symbols),
                                      **imported.coff_object(obj.read_bytes())))
    record['status'] = 'verified_objects'
    (output / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def main():
    try:
        record = build()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'Adapter checkpoint failed: {error}', file=sys.stderr)
        return 1
    print(f'Verified {len(record["objects"])} DOS platform adapter objects; not a linked game')
    return 0


if __name__ == '__main__':
    sys.exit(main())
