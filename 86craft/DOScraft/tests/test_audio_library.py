from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_sound_bank import checksum,MAGIC
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
BANK=ROOT/'build/assets/audio/SOUND.PAK'

class AudioLibraryTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file() and BANK.is_file(),'native Zig and packaged bank required')
    def test_real_bank_and_integer_mixer(self):
        with tempfile.TemporaryDirectory(prefix='dos-audio-library-') as folder:
            exe=Path(folder)/'test.exe';dos=ROOT/'src/platform/dos'
            r=subprocess.run([str(x) for x in [ZIG,'cc','-target','x86-windows-gnu','-O2','-Wall','-Wextra','-Werror',
                '-I',dos,ROOT/'tests/audio_library_native.c',dos/'audio_bank.c',dos/'audio_voices.c','-o',exe]],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=subprocess.run([str(exe),str(BANK)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertIn('PASS:',r.stdout)

    def test_masters_retained_and_external_bank_declares_format(self):
        from hashlib import sha256
        p=ROOT/'assets/audio';m=json.loads((p/'sources.json').read_text())
        self.assertEqual(m['license'],'CC0-1.0');self.assertEqual(len(m['files']),44)
        for f in m['files']:self.assertEqual(sha256((p/'masters'/f['file']).read_bytes()).hexdigest(),f['sha256'])
        b=BANK.read_bytes();magic,version,rate,fmt,count,n,total=struct.unpack_from('<8s6I',b)
        self.assertEqual((magic,version,rate,fmt,count),(MAGIC,1,11025,8|(1<<16),50))
        self.assertEqual(n,len(b)-32-16*count);self.assertEqual(total,checksum(b[32+16*count:]))

    def test_audio_overlay_is_only_event_selection_and_replays(self):
        import dos_game_audio,build_imported
        data=(build_imported.VENDOR/'src/game.c').read_bytes();result,steps=dos_game_audio.adapt('game.c',data)
        replay=data
        for s in steps:
            self.assertEqual(replay.count(s['before'].encode()),s['count'])
            replay=replay.replace(s['before'].encode(),s['after'].encode())
        self.assertEqual(result,replay);self.assertIn(b'dos_sound_step',result)
        # Minecraft's spacing for both new and loaded worlds; the title screen keeps 8-16 s.
        self.assertEqual(result.count(b'music_set_gap(60000);'),2)
        self.assertNotIn(b'music_set_gap(12000);',result);self.assertIn(b'music_set_gap(800);',result)
        self.assertEqual(dos_game_audio.adapt('world.c',b'test'),(b'test',[]))
        data=(build_imported.VENDOR/'src/mobs.c').read_bytes();result,steps=dos_game_audio.adapt('mobs.c',data)
        self.assertIn(b'dos_sound_zombie(',result);self.assertEqual(len(steps),2)

    @unittest.skipUnless(BANK.is_file(),'packaged bank required')
    def test_percussive_attacks_survive_and_levels_are_even(self):
        import math
        b=BANK.read_bytes();count=struct.unpack_from('<I',b,20)[0];payload=b[32+16*count:]
        loud={}
        for i in range(count):
            group,offset,frames,_=struct.unpack_from('<4I',b,32+16*i);x=[v-128 for v in payload[offset:offset+frames]]
            self.assertLessEqual(max(abs(v) for v in x),121)
            w=551;loud.setdefault(group,[]).append(max(math.sqrt(sum(v*v for v in x[a:a+w])/len(x[a:a+w]))
                                                     for a in range(0,max(1,len(x)-w),137)))
        # Footsteps (3-5) keep their attack: no longer ~14 dB below block breaks.
        for group in (3,4,5):self.assertGreater(min(loud[group]),0.4*min(loud[0]))
        # Every clip is within 8 dB of the loudest (was 14); the game supplies the balance.
        top=max(max(v) for v in loud.values())
        for values in loud.values():self.assertGreater(min(values),top*10**(-8/20))

if __name__=='__main__':unittest.main()
