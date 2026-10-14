"""Link pinned renderer/gameplay with DOS platform adapters and source overlays.

The Towns source stays immutable. Verified transport optimizations are separate
from the pinned renderer. Audio is deliberately silent.
"""
import argparse
import json
import struct
from pathlib import Path
import subprocess
import sys
import build_imported as imported
import build_adapters as adapters
import dos_sources

ROOT=imported.ROOT
OUTPUT=ROOT/'build/game/486dx25'
GAME_ADAPTERS=('game_entry.c','game_runtime.c','sound_silent.c','gfx.c','heap.c','video.c',
    'vga_pack.c','keyboard.c','irq.c','irq_entry.S','system.c','input.c','hdd.c','save.c',
    'craft_grid.c','viewmodel.c')
DEFAULT_ADAPTER_FLAGS=('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_ET4000_CHUNKY','-DDOS_TRAP_FOUR','-DDOS_ET4000_STRIDED','-DDOS_ET4000_HUD_WORDS','-DDOS_VGA_PROVEN_ROWS','-DDOS_VGA_PROVEN_VERTICAL','-DDOS_VGA_ASYNC_FLIP','-DDOS_VGA_HUD_PROOF','-DDOS_RASTER_FINISH_SKIP','-DDOS_VERY_LOW','-DDOS_VERY_LOW_TEXELS','-DDOS_LOW_PLANAR','-DDOS_TEXEL_EDX','-DDOS_ROW_MUL','-DDOS_ROW_POINTER','-DDOS_FLAT_INLINE','-DDOS_STREAM_VALIDATE_ONCE','-DDOS_LIGHT_OFFSETS','-DDOS_MESH_EDGES','-DDOS_COLLECT_SWAP','-DDOS_MEMMOVE_DWORDS','-DDOS_WORLD_SCANS','-DDOS_BAKED_LIGHT_FAST','-DDOS_SORT_HISTOGRAMS','-DDOS_FILL_ROWS','-DDOS_PLANT_BILLBOARDS','-DDOS_VIEWMODEL')
# ET4000-only transport: chain-4 banked uploads (High). Without these the
# presenter is plain VGA Mode X on any card.
ET4000_ADAPTER_FLAGS=('-DDOS_ET4000_CHUNKY','-DDOS_ET4000_STRIDED','-DDOS_ET4000_HUD_WORDS')
# 386 build: any VGA card, accepts a 386 at startup, i386 code generation.
CPU386_ADAPTER_FLAGS=tuple(flag for flag in DEFAULT_ADAPTER_FLAGS if flag not in ET4000_ADAPTER_FLAGS)+('-DDOS_CPU386',)
CPU386_CFLAGS=('-march=i386','-mtune=i386')
OUTPUT_386=ROOT/'build/game/386-vga'
PORTABLE_EXPERIMENT_FLAGS=('-DDOS_F2B_VERIFY','-DDOS_F2B_STRESS','-fomit-frame-pointer','-DDOS_VGA_PROVEN_ROWS','-DDOS_VGA_PROVEN_VERTICAL','-DDOS_RASTER_FINISH_SKIP','-DDOS_VERY_LOW','-DDOS_VERY_LOW_TEXELS','-DDOS_LOW_PLANAR')


# Main stack, as system.c's _stklen. DJGPP uses the larger of _stklen and the
# stub's minstack (512 KiB by default), so the stub field is set to match, as
# DJGPP's stubedit minstack= does.
MAIN_STACK_BYTES=64*1024


def set_min_stack(exe):
    data=bytearray(exe.read_bytes())
    at=data.find(b'go32stub, v 2.')
    if at!=512 or struct.unpack_from('<I',data,at+16)[0]<24:
        raise ValueError('Unexpected DJGPP stub info in '+str(exe))
    struct.pack_into('<I',data,at+20,MAIN_STACK_BYTES)
    exe.write_bytes(bytes(data))


def portable_experiment_flags(flags):
    return tuple(flag for flag in flags if flag in PORTABLE_EXPERIMENT_FLAGS or flag in ('-DDOS_COLLECT_VERIFY','-DDOS_STREAM_LIGHT_CACHE','-DDOS_FAR_FIELD','-DDOS_FAR_DETAILS','-DDOS_FAR_VIEW_DEPTH','-DDOS_FAR_SKIP_FIRST','-DDOS_FAR_REPROJECT','-DDOS_BENCH_FAR_COARSE_EYE','-DDOS_BENCH_PROFILE_FAR','-DDOS_CPU386'))


def build(output=OUTPUT, benchmark=False, extra_adapter_flags=None, graphics=None):
    if extra_adapter_flags is None: extra_adapter_flags=DEFAULT_ADAPTER_FLAGS
    if graphics is None: graphics=not benchmark
    # Later -march/-mtune options override the i486 defaults in imported.CFLAGS.
    arch=CPU386_CFLAGS if '-DDOS_CPU386' in extra_adapter_flags else ()
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
        trap_zero_gradient='-DDOS_TRAP_ZERO_GRADIENT' in extra_adapter_flags
        trap_destination='-DDOS_TRAP_DESTINATION' in extra_adapter_flags
        split_reuse='-DDOS_SPLIT_REUSE' in extra_adapter_flags
        depth_adaptive='-DDOS_DEPTH_ADAPTIVE' in extra_adapter_flags
        trap_stable='-DDOS_TRAP_STABLE' in extra_adapter_flags
        trap_four='-DDOS_TRAP_FOUR' in extra_adapter_flags
        wall_visibility='-DDOS_WALL_VISIBILITY' in extra_adapter_flags
        wall_verify='-DDOS_WALL_VERIFY' in extra_adapter_flags
        group_facing='-DDOS_GROUP_FACING' in extra_adapter_flags
        proven_rows='-DDOS_VGA_PROVEN_ROWS' in extra_adapter_flags
        raster_finish_skip='-DDOS_RASTER_FINISH_SKIP' in extra_adapter_flags
        very_low='-DDOS_VERY_LOW' in extra_adapter_flags
        very_low_texels='-DDOS_VERY_LOW_TEXELS' in extra_adapter_flags
        low_planar='-DDOS_LOW_PLANAR' in extra_adapter_flags
        a1_census='-DDOS_BENCH_A1_CENSUS' in extra_adapter_flags
        profile_cw='-DDOS_BENCH_PROFILE_CW' in extra_adapter_flags
        large_world='-DDOS_LARGE_WORLD' in extra_adapter_flags
        if '-DDOS_VGA_PROVEN_VERTICAL' in extra_adapter_flags and not proven_rows:
            raise ValueError('Vertical row proof requires proven rows')
        if raster_finish_skip and not (proven_rows and '-DDOS_VGA_PROVEN_VERTICAL' in extra_adapter_flags):
            raise ValueError('Raster finish skip requires vertical row proof')
        if very_low and not raster_finish_skip:
            raise ValueError('Very Low output requires the repeated-row raster finish path')
        if very_low_texels and not very_low:
            raise ValueError('Very Low texels require Very Low output')
        if low_planar and not (raster_finish_skip and '-DDOS_VGA_PACK_ALL' in extra_adapter_flags and
                               '-DDOS_VGA_BROADCAST' in extra_adapter_flags):
            raise ValueError('Planar Low requires the finish-skip bands and all-plane broadcast presenter')
        if proven_rows and ('-DDOS_VGA_FUSED' in extra_adapter_flags or
                            '-DDOS_VGA_FUSED_TRANSPOSE' in extra_adapter_flags):
            raise ValueError('Measure proven rows independently of fused packing')
        transform=lambda name,data:dos_sources.adapt(name,data,graphics=graphics,
            raster_zero_dx=raster_zero_dx,camera_reuse=camera_reuse,packed_raster=packed_raster,
            early_split_cull=early_split_cull,front_to_back=front_to_back,edge_recip=edge_recip,
            trap_zero_gradient=trap_zero_gradient,trap_destination=trap_destination,
            split_reuse=split_reuse,depth_adaptive=depth_adaptive,trap_stable=trap_stable,trap_four=trap_four,
            wall_visibility=wall_visibility,wall_verify=wall_verify,group_facing=group_facing,
            hud_proof='-DDOS_VGA_HUD_PROOF' in extra_adapter_flags,
            proven_rows=proven_rows,raster_finish_skip=raster_finish_skip,
            very_low_texels=very_low_texels,low_planar=low_planar,a1_census=a1_census,
            profile_cw=profile_cw,large_world=large_world,
            collect_margin='-DDOS_COLLECT_MARGIN' in extra_adapter_flags,
            stream_light='-DDOS_STREAM_LIGHT_CACHE' in extra_adapter_flags,
            far_field='-DDOS_FAR_FIELD' in extra_adapter_flags,
            stream_validate_once='-DDOS_STREAM_VALIDATE_ONCE' in extra_adapter_flags,
            light_offsets='-DDOS_LIGHT_OFFSETS' in extra_adapter_flags,
            mesh_edges='-DDOS_MESH_EDGES' in extra_adapter_flags,
            collect_swap='-DDOS_COLLECT_SWAP' in extra_adapter_flags,
            memmove_dwords='-DDOS_MEMMOVE_DWORDS' in extra_adapter_flags,
            world_scans='-DDOS_WORLD_SCANS' in extra_adapter_flags,
            baked_light_fast='-DDOS_BAKED_LIGHT_FAST' in extra_adapter_flags,
            sort_histograms='-DDOS_SORT_HISTOGRAMS' in extra_adapter_flags,
            texel_edx='-DDOS_TEXEL_EDX' in extra_adapter_flags,
            row_pointer='-DDOS_ROW_POINTER' in extra_adapter_flags,
            flat_inline='-DDOS_FLAT_INLINE' in extra_adapter_flags,
            row_mul='-DDOS_ROW_MUL' in extra_adapter_flags,
            mulshift='-DDOS_MULSHIFT' in extra_adapter_flags,
            planar_expand_words='-DDOS_PLANAR_EXPAND_WORDS' in extra_adapter_flags,
            plant_billboards='-DDOS_PLANT_BILLBOARDS' in extra_adapter_flags,
            fill_rows='-DDOS_FILL_ROWS' in extra_adapter_flags,
            viewmodel='-DDOS_VIEWMODEL' in extra_adapter_flags)
        portable=imported.build(output=output/'imported',
            extra_cflags=(*arch,*extra,*portable_experiment_flags(extra_adapter_flags),
                '-I',str(ROOT/'src/platform/dos'),'-I',str(output/'imported/src')),source_transform=transform)
        platform=adapters.stage(output/'platform',large_world=large_world,chickens=True)
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
        if '-DDOS_VGA_FUSED' in extra_adapter_flags:
            names=(*names,'vga_fused.c')
        if proven_rows:
            names=(*names,'vga_proven.c')
        if packed_raster:
            names=(*names,'packed_raster.c')
        if low_planar:
            names=(*names,'low_planar.S')
        if front_to_back:
            names=(*names,'coverage_spans.c','f2b_capture.c','f2b_texel.c','f2b_raster.c','f2b_span.S')
        if edge_recip: names=(*names,'edge_recip.c')
        for name in names:
            source=ROOT/'src/platform/dos'/name
            obj=objdir/(Path(name).stem+'.o')
            # Only benchmark builds sample the profiler.
            profile=() if benchmark else ('-DDOS_PROFILE_SAMPLES=1',)
            command=[imported.COMPILER,*imported.CFLAGS,*arch,*extra,*profile,*extra_adapter_flags,
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
        command=[imported.COMPILER,*imported.CFLAGS,*arch,*objects,wraps,'-o',exe]
        subprocess.run(list(map(str,command)),check=True)
        set_min_stack(exe)
        report.update(status='linked',linked=True,exe_sha256=imported.sha256(exe.read_bytes()),
            executable_bytes=exe.stat().st_size,link_command=list(map(str,command)))
        return report
    except Exception as error:
        report.update(status='failed',error=str(error)); raise
    finally:
        manifest.write_text(json.dumps(report,indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu386',action='store_true',
                        help='386 and any-VGA build (no ET4000 transport) in build/game/386-vga')
    args=parser.parse_args()
    output=OUTPUT_386 if args.cpu386 else OUTPUT
    report=build(output,extra_adapter_flags=CPU386_ADAPTER_FLAGS if args.cpu386 else None)
    print(f'Linked {output / "DOSCRAFT.EXE"} ({report["executable_bytes"]} bytes); gameplay acceptance pending.')


if __name__=='__main__': main()
