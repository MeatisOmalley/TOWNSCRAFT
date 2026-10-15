from pathlib import Path
import tempfile
import unittest
from test_irq import run,ZIG
ROOT=Path(__file__).resolve().parents[1]
STAGE=ROOT/'build/checks/sampled-audio-base-release/imported/src'
@unittest.skipUnless(ZIG.is_file() and STAGE.is_dir(),'native Zig and staged game headers required')
class OplMusicTests(unittest.TestCase):
    def test_sequencer_and_register_contract(self):
        with tempfile.TemporaryDirectory(prefix='dos-opl-music-') as folder:
            p=Path(folder);(p/'pc.h').write_text('unsigned char inportb(unsigned short);\nvoid outportb(unsigned short,unsigned char);\n')
            exe=p/'test.exe';dos=ROOT/'src/platform/dos'
            run([ZIG,'cc','-target','x86-windows-gnu','-std=gnu99','-O2','-fno-builtin',
                 '-Wall','-Wextra','-Werror','-I',p,'-I',dos,'-I',STAGE,
                 ROOT/'tests/opl_music_native.c','-o',exe])
            self.assertIn('PASS:',run([exe]))

if __name__=='__main__':unittest.main()
