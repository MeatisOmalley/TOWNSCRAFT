from pathlib import Path
import tempfile
import unittest
from test_irq import run,ZIG
ROOT=Path(__file__).resolve().parents[1]
STAGE=ROOT/'build/checks/sampled-audio-base-release/imported/src'
@unittest.skipUnless(ZIG.is_file() and STAGE.is_dir(),'native Zig and staged game headers required')
class GameAudioTests(unittest.TestCase):
    def test_samples_basic_and_speaker_keep_game_contracts(self):
        with tempfile.TemporaryDirectory(prefix='dos-game-audio-') as folder:
            p=Path(folder);(p/'pc.h').write_text('unsigned char inportb(unsigned short);\nvoid outportb(unsigned short,unsigned char);\n')
            exe=p/'test.exe';dos=ROOT/'src/platform/dos'
            run([ZIG,'cc','-target','x86-windows-gnu','-std=gnu99','-O2','-fno-builtin',
                 '-Wall','-Wextra','-Werror','-I',p,'-I',dos,'-I',STAGE,
                 ROOT/'tests/game_audio_native.c',dos/'audio_voices.c',
                 STAGE/'blocks.c',STAGE/'fmath.c',STAGE/'tables.c','-o',exe])
            for scenario in range(3):self.assertIn('PASS:',run([exe,str(scenario)]))
