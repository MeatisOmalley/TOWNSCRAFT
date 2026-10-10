"""Link real pinned gameplay/renderer with existing DOS platform adapters.

No gameplay changes, budget reductions or vendor edits. Verified transport
optimizations are separate from the pinned renderer. Audio is deliberately silent.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import build_imported as imported
import build_adapters as adapters
import dos_sources

ROOT=imported.ROOT
OUTPUT=ROOT/'build/game/486dx25'
GAME_ADAPTERS=('game_entry.c','game_runtime.c','sound_silent.c','gfx.c','heap.c','video.c',
    'vga_pack.c','keyboard.c','irq.c','irq_entry.S','system.c','input.c','hdd.c','save.c')
DEFAULT_ADAPTER_FLAGS=('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST')


def build(output=OUTPUT, benchmark=False, extra_adapter_flags=None, graphics=None):
    if extra_adapter_flags is None: extra_adapter_flags=DEFAULT_ADAPTER_FLAGS
    if graphics is None: graphics=not benchmark
    output=Path(output)
    output.mkdir(parents=True,exist_ok=True)
    report={'stage':'Full game link; silent audio; gameplay acceptance pending',
        'status':'in_progress','source_commit':imported.PINNED_COMMIT,
        'audio':'Silent temporary backend explicitly authorized; no audio parity',
        'commands':[],'objects':[],'linked':False,'guest_verified':False}
    manifest=output/'build.json'
    manifest.write_text(json.dumps(report,indent=2)+'\n')
    try:
        extra=('-DBENCH_EDIT','-DFIXED_SEED=4242','-DDOSCRAFT_BENCH') if benchmark else ()
        report['benchmark']=benchmark
        report['extra_adapter_flags']=list(extra_adapter_flags)
        report['graphics_presets']=graphics
        raster_zero_dx='-DDOS_RASTER_ZERO_DX' in extra_adapter_flags
        camera_reuse='-DDOS_CAMERA_REUSE' in extra_adapter_flags
        packed_raster='-DDOS_PACKED_RASTER' in extra_adapter_flags
        early_split_cull='-DDOS_EARLY_SPLIT_CULL' in extra_adapter_flags
        front_to_back='-DDOS_FRONT_TO_BACK' in extra_adapter_flags
        edge_recip='-DDOS_EDGE_RECIP' in extra_adapter_flags
        transform=lambda name,data:dos_sources.adapt(name,data,graphics=graphics,
            raster_zero_dx=raster_zero_dx,camera_reuse=camera_reuse,packed_raster=packed_raster,
            early_split_cull=early_split_cull,front_to_back=front_to_back,edge_recip=edge_recip)
        portable=imported.build(output=output/'imported',
            extra_cflags=(*extra,*(flag for flag in extra_adapter_flags if flag in ('-DDOS_F2B_VERIFY','-DDOS_F2B_STRESS')),
                '-I',str(ROOT/'src/platform/dos'),'-I',str(output/'imported/src')),source_transform=transform)
        platform=adapters.stage(output/'platform')
        report['imported']=portable
        report['platform']=platform
        objects=[output/'imported'/row['path'] for row in portable['objects']]
        objdir=output/'obj'; objdir.mkdir(exist_ok=True)
        names=tuple('bench_runtime.c' if name=='game_runtime.c' else name for name in GAME_ADAPTERS) if benchmark else GAME_ADAPTERS
        if graphics or any(flag.startswith('-DDOS_BENCH_QUALITY=') for flag in extra_adapter_flags):
            names=(*names,'graphics_presets.c')
        if '-DDOS_VGA_PACK_ALL' in extra_adapter_flags:
            names=(*names,'vga_pack_all.c')
        if '-DDOS_VGA_PACK_WORDS' in extra_adapter_flags:
            names=(*names,'vga_pack_words.c')
        if '-DDOS_VGA_HUD_CACHE' in extra_adapter_flags:
            names=(*names,'vga_hud_cache.c')
        if '-DDOS_VGA_BROADCAST' in extra_adapter_flags:
            names=(*names,'vga_upload_plan.c')
        if packed_raster:
            names=(*names,'packed_raster.c')
        if front_to_back:
            names=(*names,'coverage_spans.c','f2b_capture.c','f2b_texel.c','f2b_raster.c','f2b_span.S')
        if edge_recip: names=(*names,'edge_recip.c')
        for name in names:
            source=ROOT/'src/platform/dos'/name
            obj=objdir/(Path(name).stem+'.o')
            command=[imported.COMPILER,*imported.CFLAGS,*extra,*extra_adapter_flags,
                *(('-DDOS_GRAPHICS_PRESETS',) if graphics else ()),'-I',output/'platform/src',
                '-I',ROOT/'src/platform/dos','-c',source,'-o',obj]
            subprocess.run(list(map(str,command)),check=True)
            report['commands'].append(list(map(str,command)))
            report['objects'].append({'source':str(source.relative_to(ROOT)),
                'source_sha256':imported.sha256(source.read_bytes()),
                'object_sha256':imported.sha256(obj.read_bytes())})
            objects.append(obj)
        exe=output/'DOSCRAFT.EXE'
        wraps='-Wl,--wrap=gfx_present' if benchmark else '-Wl,--wrap=gfx_present,--wrap=save_world,--wrap=load_world,--wrap=render_frame'
        if packed_raster: wraps+=',--wrap=raster_finish'
        if front_to_back:
            wraps+=''.join(',--wrap=trap_'+name for name in
                ('opaque1','opaque2','transp1','transp2','opaque1w','opaque2w','transp1w','transp2w'))
        command=[imported.COMPILER,*imported.CFLAGS,*objects,wraps,'-o',exe]
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
