# Sound assets

`masters/` retains unmodified original-quality OGG samples from Kenney's
[Impact Sounds](https://kenney.nl/assets/impact-sounds). The archive's license
is retained in `KENNEY-LICENSE.txt`; their source hashes are in
`sources.json`. These samples use CC0-1.0. Eaglercraft's supplied
event mapping informed material groups/variation; no Minecraft/Eaglercraft
recordings are copied or remixed into this library.

Current packaging (host Python, NumPy and SoundFile; decoder is not in DOS):

```powershell
python tools/build_sound_bank.py --output build/assets/audio/SOUND.PAK
```

`picks.json` names the takes each event plays, chosen by ear in the sound
picker: Kenney masters, other recordings retained unmodified in `extra/` (their
authors, licenses, pages and hashes are in `extra/sources.json`), or a sound
synthesized from a fixed seed (`procedural/...`). `file#n` is the nth sound of a
recording split at its pauses. A group with several takes plays one at random,
never the same one twice in a row. A group without picks stays empty: the game
then plays its synthesized Towns sound (block breaks, player and mob hurt,
click) or nothing (mob calls, water). Credits, including the one CC-BY
recording, are in [CREDITS.md](CREDITS.md) and on the ISO's `README.TXT`.

The current bank has 30 clips, 11025 Hz unsigned 8-bit mono, 122,739 bytes (the
game accepts up to 512 KiB and 128 clips). Unpicked so far: stone and glass
breaks, player hurt, click, all pig and sheep sounds, chicken calls and hurt,
creeper death. Groups, in `audio_bank.h` order:
block breaks (crunch/dirt, stone, wood, sand, glass, gravel), footsteps (grass,
stone, wood, sand, gravel), place, player hurt, click, explosion, creeper fuse
hiss, zombie groan, then per-mob calls, hurt and death cries (pig, sheep,
chicken, zombie, creeper), splash and swim.

How the game uses them, as in Minecraft:

- Mining hits replay the block's footstep at a quarter of the break level and
  half pitch, every fourth tick while the button is held.
- Pigs, sheep, chickens and zombies call about every 6 seconds each, from up to
  16 blocks away. A death cry falls back to the hurt cry, then to the original
  pitched hurt sound.
- Entering water splashes (louder the faster you fall in); every 2 blocks swum
  plays a stroke.
It is a separate `SOUND.PAK` file alongside the executable on the boot floppy
and in the ISO. Playback checks `C:\SOUND.PAK`, then `A:\SOUND.PAK`. Missing,
corrupt or unsupported banks use the basic synthesized effects. A missing
Sound Blaster uses basic PC-speaker feedback.

Levels: every clip's loudest 50 ms is normalized to the same RMS (0.26 of full
scale, peaks capped at 0.95), and fades in only across the silence before its
onset, so footstep and click attacks survive. The game then applies the Towns
per-effect levels (step 120, click 140, crunch 200, wood/place 230, explosion
255, ...) and one output gain; the mixer soft-clips loud overlaps.

Music is the original Towns score played on the Sound Blaster/AdLib OPL2 FM
chip (`src/platform/dos/opl_music.c`), with a two-operator electric piano
standing in for the Towns' four-operator one. It needs no bank data. Songs you
add as MIDI files join the rotation through `MUSIC.PAK`; see
[the music guide](../music/README.md).

For a future higher-quality build, use the same original masters:

```powershell
python tools/build_sound_bank.py --rate 44100 --bits 16 --channels 2 --output build/assets/audio/future/SOUND.PAK
```

This generates a 1,961,948-byte stereo bank. The current SB1 runtime deliberately
accepts only its 8-bit mono format; the future backend must support the richer
format and appropriate media. Master samples are never overwritten by packaging.
Bank headers declare version, sample rate, bit depth, channel count, descriptors
and checksums, so target-specific repackaging stays independent of game code.
