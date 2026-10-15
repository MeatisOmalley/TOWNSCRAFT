# Base sound effects and shared RLE storage — 2026-10-14

The shared base executable now implements sampled Sound Blaster effects. The
386 player is deferred for this audio checkpoint. Music remains unimplemented.

## Sound assets and playback

- 44 variants in 11 material/event groups: impacts, placement, hurt, glass,
  clicks, sand and material-specific footsteps.
- Unmodified original-quality Kenney OGG masters, their hashes and CC0 license
  are retained under `assets/audio`. Eaglercraft's event mapping informed
  grouping/variation; no Minecraft/Eaglercraft recordings were copied or remixed.
- The external `SOUND.PAK` is 100,134 bytes: 11025 Hz, unsigned 8-bit mono.
  Its SHA-256 is `210050a1ff6fc743bb9467c1f71a3d0402eee7d493db03208bdd8b1fbe1eee05`.
  It is shipped beside the executable on the boot floppy and in the ISO.
- The host packer also produces a 1,591,088-byte 44100 Hz, 16-bit stereo bank
  from those same masters. That format requires a future playback backend and
  larger media; the current SB1 loader rejects it cleanly.
- Seven one-shot voices plus rain use integer mixing and bounded IRQ playback.
  The original SB1.05 uses 512-byte ping-pong buffers, single-cycle DMA and
  IRQ rearming, so effects continue during long foreground frames. Missing or
  corrupt banks select synthesized effects; absent SB hardware selects brief
  PC-speaker effects. Speaker ownership disables the conflicting PIT2 fine
  clock and uses the existing conservative video tick clock instead.

The apparent machine-gun noise reported during testing was the busy benchmark:
it deliberately ran continuous rain and triggered footsteps every eight frames
and stone impacts every sixteen. An isolated test used four silent seconds,
three individual effects one second apart, then silence. The user confirmed
three separate sounds with quiet between them, and that sound was good in the
current test. The production build excludes `DOS_BENCH_AUDIO_STRESS`.

## Validation

The exact production executable is
`c3c57866cb482da42cd6b0ab52cf0baf0b1e78ab519b9293782c69cbfdd6cd5f`.
New-world play and fresh-process reload passed in the 1990 DX/40, 8 MiB guest:
483/570 presented frames, sampled audio, 538/501 completed DMA blocks and zero
audio faults. AT movement, full-pitch look, torch edits, menus/presets, VGA
readback, distant terrain, lighting, edge streaming and V5 persistence passed.

Private isolated probes also passed with the sampled bank, with the bank
missing, and with the card removed. They reported `SB_SAMPLED`, `SB_BASIC` and
`PC_SPEAKER`, respectively; VGA readback and IRQ restoration passed in all
three. Completed diagnostics are closed, with disks/reports retained.

34 focused audio/media checks and 10 storage/crown checks pass, including
integer PCM output/silence, corruption rejection, voice priority, loop/mute,
non-repeating variants, IRQ buffer ordering/fault cleanup, both committed save
banks, raw and RLE edited columns, and byte-preserving edit-pool growth.

## Measured cost

Matched frozen Low graphics replays (160x100, view 8, distant Low) on the
1990 DX/40, 8 MiB profile measured 576 frames over three repeats per build:

| Replay | Average FPS |
| --- | ---: |
| Silent | 28.6483 |
| Busy sampled effects, initial mixer | 26.4561 |
| Busy sampled effects, active-voice mixer | 26.9489 |

The final busy workload costs 5.93% FPS versus silence. All six scene hashes
match across both builds and their repeats; zero DSP faults were reported.
This deliberately noisy replay is not a measurement of typical gameplay audio
or a promise of full-simulation FPS. Sparse tail samples are not reliable
0.1%-low statistics.

## RLE HDD conversion

The 1990/1992 players previously used V1 raw base/light records. Their V2 reader
is now shared with the existing 386 format. RLE is compression, not encryption.
The converter verifies every decoded base/light/far record, migrates both valid
V5 save banks and their referenced raw/RLE edit snapshots, and preserves player,
inventory, chests, mobs, generations and bank tails. Repeated references are
checked for conflicts and corrupt committed saves fail closed. Source VMs and
disk images remain recovery copies.

The crown-leaf immutable records occupy 24,397,824 bytes in V2. The smaller
386 layout adds 384 fixed edit-slot pairs (9,437,184 bytes), giving a
33,835,008-byte terrain file. Preserving the V1 format's full 4,096-column
editing capacity instead reserves 100,663,296 edit bytes, giving a
125,061,120-byte file versus 178,815,488 bytes in V1. Reserving fewer edit slots
is a separate capacity tradeoff, not a benefit of the RLE codec alone. The
1990/1992 capacity choice is pending before final publication.

Pool growth only appends zeroed pairs and adjusts two header fields; it keeps
all original record offsets, edit bytes and save-directory fields unchanged.
The physical 206,438,400-byte HDD geometry and CMOS do not change with either
choice. RLE disk size is separate from the game's resident RAM allocation.

Primary references: [Kenney Impact Sounds](https://kenney.nl/assets/impact-sounds),
[CC0](https://creativecommons.org/publicdomain/zero/1.0/),
[Creative Sound Blaster hardware guide](https://www.ardent-tool.com/sound/Sound_Blaster_HW_Programming_Guide_1st.pdf).
