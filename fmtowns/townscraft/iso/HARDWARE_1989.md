# Late-1989 Townscraft launcher minimum

All 19 numbered/compatibility `.cmd` launchers use this profile through
`../tools/run_1989.ps1`. Their ISO selection and ISO bytes are unchanged.

| Hardware | Selection |
| --- | --- |
| Towns generation | November 1989 1F/2F/1H/2H family, 2H-class expanded machine |
| CPU | Intel 386DX, 16 MHz |
| Main RAM | 8 MB, the late-1989 generation's expansion ceiling |
| Numeric coprocessor | Intel 80387 fitted |
| HDD | One 200,000,000-byte raw SCSI disk, SCSI ID 0 |
| CD-ROM | Existing internal drive, forced to 1x |
| Floppies | The emulator's two drives remain available; no disk is auto-mounted |
| Graphics/audio/input | Existing Towns graphics, FM/PCM sound, mouse and pad hardware |

This is the best expanded **1989 Towns generation**, not the fastest PC
architecture sold that year. The CPU remains 16 MHz; later 20/25/33 MHz
Towns configurations, 486 models, larger RAM configurations, faster CDs
and emulator fast-I/O shortcuts are not part of this target. The selected
200 MB consumer SCSI capacity is the user's choice, not a claim that no
larger drive existed in 1989. It replaces the capacity of the 2H's factory
40 MB disk in our emulated setup; it does not add a second internal disk.

## Exact settings

```text
-TOWNSTYPE 2F -FREQ 16 -MEMSIZE 8 -USEFPU -CDSPEED 1
-HD0 <per-ISO HDD image> -NORMALSCSI -NORMALFD
-DIFFMOUSE -DONTAUTOSAVECMOS -YESWAIT -AUTOSCALE -MAXIMIZE
```

Tsugaru has no separate `2H` command-line value. `2F` selects the shared
second-generation machine ID used by 1F/2F/1H/2H, with the HDD supplied
separately. The launchers continue to use the existing test ROM and their
own historical CD images. This is not a new ROM installation.

## Disk safety and persistence

On first launch only, the helper creates
`runtime_1989/<ISO-basename>.HDD0.h0` next to the selected ISO. Each ISO,
including the compatibility ISO in folder 17, has a different disk path.
The raw image is blank/unformatted; no Towns OS or game files are installed.
Future writes persist in this file. Existing images must have the expected
size or the launcher stops without modifying them. No existing disk or
save file is resized, formatted, replaced or migrated.

Writable `runtime_1989/` directories are ignored by Git. Back up these
images separately if they acquire useful data; the scripts and this profile
are versioned. Do not run two instances of the same ISO against its one HDD
simultaneously.

The current game does **not** have an HDD save/world-streaming driver and
does **not** issue x87 math instructions. Attaching this hardware makes it
available for future work, not automatically used. Existing floppy saving
and RAM-dependent world sizing are unchanged. In particular, this change
does not promise that a larger 8 MB world will fit the old save-floppy format.

Tsugaru's `-NORMALSCSI` avoids its explicit fast mode; it does not identify
or calibrate the seek/cache behavior of a particular real 200 MB drive.
Do not interpret this as a cycle-exact Logitec HDD performance simulation.

## Evidence

- [Fujitsu's PC history](https://www.fujitsu.com/jp/group/fccl/about/resources/news/40th-portal/pdf/40thanniv.pdf)
  dates the HDD-equipped 1H/2H successors to November 1989.
- The [MAME FM Towns driver](https://github.com/mamedev/mame/blob/master/src/mame/fujitsu/fmtowns.cpp)
  documents the second-generation 386DX-16, 8 MB maximum, 20/40 MB factory
  disks, optional 80387 and shared machine ID 2. The original spring-1989
  Model 1/2 ceiling was 6 MB; do not confuse that with this later generation.
- [Logitec's manufacturer history](https://www.logitec.co.jp/company/product-history/)
  lists consumer external SCSI units of 40, 100 and 200 MB in 1989. This
  establishes period capacity, not specific model firmware compatibility
  or a measured transfer rate on a Towns.
- Local `src/main_cui/argv/townsargv.cpp` and `src/towns/townsdef/townsdef.cpp`
  verify the flags and `2F` mapping. `src/towns/towns.cpp` applies `-USEFPU`,
  the RAM setting, SCSI image and `-CDSPEED 1`.

## Verification

Run any `.cmd` with `-ValidateOnly` to print its resolved paths and complete
argument list without creating a disk or starting the emulator. The default
double-click path creates only a missing HDD and then runs the emulator.

Verified on 2026-10-08: all 19 `.cmd` wrappers resolved the expected ISO and
hardware arguments from a different working directory, with 19 distinct HDD
paths. The latest (#18) actual CUI launcher reached gameplay with an advancing
frame/tick counter and a 256-block world, reported the enabled FPU and attached
SCSI disk, and exited cleanly. Its ISO hash and existing HDD bytes were unchanged.
A separate wrong-sized disk fixture was rejected without modifying its contents.
Historical ISOs were configuration-checked, not all individually boot-tested.

Existing benchmark version 1 is intentionally not rewritten. Its old
2 MB `minimum` and 4 MB `high` measurements retain their original labels;
new 8 MB results must be identified separately.
