# Sound assets

`masters/` retains unmodified original-quality OGG samples from Kenney's
[Impact Sounds](https://kenney.nl/assets/impact-sounds). The archive's license
is retained in `KENNEY-LICENSE.txt`; all selected source hashes and variant
groups are in `sources.json`. These samples use CC0-1.0. Eaglercraft's supplied
event mapping informed material groups/variation; no Minecraft/Eaglercraft
recordings are copied or remixed into this library.

Current packaging (host Python, NumPy and SoundFile; decoder is not in DOS):

```powershell
python tools/build_sound_bank.py --output build/assets/audio/SOUND.PAK
```

The current bank has 50 clips: the 44 Kenney variants plus 6 procedural ones
(2 explosions, 1 creeper fuse hiss, 3 zombie groans). Those three sounds have no
Kenney master, so `build_sound_bank.py` synthesizes them from fixed seeds; the
recipes are recorded in the bank's JSON report. It is 11025 Hz, unsigned 8-bit
mono and 176,661 bytes (the game accepts up to 192 KiB).
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
standing in for the Towns' four-operator one. It needs no bank data.

For a future higher-quality build, use the same original masters:

```powershell
python tools/build_sound_bank.py --rate 44100 --bits 16 --channels 2 --output build/assets/audio/future/SOUND.PAK
```

This generates a 2,814,076-byte stereo bank. The current SB1 runtime deliberately
accepts only its 8-bit mono format; the future backend must support the richer
format and appropriate media. Master samples are never overwritten by packaging.
Bank headers declare version, sample rate, bit depth, channel count, descriptors
and checksums, so target-specific repackaging stays independent of game code.
