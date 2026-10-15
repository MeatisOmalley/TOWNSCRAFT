"""Stage exact pinned Towns effect synthesis and score parsing for DOS.

No waveform/score rewrites, resampling, music timing changes or hardware I/O.
Only the chip sink/initialization wrappers are excluded; generation helpers,
effect parameters, score strings and stable event sorting are retained. The
one recorded declaration edit removes the hardware wave-bank shadow.
"""
import json
from pathlib import Path
import build_imported as imported


def asset_source(vendor=imported.VENDOR):
    vendor=Path(vendor)
    manifest=json.loads((vendor/'MANIFEST.json').read_bytes())
    row=next(r for r in manifest['files'] if r['path']=='src/sound.c')
    data=(vendor/'src/sound.c').read_bytes()
    if (manifest['source_commit']!=imported.PINNED_COMMIT or
            imported.sha256(data)!=row['sha256'] or len(data)!=row['size']):
        raise ValueError('Pinned sound.c provenance mismatch')
    boundaries=(
        (b'#define PCM_RATE 20833',b'static u8 pcmOff'),
        (b'static u32 synthSeed=12345;',b'static void put_byte'),
        (b'static void put_sample',b'static void pcm_init'),
        (b'\t/* Page 0: the silence loop */',b'\tfor(i=0; i<8; ++i)'),
        (b'#define EIGHTH 36',b'static volatile int musicPlaying'),
        (b'/* Parse one voice into events',b'static void music_init'),
        (b'\tparse_voice(scoreLH',b'\tfm_write(0,0x22,0);'),
    )
    selections=[]
    for begin,end in boundaries:
        if data.count(begin)!=1 or data.count(end)!=1:
            raise ValueError('Unexpected audio source selection boundary')
        a,b=data.index(begin),data.index(end)
        if a>=b: raise ValueError('Reversed audio source boundary')
        selections.append([a,b])
    assembly=[{'source':0},{'source':1},{'source':2},
              {'literal':'\nstatic void assets_wave_source(void)\n{\n'},{'source':3},{'literal':'}\n'},
              {'source':4},{'source':5},
              {'literal':'\nstatic void assets_score_source(void)\n{\n\tint i,j;\n'},{'source':6},{'literal':'}\n'}]
    assembled=b''.join(data[slice(*selections[part['source']])] if 'source' in part
                       else part['literal'].encode('ascii') for part in assembly)
    before,after=b'static int synthTop,synthBank=-1;',b'static int synthTop;'
    if assembled.count(before)!=1: raise ValueError('Unexpected wave-bank declaration')
    generated=assembled.replace(before,after,1)
    if any(token in generated for token in (b'outb(',b'inb(',b'PCM_WINDOW',b'fm_write(',b'cli()',b'sti()')):
        raise ValueError('Hardware dependency in portable audio asset selection')
    recipe=[dict(operation='replace',before=before.decode(),after=after.decode(),count=1)]
    return generated,dict(source='src/sound.c',source_sha256=row['sha256'],
        staged='src/towns_audio_assets.inc',staged_sha256=imported.sha256(generated),
        selections=selections,assembly=assembly,transformations=recipe,
        transformations_sha256=imported.sha256(imported.canonical_json(recipe)))


def stage(output,vendor=imported.VENDOR):
    generated,selection=asset_source(vendor)
    output=Path(output)
    record=imported.stage_sources(vendor,output)
    (output/selection['staged']).write_bytes(generated)
    record['audio_selection']=selection
    return record
