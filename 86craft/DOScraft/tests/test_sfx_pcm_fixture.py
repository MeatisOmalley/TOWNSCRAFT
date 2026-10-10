"""Independent expected PCM for retained-assets mixed-effects guest fixture."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from test_sound_mixer import Oracle, DEFAULT_RATE, ZIG, command_run
import test_sound_assets as reference

ROOT=Path(__file__).resolve().parents[1]


def expected_pcm(wire=None):
    if wire is None: wire=reference.native_reference_bytes()
    _,_,wave_bytes,_,_=struct.unpack_from('<5I',wire)
    samples=[struct.unpack_from('<4I',wire,20+i*16) for i in range(10)]
    commands=[('build',),('init',DEFAULT_RATE,0)]
    for block in range(48):
        if block==0: commands.append(('loop',9,128))
        if block<10: commands.append(('play',block,128+(block%4)*64,220,block*3-15))
        if 10<=block<20: commands.append(('play',(block-10)%10,256,255,0))
        if block==20: commands.append(('loop',9,220))
        if block==24: commands.append(('loop',9,0))
        if block==28:
            commands.extend(('play',ident,256,200,ident-5) for ident in range(10))
        if block==40: commands.append(('stop',))
        commands.append(('render',512))
    return Oracle(wire[180:180+wave_bytes],samples).run(commands)


@unittest.skipUnless(sys.platform=='win32' and ZIG.is_file(),'existing native x86-Win32 Zig unavailable')
class MixedEffectFixtureTests(unittest.TestCase):
    def test_real_c_fixture_matches_independent_original_assets_arithmetic(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-sfx-fixture-') as name:
            directory=Path(name)
            executable=reference.build_native_test(directory)
            wire=reference.native_reference_bytes(executable=executable)
            harness=directory/'fixture.c'
            harness.write_text('''#include "sound.h"
#include "sound_assets.h"
#include "sound_mixer.h"
#include "sfx_pcm_fixture.inc"
#include <stdio.h>
int main(int argc,char **argv) {
    unsigned char pcm[SFX_FIXTURE_FRAMES];
    if(argc!=2 || dos_sound_assets_build() || dos_mixer_init(DOS_MIXER_DEFAULT_RATE)) return 1;
    FILE *file=fopen(argv[1],"wb"); if(!file) return 2;
    for(unsigned int i=0;i<SFX_FIXTURE_BLOCKS;++i) {
        sfx_fixture_command(i);
        if(dos_mixer_render(pcm,sizeof(pcm)) || fwrite(pcm,1,sizeof(pcm),file)!=sizeof(pcm)) return 3;
    }
    return fclose(file)!=0;
}
''')
            staged=directory/'src'
            executable=directory/'fixture.exe'
            command_run([ZIG,'cc','-target','x86-windows-gnu','-std=gnu99','-O2',
                '-Wall','-Wextra','-Werror','-I',ROOT/'src/platform/dos','-I',ROOT/'tests','-I',staged,
                harness,ROOT/'src/platform/dos/sound_assets.c',ROOT/'src/platform/dos/sound_mixer.c',
                staged/'fmath.c',staged/'tables.c','-o',executable])
            output=directory/'pcm.bin'
            command_run([executable,output])
            expected=expected_pcm(wire)
            self.assertEqual(len(expected),48*512)
            self.assertEqual(output.read_bytes(),expected)
            self.assertNotEqual(expected[:40*512],bytes([128])*(40*512))
            self.assertEqual(expected[40*512:],bytes([128])*(8*512))


if __name__=='__main__': unittest.main()
