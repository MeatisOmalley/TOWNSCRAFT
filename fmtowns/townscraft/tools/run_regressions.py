"""Run the actual Townscraft regressions with bundled Zig on Windows.
Only generated test binaries/assembly are written, under ignored build/.
"""
import concurrent.futures
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]

def main():
    out=ROOT/'build/regressions'; out.mkdir(parents=True,exist_ok=True)
    zig=ROOT/'build/tools/zig-windows-x86_64-0.13.0/zig.exe'
    tables=out/'tables.c'
    subprocess.run([sys.executable,str(ROOT/'tools/gentables.py'),str(tables)],check=True)
    # The real raster assembly uses ELF metadata and undecorated symbols.
    # Adapt linkage, not its instructions, for native 32-bit Windows tests.
    assembly=(ROOT/'src/trap.S').read_text()
    assembly=re.sub(r'^\s*\.section \.note\.GNU-stack.*$','',assembly,flags=re.M)
    assembly=re.sub(r'(\tTRAP )(trap_\w+)',r'\1_\2',assembly).replace('g_recip14','_g_recip14')
    trap=out/'trap-native.S'; trap.write_text(assembly)
    tests=[(n,n,[],[]) for n in ('caves','column_cache','column_save','edit','stream','mobs','mob_regions','save_regions')]
    tests += [('edit_cache_2','edit',['-DEDIT_CACHED','-DEDIT_RAM=2'],[]),
              ('terrain_hdd','terrain_hdd',[],[]),
              ('terrain_floppy','terrain_floppy',[],[]),
              ('hdd','hdd',[],[]),
              ('edit_cache_4','edit',['-DEDIT_CACHED','-DEDIT_RAM=4'],[]),
              ('edit_cache_8','edit',['-DEDIT_CACHED','-DEDIT_RAM=8'],[]),
              ('player','player',[],['src/player.c','src/physics.c']),
              ('keyboard','keyboard',[],['src/player.c','src/physics.c']),
              ('render_occlusion','render_occlusion',[],['src/raster.c',str(trap)])]
    def run(spec):
        name,test,flags,extra=spec
        exe=out/(name+'.exe')
        args=[str(zig),'cc','-target','x86-windows-gnu','-O2','-fno-builtin','-Isrc',*flags,
              'tests/'+test+'_test.c',*extra,*([] if test in ('terrain_hdd','terrain_floppy','hdd') else ['tests/no_hdd.c']),
              'src/blocks.c','src/fmath.c','src/libc.c',str(tables),'-o',str(exe)]
        compiled=subprocess.run(args,cwd=ROOT,text=True,capture_output=True)
        result=compiled if compiled.returncode else subprocess.run([str(exe)],cwd=ROOT,text=True,capture_output=True)
        log=compiled.stdout+compiled.stderr
        if result is not compiled: log+=result.stdout+result.stderr
        (out/(name+'.log')).write_text(log)
        print(('PASS ' if result.returncode==0 else 'FAIL ')+name,flush=True)
        return dict(test=name,exit_code=result.returncode,output=log)
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool: results=list(pool.map(run,tests))
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    if any(r['exit_code'] for r in results): raise SystemExit(1)
    print(f'All {len(results)} regression configurations passed.')

if __name__=='__main__': main()
