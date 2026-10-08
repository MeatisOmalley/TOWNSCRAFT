"""Compare historical source snapshots in Tsugaru using emulated frame times.

Example (from townscraft):
  python tools/perf_audit.py --name merged --ref 159d95f2
Outputs stay under ignored build/perf-audit/. Production sources are untouched.
Use --ref WORKTREE to measure current files, and --build-only for no emulator.
"""
import argparse
import hashlib
import io
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'build/tools/python'))
from elftools.elf.elffile import ELFFile


def checked(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def build(a, out):
    # Fresh extraction prevents newer .c files leaking into older builds when
    # the same output name is reused for a different revision.
    src = Path(tempfile.mkdtemp(prefix='snapshot-', dir=out))
    if a.ref == 'WORKTREE':
        src.mkdir(exist_ok=True)
        for name in ('src', 'boot', 'tools'):
            shutil.copytree(ROOT / name, src / name, dirs_exist_ok=True)
        shutil.copy2(ROOT / 'Makefile', src / 'Makefile')
    else:
        repo = checked(['git', 'rev-parse', '--show-toplevel'], cwd=ROOT,
                       capture_output=True, text=True).stdout.strip()
        prefix = 'fmtowns/townscraft'
        exists = subprocess.run(['git', 'cat-file', '-e', a.ref + ':' + prefix + '/src'],
                                cwd=repo, capture_output=True).returncode == 0
        if not exists:
            prefix = 'townscraft'
        archive = checked(['git', 'archive', a.ref, prefix], cwd=repo,
                          capture_output=True).stdout
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            for member in tar:
                if member.isfile():
                    relative = Path(member.name).relative_to(prefix)
                    target = src / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(tar.extractfile(member).read())
    game = src / 'src/game.c'
    code = game.read_text()
    if a.production:
        compile_image(a, out, src)
        return
    if a.standard:
        world = src / 'src/world.c'
        wcode = world.read_text()
        cached = 'cacheAllocated' in wcode
        streaming = 'void world_stream(' in wcode
        assert a.width >= 80 and a.width % 16 == 0
        # Common finite extent; do not let code growth choose a different scene.
        marker = 'g_NC=g_W/CS;'
        assert wcode.count(marker) == 1
        wcode = wcode.replace(marker, f'g_W={a.width}; ' + marker)
        wcode = wcode.replace('void world_generate(u32 seed)', 'static void unused_world_generate(u32 seed)')
        level = (ROOT / 'tests/standard_level.inc').read_text()
        begin = 'if(cacheAllocated) generation_begin();' if cached else ''
        if 'static u32 genSeed;' in wcode: begin += 'genSeed=seed;'
        if 'static u32 genVersion;' in wcode: begin += 'genVersion=1;'
        end = ('if(!generation_finish()) fatal("Standard level backing full");' if cached else
               'light_init(); mesh_reset();' if streaming else
               'light_init(); poolTop=0; g_meshQuads=0; '
               'for(int cz=0;cz<g_NC;++cz) for(int cx=0;cx<g_NC;++cx) '
               'for(int cy=0;cy<NCY;++cy) rebuild_chunk(cx,cy,cz);')
        level = level.replace('STD_GENERATION_BEGIN', begin).replace('STD_GENERATION_END', end)
        world.write_text(wcode + '\n' + level)
    if a.mesh_probe:
        world = src / 'src/world.c'
        wcode = world.read_text()
        cached = 'cacheAllocated' in wcode
        probe = (ROOT / 'tests/mesh_value_probe.inc').read_text()
        replace = {
            'AUDIT_MEMORY': ('auditMemory[5]=COLUMN_CELLS*g_residentColumns*2; '
                             'auditMemory[6]=columnCapacity; auditMemory[7]=world_store_used(); '
                             'auditMemory[8]=(g_residentColumns+1)*CS*CS; '
                             'auditMemory[9]=sizeof(Chunk)*g_allocChunkCount; '
                             'auditMemory[10]=COLUMN_CELLS*2' if cached else
                             'auditMemory[5]=g_W*g_W*WH*2; auditMemory[8]=g_W*g_W; '
                             'auditMemory[9]=sizeof(Chunk)*g_NC*g_NC*NCY'),
            'AUDIT_SETTLE': 'cache_light_pump(0); cache_stream(40,40,100000); cache_light_pump(0)' if cached else '(void)0',
            'AUDIT_READY': 'world_column_ready(x/CS,z/CS)' if cached else '1',
            'AUDIT_HINDEX': 'height_index(x,z)' if cached else 'z*g_W+x',
            'AUDIT_FULL': 'build_chunk(cx,cy,cz)' if cached else 'rebuild_chunk(cx,cy,cz,0xffff)',
            'AUDIT_LAYERS': ('__builtin_popcount((unsigned)c->dirtyLayers)' if
                             'dirtyLayers;' in (src / 'src/world.h').read_text() else '16'),
            'AUDIT_BUDGET': '1' if cached else '48',
            'AUDIT_LINEAR': ('cacheAllocated=0; g_columnMap=NULL; g_columnReady=NULL; '
                             'g_W=80; g_NC=5; strideZ=g_W*WH; g_allocChunkCount=75; '
                             'for(int z=0;z<g_W;++z) g_zOff[z]=z*strideZ' if a.linear_test else
                             'g_W=80; g_NC=5; strideZ=g_W*WH; '
                             'for(int z=0;z<g_W;++z) g_zOff[z]=z*strideZ; '
                             'memset(columnMapStorage,0xff,sizeof(columnMapStorage)); '
                             'for(int s=0;s<g_residentColumns;++s) { '
                             'int cx=g_columnCoords[s*2],cz=g_columnCoords[s*2+1]; '
                             'if(cx>=0 && cz>=0 && cx<g_NC && cz<g_NC) '
                             'g_columnMap[cz*g_NC+cx]=s; }' if cached else
                             'g_W=80; g_NC=5; strideZ=g_W*WH; '
                             'for(int z=0;z<g_W;++z) g_zOff[z]=z*strideZ'),
        }
        assert not a.linear_test or cached
        for key, value in replace.items(): probe = probe.replace(key, value)
        if a.sync_mesh:
            assert cached
            marker = 'if(cacheAllocated) mesh_step(cx,cy,cz);'
            assert wcode.count(marker) == 1
            wcode = wcode.replace(marker, 'if(cacheAllocated) build_chunk(cx,cy,cz);')
        if a.full_mesh:
            assert not cached
            marker = 'rebuild_chunk(cx,cy,cz,c->dirtyLayers);'
            assert wcode.count(marker) == 1
            wcode = wcode.replace(marker, 'rebuild_chunk(cx,cy,cz,0xffff);')
        world.write_text(wcode + '\n' + probe)
        code = code.replace('void kmain(void)', 'extern void audit_world_probe(void);\nvoid kmain(void)')
    probe = ((ROOT / 'tests/perf_probe.inc').read_text() if not a.mesh_probe else
             'u32 auditUpdate,auditRender;\nstatic void audit_tick(void) { '
             'if(GS_TITLE==state) { new_game(4242); audit_world_probe(); state=GS_PLAY; } }\n')
    if a.standard:
        probe = (ROOT / 'tests/standard_probe.inc').read_text()
        values = dict(STD_SCALE=str(a.scale), STD_VIEW=str(a.view), STD_ONLY=str(a.phase), STD_MOBS=str(a.mobs),
                      STD_INTERLACE='g_interlace=0' if 'g_interlace' in code else '(void)0',
                      STD_IS_MESHED='c->meshed' if 'meshed;' in (src / 'src/world.h').read_text() else '1')
        for key, value in values.items(): probe = probe.replace(key, value)
    probe = probe.replace('AUDIT_INTERLACE_OFF', 'g_interlace=0' if 'g_interlace' in code else '(void)0')
    probe = probe.replace('AUDIT_UNMESHED', '!c->meshed' if 'meshed;' in (src / 'src/world.h').read_text() else '0')
    assert code.count('void kmain(void)') == 1
    code = code.replace('void kmain(void)', probe + '\nvoid kmain(void)')
    assert code.count('u32 now=g_ticks,elapsed=now-lastTick;') == 1
    code = code.replace('u32 now=g_ticks,elapsed=now-lastTick;', 'audit_tick();\n\t\tu32 now=g_ticks,elapsed=now-lastTick;')
    code = code.replace('tickAccum+=elapsed;', 'tickAccum=0; /* controlled poses, frozen simulation */')
    markers = re.findall(r'world_update_dirty_chunks\(\d+\);', code)
    assert len(markers) == 1, markers
    marker = markers[0]
    pos = code.index(marker)
    line = code.rfind('\n', 0, pos)
    # Include the declaration "int meshWorked=" when present.
    clock = 'std_us()' if a.standard else 'g_ticks'
    code = code[:line] + '\n\t\t\tu32 auditU0=' + clock + ';' + code[line:]
    pos = code.index(marker)
    end = code.index('\n\t\t}', pos)
    code = code[:end] + '\n\t\t\tauditUpdate=' + clock + '-auditU0;' + code[end:]
    assert code.count('draw_world();') == 1
    code = code.replace('draw_world();', 'u32 auditR0='+clock+'; draw_world(); auditRender='+clock+'-auditR0;')
    game.write_text(code)
    compile_image(a, out, src)


def compile_image(a, out, src):
    tables = out / 'tables.c'
    checked([sys.executable, src / 'tools/gentables.py', tables])
    zig = Path(a.zig).resolve()
    flags = ['-target', 'x86-freestanding-none', '-mcpu=i386', '-O2', '-g',
             '-ffreestanding', '-fno-pic', '-fno-pie', '-fno-stack-protector',
             '-fno-asynchronous-unwind-tables', '-fno-builtin', '-mno-80387',
             '-mno-mmx', '-mno-sse', '-std=gnu99', '-I', str(src / 'src')]
    flags += ['-D' + d for d in a.define]
    make = (src / 'Makefile').read_text()
    objline = next(line for line in make.splitlines() if line.startswith('OBJS '))
    assembly = [src / ('src/' + name + '.S') for name in ('crt0', 'isr', 'span', 'trap')
                if name + '.o' in objline]
    inputs = assembly + [tables] + sorted((src / 'src').glob('*.c'))
    objects = []
    for path in inputs:
        obj = out / (path.stem + '.o')
        checked([zig, 'cc', *flags, '-c', path, '-o', obj])
        objects.append(obj)
    elf = out / 'kernel.elf'
    checked([zig, 'ld.lld', '-m', 'elf_i386', '-T', src / 'src/kernel.ld', '-o', elf, *objects])
    # ELF extraction retains all loadable code/data; repeated Zig objcopy -j
    # options select only the last section and would produce a broken image.
    with elf.open('rb') as f:
        e = ELFFile(f)
        sections = [s for s in e.iter_sections() if s['sh_type'] == 'SHT_PROGBITS'
                    and s['sh_flags'] & 2 and s['sh_size']]
        base = min(s['sh_addr'] for s in sections)
        image = bytearray(max(s['sh_addr'] + s['sh_size'] for s in sections) - base)
        for s in sections:
            offset = s['sh_addr'] - base
            image[offset:offset+s['sh_size']] = s.data()
    (out / 'kernel.bin').write_bytes(image)
    checked([zig, 'cc', '-target', 'x86-freestanding-none', '-c', src / 'boot/ipl.S', '-o', out / 'ipl.o'])
    checked([zig, 'ld.lld', '-m', 'elf_i386', '-Ttext=0', '--oformat=binary', '-o', out / 'ipl.bin', out / 'ipl.o'])
    checked([sys.executable, src / 'tools/mkimage.py', out / 'ipl.bin', out / 'kernel.bin', out / 'audit.ISO'])


def stats(values, frame=False):
    v = sorted(values)
    result = dict(median_ms=v[len(v)//2]*10, p95_ms=v[int(len(v)*.95)]*10,
                  max_ms=max(v)*10, mean_ms=sum(v)/len(v)*10)
    if frame: result['fps'] = len(v)*100/sum(v)
    return result


def measure(a, out):
    with (out / 'kernel.elf').open('rb') as f:
        syms = {s.name: s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
    exe = ROOT / 'build/emulator/main_headless/Release/Tsugaru_Headless.exe'
    rom = ROOT / 'build/play/STUBROM'
    log = (out / 'run.log').open('w')
    p = subprocess.Popen([str(exe), str(rom), '-CD', str(out / 'audit.ISO'),
                          '-TOWNSTYPE', 'MODEL2', '-FREQ', str(a.freq), '-MEMSIZE', str(a.ram),
                          '-DONTAUTOSAVECMOS', '-NOWAITBOOT'], stdin=subprocess.PIPE,
                         stdout=log, stderr=subprocess.STDOUT, text=True)
    def cmd(s):
        p.stdin.write('!' + s + '\n'); p.stdin.flush()
    def word(b, name):
        return struct.unpack_from('<I', b, syms[name])[0]
    try:
        deadline = time.monotonic() + a.timeout
        while time.monotonic() < deadline:
            if p.poll() is not None:
                raise RuntimeError('Emulator exited; inspect ' + str(out / 'run.log'))
            time.sleep(1)
            dump = out / 'ram.bin'
            if dump.exists(): dump.unlink()
            cmd('SAVEMEMDUMP ' + str(dump) + ' PHYS:0 100000')
            wait = time.monotonic() + 2
            while time.monotonic() < wait and (not dump.exists() or dump.stat().st_size != 0x100000):
                time.sleep(.05)
            if not dump.exists() or dump.stat().st_size != 0x100000:
                continue
            b = dump.read_bytes()
            if word(b, 'stdDone' if a.standard else 'auditMeshDone' if a.mesh_probe else 'auditDone') == 1:
                break
        else:
            raise RuntimeError('Benchmark did not complete; inspect ' + str(out / 'run.log'))
        if a.standard:
            phases = ['look','walk','down','up','break','mixed']
            info = struct.unpack_from('<8I', b, syms['stdInfo'])
            result = dict(ref=a.ref, name=a.name, ram=a.ram, freq=a.freq,
                          level_version=info[0], width=info[1], terrain_hash=info[2],
                          scale=info[3], view=info[4], mobs=info[5], phases={},
                          iso_sha256=hashlib.sha256((out / 'audit.ISO').read_bytes()).hexdigest())
            for i, label in enumerate(phases):
                s = struct.unpack_from('<12I', b, syms['stdStats']+i*48)
                if not s[0]: continue
                hist = struct.unpack_from('<256I', b, syms['stdHist']+i*1024)
                def percentile(frac):
                    target = int(s[0]*frac + .999999); count = 0
                    for j, n in enumerate(hist):
                        count += n
                        if count >= target: return j*2
                result['phases'][label] = dict(frames=s[0], fps=s[0]*1e6/s[1],
                    median_ms_lower_bound=percentile(.5), p95_ms_lower_bound=percentile(.95),
                    max_ms=s[2]/1000, update_mean_ms=s[3]/s[0]/1000,
                    update_max_ms=s[4]/1000, render_mean_ms=s[5]/s[0]/1000,
                    average_faces=s[6]/s[0], average_items=s[7]/s[0],
                    over_66ms=s[8], over_100ms=s[9], edits=s[10])
            n = word(b, 'stdEditCount')
            assert n <= 32
            result['edits'] = [struct.unpack_from('<7I', b, syms['stdEdits']+i*28) for i in range(n)]
            assert result['width'] == a.width and result['phases']
            (out / 'result.json').write_text(json.dumps(result, indent=2)+'\n')
            cmd('SS '+str(out / 'end.png'))
            print(json.dumps(result), flush=True)
            return
        if a.mesh_probe:
            n = word(b, 'auditMeshCount')
            assert n == 30, n
            rows = [struct.unpack_from('<10I', b, syms['auditMesh']+i*40) for i in range(n)]
            memory = struct.unpack_from('<12I', b, syms['auditMemory'])
            result = dict(ref=a.ref, name=a.name, ram=a.ram, memory=list(memory),
                          sync_mesh=a.sync_mesh, linear_test=a.linear_test, full_mesh=a.full_mesh,
                          iso_sha256=hashlib.sha256((out / 'audit.ISO').read_bytes()).hexdigest(),
                          edits=rows, mesh_hashes=list(struct.unpack_from('<30I', b, syms['auditMeshHashes'])))
            assert all(r[9] == 0 for r in rows), 'Dirty work or mesh/full-reference mismatch'
            (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result), flush=True)
            return
        n = word(b, 'auditCount')
        assert 10 < n <= 768, n
        rows = [struct.unpack_from('<10I', b, syms['auditRows']+i*40) for i in range(n)]
        info = struct.unpack_from('<8I', b, syms['auditInfo'])
        result = dict(ref=a.ref, name=a.name, ram=a.ram, width=info[0], spawn=list(info[1:4]),
                      scale=info[4], view=info[5], terrain_hash=info[6],
                      iso_sha256=hashlib.sha256((out / 'audit.ISO').read_bytes()).hexdigest())
        for label, start, end in [('look', 200, 1200), ('travel', 1400, 3200)]:
            phase = [r for r in rows if start <= r[0] < end]
            result[label] = dict(frame=stats([r[1] for r in phase], frame=True),
                                 update=stats([r[2] for r in phase]) if sum(r[2] for r in phase) else {'max_ms':0},
                                 render=stats([r[3] for r in phase]),
                                 average_faces=sum(r[4] for r in phase)/len(phase),
                                 average_items=sum(r[5] for r in phase)/len(phase),
                                 max_missing_visible_chunks=max(r[9] for r in phase), frames=len(phase))
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        (out / 'rows.json').write_text(json.dumps(rows) + '\n')
        cmd('SS ' + str(out / 'end.png'))
        print(json.dumps(result), flush=True)
    finally:
        if p.poll() is None:
            cmd('Q')
            try: p.wait(timeout=10)
            except subprocess.TimeoutExpired: p.kill(); p.wait()
        log.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--name', required=True)
    ap.add_argument('--ref', default='WORKTREE')
    ap.add_argument('--ram', type=int, default=2)
    ap.add_argument('--freq', type=int, default=16)
    ap.add_argument('--zig', default=str(ROOT / 'build/tools/zig-windows-x86_64-0.13.0/zig.exe'))
    ap.add_argument('--timeout', type=float, default=240)
    ap.add_argument('--define', action='append', default=[])
    ap.add_argument('--build-only', action='store_true')
    ap.add_argument('--run-only', action='store_true')
    ap.add_argument('--mesh-probe', action='store_true', help='Controlled edit/memory fixture, not FPS')
    ap.add_argument('--sync-mesh', action='store_true', help='Test-only synchronous local dirty rebuilds')
    ap.add_argument('--linear-test', action='store_true', help='Test-only local mesher with 80-wide linear storage')
    ap.add_argument('--full-mesh', action='store_true', help='Test-only master full rather than layer rebuilds')
    ap.add_argument('--standard', action='store_true', help='Standard level and individual motion/edit phases')
    ap.add_argument('--width', type=int, default=80)
    ap.add_argument('--scale', type=int, choices=[1,2], default=2)
    ap.add_argument('--view', type=int, default=8)
    ap.add_argument('--mobs', type=int, choices=range(25), default=0, help='Frozen controlled pigs, 0 isolates terrain')
    ap.add_argument('--phase', type=int, choices=range(-1,6), default=-1, help='-1 all; 0 look, 1 walk, 2 down, 3 up, 4 break, 5 mixed')
    ap.add_argument('--step', choices=['all','look','walk','down','up','break','mixed'], help='Named alias for --phase')
    ap.add_argument('--production', action='store_true', help='Build untouched snapshot, without instrumentation')
    a = ap.parse_args()
    if a.step: a.phase = ['all','look','walk','down','up','break','mixed'].index(a.step)-1
    if a.production and (not a.build_only or a.standard or a.mesh_probe):
        ap.error('--production requires --build-only and no instrumentation')
    if a.standard and a.mesh_probe: ap.error('select either --standard or --mesh-probe')
    if (a.sync_mesh or a.linear_test or a.full_mesh) and not a.mesh_probe:
        ap.error('--sync-mesh, --linear-test and --full-mesh require --mesh-probe')
    if not a.name.replace('-', '').replace('_', '').isalnum():
        ap.error('name must contain only letters, numbers, hyphens and underscores')
    out = ROOT / 'build/perf-audit' / a.name
    out.mkdir(parents=True, exist_ok=True)
    if not a.run_only: build(a, out)
    if not a.build_only: measure(a, out)


if __name__ == '__main__':
    main()
