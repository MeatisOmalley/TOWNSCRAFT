# Local 386SX-25 VM

`ISO/Launch-DOScraft-386.cmd` opens the private VM selected by
`build/game/386sx25/latest-play-vm.txt`. This VM has its own boot floppy,
compact world disk and CMOS. Its boot floppy contains the merged shared
386/486 executable; it does not automatically refresh from the older published
486 ISO.

386 CPUs now start at the Very Low preset: 80x50 (Potato), four-block near
draw distance. Low remains selectable; the default for later CPUs is unchanged.
After rebuilding with `prepare_386_vm.py --build`, use
`python tools/refresh_386_vm.py` while the existing player VM is stopped to
install the executable into a new private VM with the original HDD and CMOS
preserved. This refresh does not create a new world.

The imported `profiles/1990-386sx25-family.cfg` selects the DTK PM-1630C,
386SX at 25 MHz, 4 MiB RAM, no 387, Trident TVGA8900B with 512 KiB, Sound
Blaster 1.5, Microsoft serial mouse, and Conner CP3044 IDE timing. The HDD
geometry is 977 cylinders, 5 heads, 17 sectors (42,519,040 bytes). Audio remains
deferred by the game. The fresh seed-4242 world is converted to the compact
format, including streamed lighting and the distant map. Newly prepared VMs
use the V3 edit log at the 4 MiB new media get (8,192 sectors; a
28,592,128-byte terrain file): per-block edits keep most edited columns out
of the log, so every column can be edited, where V2's 384 edit-slot pairs
covered 9%. VMs prepared before that took what the 40 MB volume had left
(30,668 sectors, leaving 1 MiB free). Existing player saves are separate;
`tools/log_player_vm.py` migrates a stopped V2 player VM, or moves a V3 one
onto the 4 MiB log (about 11 MiB more free space), into a new clone.

The emulator is built from `MeatisOmalley/86Box` commit
`cc2ff8fe3058c4f8eec70cfac82b1909894d458f`, which registers the Trident
TVGA8900 as a 16-bit ISA card. Its runtime is isolated in
`build/deps/86box-doscraft-386`. The setup script downloads checksum-pinned DTK
and Trident ROMs from the existing pinned 86Box ROM revision.

## Recreate a private VM

From this directory, with the existing Python, DJGPP and MSYS2 UCRT64
dependencies available:

```powershell
python tools/build_386_86box.py
python tools/bake_far_field.py --terrain build/checks/large-world-proto/terrain1024-far.bin --output build/checks/large-world-proto/terrain1024-crown-leaves.bin --crown-leaves
python tools/prepare_386_vm.py --build
powershell -NoProfile -File ISO/Launch-DOScraft-386.ps1 -ValidateOnly
```

Preparation creates a new disk and changes only the 386 launcher pointer.
It uses `build/checks/large-world-proto/terrain1024-crown-leaves.bin` by default; pass
`--terrain` to supply another fresh compatible V1 bake. The converter rejects
V1 disks containing player edits.

The crown bake keeps only real leaves sharing a face with each trunk's highest
log in the distant representation. To update an existing stopped VM without
replacing its logical world or save, use `tools/crown_leaves_media.py --vm <vm>`.
This produces a checked clone; `--install-pointer <pointer-file>` selects it.

The manually configured CMOS is backed up at
`build/game/386sx25/bios/dtk386.nvr`. Preparation reuses that file automatically
when present, or accepts `--bios <path-to-dtk386.nvr>`. The CMOS memory, display,
floppy, disk-type and checksum fields are checked before copying. It belongs
to the DTK motherboard; a 486 motherboard's CMOS is incompatible.

Without a saved CMOS, use physical F1 at the first-boot prompt. Configure A:
as 1.44 MB, B: as 1.2 MB, one fixed disk of type 17, VGA/EGA display, and no
coprocessor. Accept 640 KiB base, 384 KiB shadow and 3072 KiB extended memory;
save and exit using the BIOS instructions. Close the VM before backing up its
CMOS or copying its writable disk.

The existing automated game smoke assumes the 486's full-resolution and
Medium/High settings, which are unavailable on the 386. It is not a passing
386 acceptance test. BIOS/DOS boot verification does not establish gameplay,
save/reload or performance acceptance.

Local boot validation on October 14, 2026: the saved DTK CMOS matches the
profile, FreeDOS recognizes the 40 MB C: drive, CuteMouse and CWSDPMI load,
and the shared executable reaches the DOScraft title screen. The launcher
validation passed with the VM stopped. Gameplay and save/reload remain untested.

## Performance baseline

```powershell
python tools/benchmark_386.py --label very-low-far-very-low-20261014
```

This uses a new private compact-world HDD and the saved DTK CMOS. The benchmark
holds Very Low near graphics (80x50, four blocks) and Very Low distant terrain,
performs one warmup pass, then records three repeats of six scenes with 32
measured frames per scene. Frame timing uses the guest PIT. The replay freezes
simulation; it measures graphics and world-update work, not unrestricted game
FPS. Edit-call time is reported separately from the following rendered frame.
It leaves the player pointer and disk untouched and closes only its completed
private diagnostic emulator. Raw reports and disk images remain under `runtime`;
the aggregate report is saved under `build/bench/386sx25/<label>/performance.json`.

The [October 14 Very Low baseline](research/386sx25-very-low-far-very-low-2026-10-14.md)
averaged 6.008 FPS over 576 frames. Walking was 6.018 FPS, the house 4.557 FPS,
and block-edit calls plus the following frame averaged 3.399 cycles/s. This is
a deterministic replay baseline; save/reload acceptance remains separate.
