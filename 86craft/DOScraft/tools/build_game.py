"""Link real pinned gameplay/renderer with existing DOS platform adapters.

No gameplay defines, budget reductions, optimizer experiments or vendor edits.
Audio deliberately silent per user priority. This builds the game, not a probe.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import build_imported as imported
import build_adapters as adapters

ROOT=imported.ROOT
OUTPUT=ROOT/'build/game/486dx25'
GAME_ADAPTERS=('game_entry.c','game_runtime.c','sound_silent.c','gfx.c','heap.c','video.c',
    'vga_pack.c','keyboard.c','irq.c','irq_entry.S','system.c','input.c','hdd.c','save.c')


def build(output=OUTPUT):
    output=Path(output)
    output.mkdir(parents=True,exist_ok=True)
    report={'stage':'Full game link; silent audio; gameplay acceptance pending',
        'status':'in_progress','source_commit':imported.PINNED_COMMIT,
        'audio':'Silent temporary backend explicitly authorized; no audio parity',
        'commands':[],'objects':[],'linked':False,'guest_verified':False}
    manifest=output/'build.json'
    manifest.write_text(json.dumps(report,indent=2)+'\n')
    try:
        portable=imported.build(output=output/'imported')
        platform=adapters.stage(output/'platform')
        report['imported']=portable
        report['platform']=platform
        objects=[output/'imported'/row['path'] for row in portable['objects']]
        objdir=output/'obj'; objdir.mkdir(exist_ok=True)
        for name in GAME_ADAPTERS:
            source=ROOT/'src/platform/dos'/name
            obj=objdir/(Path(name).stem+'.o')
            command=[imported.COMPILER,*imported.CFLAGS,'-I',output/'platform/src',
                '-I',ROOT/'src/platform/dos','-c',source,'-o',obj]
            subprocess.run(list(map(str,command)),check=True)
            report['commands'].append(list(map(str,command)))
            report['objects'].append({'source':str(source.relative_to(ROOT)),
                'source_sha256':imported.sha256(source.read_bytes()),
                'object_sha256':imported.sha256(obj.read_bytes())})
            objects.append(obj)
        exe=output/'DOSCRAFT.EXE'
        command=[imported.COMPILER,*imported.CFLAGS,*objects,
            '-Wl,--wrap=gfx_present,--wrap=save_world,--wrap=load_world','-o',exe]
        subprocess.run(list(map(str,command)),check=True)
        report.update(status='linked',linked=True,exe_sha256=imported.sha256(exe.read_bytes()),
            executable_bytes=exe.stat().st_size,link_command=list(map(str,command)))
        return report
    except Exception as error:
        report.update(status='failed',error=str(error)); raise
    finally:
        manifest.write_text(json.dumps(report,indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    report=build()
    print(f'Linked {OUTPUT / "DOSCRAFT.EXE"} ({report["executable_bytes"]} bytes); gameplay acceptance pending.')


if __name__=='__main__': main()
