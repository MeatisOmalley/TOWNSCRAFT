from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_sound_bank import checksum,MAGIC,BANK_GROUPS
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
# Build the bank from the current masters and recipes; a copy left in build/
# can predate them.
AUDIO_DEPS=ROOT/'build/deps/audio-python'
BANK=None
_bank_folder=None


def setUpModule():
    global BANK,_bank_folder
    if not AUDIO_DEPS.is_dir():return
    from build_sound_bank import build
    _bank_folder=tempfile.TemporaryDirectory(prefix='dos-audio-bank-')
    BANK=Path(_bank_folder.name)/'SOUND.PAK';build(BANK)


def tearDownModule():
    if _bank_folder:_bank_folder.cleanup()


class AudioLibraryTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file() and AUDIO_DEPS.is_dir(),'native Zig and audio build dependencies required')
    def test_real_bank_and_integer_mixer(self):
        with tempfile.TemporaryDirectory(prefix='dos-audio-library-') as folder:
            exe=Path(folder)/'test.exe';dos=ROOT/'src/platform/dos'
            r=subprocess.run([str(x) for x in [ZIG,'cc','-target','x86-windows-gnu','-O2','-Wall','-Wextra','-Werror',
                '-I',dos,ROOT/'tests/audio_library_native.c',dos/'audio_bank.c',dos/'audio_voices.c','-o',exe]],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=subprocess.run([str(exe),str(BANK)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertIn('PASS:',r.stdout)

    def test_masters_retained(self):
        from hashlib import sha256
        p=ROOT/'assets/audio';m=json.loads((p/'sources.json').read_text())
        self.assertEqual(m['license'],'CC0-1.0');self.assertEqual(len(m['files']),44)
        for f in m['files']:self.assertEqual(sha256((p/'masters'/f['file']).read_bytes()).hexdigest(),f['sha256'])

    @unittest.skipUnless(AUDIO_DEPS.is_dir(),'audio build dependencies required')
    def test_external_bank_declares_format(self):
        b=BANK.read_bytes();magic,version,rate,fmt,count,n,total=struct.unpack_from('<8s6I',b)
        self.assertEqual((magic,version,rate,fmt,count),(MAGIC,1,11025,8|(1<<16),30))
        self.assertEqual(n,len(b)-32-16*count);self.assertEqual(total,checksum(b[32+16*count:]))

    def test_bank_groups_match_the_game_enum_and_picks(self):
        import re
        header=(ROOT/'src/platform/dos/audio_bank.h').read_text()
        names=re.findall(r'A_([A-Z_]+)',header.split('enum {',1)[1].split('};',1)[0])
        self.assertEqual(names[-1],'GROUPS')
        self.assertEqual(tuple(n.lower() for n in names[:-1]),BANK_GROUPS)
        picks=json.loads((ROOT/'assets/audio/picks.json').read_text())
        self.assertLessEqual(set(picks),set(BANK_GROUPS))
        extra=json.loads((ROOT/'assets/audio/extra/sources.json').read_text())['files']
        credits=(ROOT/'assets/audio/CREDITS.md').read_text()
        for f in extra:
            if 'BY' in f['license']:self.assertIn(f['author'],credits);self.assertIn(f['page'],credits)

    def test_audio_overlay_is_only_event_selection_and_replays(self):
        import dos_game_audio,build_imported
        data=(build_imported.VENDOR/'src/game.c').read_bytes();result,steps=dos_game_audio.adapt('game.c',data)
        replay=data
        for s in steps:
            self.assertEqual(replay.count(s['before'].encode()),s['count'])
            replay=replay.replace(s['before'].encode(),s['after'].encode())
        self.assertEqual(result,replay);self.assertIn(b'dos_sound_step',result)
        self.assertIn(b'if(!dos_sound_hit(b,tX,tY,tZ)) block_sfx(b,tX,tY,tZ,110,330);',result)
        self.assertIn(b'if(!wasInWater) dos_sound_splash(b->vy);',result);self.assertIn(b'dos_sound_swim();',result)
        # Minecraft's spacing for both new and loaded worlds; the title screen keeps 8-16 s.
        self.assertEqual(result.count(b'music_set_gap(60000);'),2)
        self.assertNotIn(b'music_set_gap(12000);',result);self.assertIn(b'music_set_gap(800);',result)
        self.assertEqual(dos_game_audio.adapt('world.c',b'test'),(b'test',[]))
        data=(build_imported.VENDOR/'src/mobs.c').read_bytes();result,steps=dos_game_audio.adapt('mobs.c',data)
        self.assertIn(b'dos_sound_mob_tick(',result);self.assertIn(b'dos_sound_mob(m->type,m->health<=0,',result)
        self.assertEqual(len(steps),3)

    @unittest.skipUnless(AUDIO_DEPS.is_dir(),'audio build dependencies required')
    def test_percussive_attacks_survive_and_levels_are_even(self):
        import math
        b=BANK.read_bytes();count=struct.unpack_from('<I',b,20)[0];payload=b[32+16*count:]
        loud={}
        for i in range(count):
            group,offset,frames,_=struct.unpack_from('<4I',b,32+16*i);x=[v-128 for v in payload[offset:offset+frames]]
            self.assertLessEqual(max(abs(v) for v in x),121)
            w=551;loud.setdefault(group,[]).append(max(math.sqrt(sum(v*v for v in x[a:a+w])/len(x[a:a+w]))
                                                     for a in range(0,max(1,len(x)-w),137)))
        # Footsteps keep their attack: no longer ~14 dB below block breaks.
        for group in ('step_grass','step_stone','step_wood','step_sand','step_gravel'):
            self.assertGreater(min(loud[BANK_GROUPS.index(group)]),0.4*min(loud[0]))
        # Every clip is within 10 dB of the loudest (was 14); the game supplies the balance.
        # Crackly picks (grass rustle, gravel, fuse hiss) reach the peak cap ~9 dB down.
        top=max(max(v) for v in loud.values())
        for values in loud.values():self.assertGreater(min(values),top*10**(-10/20))

if __name__=='__main__':unittest.main()
