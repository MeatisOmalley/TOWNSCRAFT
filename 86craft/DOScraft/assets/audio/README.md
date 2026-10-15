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

The current 44-variant bank is 11025 Hz, unsigned 8-bit mono and 100,134 bytes.
It is a separate `SOUND.PAK` file alongside the executable on the boot floppy
and in the ISO. Playback checks `C:\SOUND.PAK`, then `A:\SOUND.PAK`. Missing,
corrupt or unsupported banks use the basic synthesized effects. A missing
Sound Blaster uses basic PC-speaker feedback.

For a future higher-quality build, use the same original masters:

```powershell
python tools/build_sound_bank.py --rate 44100 --bits 16 --channels 2 --output build/assets/audio/future/SOUND.PAK
```

This generates a 1,591,088-byte stereo bank. The current SB1 runtime deliberately
accepts only its 8-bit mono format; the future backend must support the richer
format and appropriate media. Master samples are never overwritten by packaging.
Bank headers declare version, sample rate, bit depth, channel count, descriptors
and checksums, so target-specific repackaging stays independent of game code.
