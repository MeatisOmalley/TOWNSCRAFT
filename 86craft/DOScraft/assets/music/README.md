# Music

The game plays music on the Sound Blaster/AdLib OPL2 FM chip
(`src/platform/dos/opl_music.c`): the original Towns piece, plus any songs you
put in `midi/`. After each piece comes a 2–4 minute gap in-game (8–16 s on the
title screen), then a random piece, never the same one twice running. F6
toggles music.

## Adding songs

1. Put Standard MIDI Files (format 0 or 1, `.mid`) in `assets/music/midi/`.
   You choose which songs go in. Files there are git-ignored by default; delete
   `midi/.gitignore` if you want to commit them.
2. Check how they convert:

   ```powershell
   python tools/build_music_pack.py
   ```

   This writes `build/assets/music/MUSIC.PAK` and prints, per song: length,
   notes, instruments used, and anything that changed. Add `--no-builtin` to
   leave the Towns piece out of the rotation.
3. Build, smoke-test and publish as usual. Every audio build packs the folder
   into `MUSIC.PAK` and ships it next to `SOUND.PAK` on the boot floppy and
   ISO; the game reads `C:\MUSIC.PAK`, then `A:\MUSIC.PAK`.

## What the conversion does

- Timing comes from the file's tempo map, rounded to the game's 100 Hz timer.
- OPL2 has 9 voices. Notes are assigned to them ahead of time; when more than
  9 sound at once (often with the sustain pedal down), the oldest note is cut.
  The report counts these as "notes cut for polyphony".
- Sustain pedal, channel volume and expression are honored; velocity sets
  loudness. Pitch bend and other controllers are ignored.
- The drum channel (MIDI channel 10) is dropped: OPL2 has no drum kit here.
- Notes outside C1–B7 move by octaves.
- General MIDI instruments map to eight FM patches: piano, electric piano,
  bell (celesta, music box, vibes), pad (strings, choir, synth pads), bass,
  organ, pluck (guitar, harp) and lead (brass, reeds, synth leads). The patch
  register values live in `tools/build_music_pack.py` and are stored in the
  pack, so they can be retuned by ear without rebuilding the game.

Piano-heavy pieces arranged for one or two hands, without drums, convert best.
Busy orchestral MIDI files will lose notes to the 9-voice limit.
