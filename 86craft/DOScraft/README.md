# DOScraft

IBM-PC port of Townscraft, targeting a 486DX/25-class PC in 86Box.

Primary target: Intel 486DX/25 with its integrated x87 FPU, 16 MB RAM, Tseng ET4000AX ISA with 1 MB video RAM, ESDI HDD and original Sound Blaster. The ASUS ISA-486 motherboard and selected BIOS images are emulator proxies, not an authenticated 1989 Compaq reconstruction. The Everex-class 386DX/33 is a **deferred compatibility/port target**, to revisit only after the main demake meets its agreed correctness/performance goals. The main build may use 486 instructions and x87; 386 compatibility is not a current release gate.

Current stage: direct port first; optimizations later. A pinned 50-file Towns source snapshot is tracked under `vendor/towns`. The unchanged gameplay/fixed-point renderer compiles to 16 DOS-target objects with assembly ABI checks. The 486 guest now boots DOS and passes the platform heap/FPU/no-paging/disk checks. The 320x240 Mode X display probe passes all four VRAM-plane readbacks, restores DOS text mode automatically, and its held test pattern has been visually checked. Automated keyboard exit is not yet confirmed; game input/audio and gameplay remain untested. There is no playable DOS game yet. No new renderer optimization has been implemented. See [the recorded diagnostic evidence](tests/baselines/2026-10-09-pc-platform.json).

See [the port plan](PORT_PLAN.md) for the direct-port parity gate, and the separate [optimization backlog](OPTIMIZATION_BACKLOG.md) for work deferred until port acceptance. [The investigation runner](tools/audit_towns_house.py) builds isolated Towns test snapshots without modifying Towns production sources or ISOs.

The emulator checkout at `../86Box` is an independent Git repository. Game work belongs here. Generated builds and mutable runtime images are ignored.

## Build the initial platform probe

From this directory, with Python 3.10 or newer:

```powershell
python tools/fetch_dependencies.py
python tools/fetch_roms.py
python -m unittest discover -s tests -p "test_*.py"
python tools/build_pc.py --prepare-vm
python tools/build_pc.py --probe display --prepare-vm
python tools/build_pc.py --probe display --hold-display --prepare-vm
python tools/import_towns.py --verify
python tools/build_imported.py
```

The default target is `486dx25`: compiler flags include `-march=i486 -mtune=i486 -m80387 -mfpmath=387`, with no fast-math option. This permits native 486/x87 code; it does not rewrite the integer renderer or establish a speedup. Builds are isolated under `build/pc/486dx25`, whose `build.json` records the compiler command, CPU floor, profile hash, executable hash and VM path.

The dependencies are version/hash-pinned and extracted only under ignored `build/deps`; nothing is installed globally. Each `--prepare-vm` invocation creates a new private directory under `runtime`, never overwriting an existing world/disk image. These probes use a custom FreeDOS floppy and fresh approximately 20 MiB FAT16 scratch HDD, **not** final disk geometry. Official 86Box v6 lacks the desired Wren V timing preset and silently substitutes RAM Disk for it; diagnostic profiles now explicitly use supported `1989_3500rpm`, a generic timing proxy. The desired ESDI/Wren hardware target is unchanged.

Launch an exact prepared diagnostic directory with `python tools/run_vm.py --vm "<VM directory printed by the build>"`. Add `--dry-run` to validate without launching. The launcher checks the hardware, ROM hashes and actual boot-media executable hash; it creates/replaces no images. First boot requires CMOS configuration. The probes are not the game, and the compiled import intentionally leaves DOS hardware services unresolved.

### First-boot BIOS settings

Select BIOS defaults, then Standard CMOS: drive A **1.44 MB 3.5-inch**, drive B absent, first HDD **Type 2 only when shown as 615 cylinders / 4 heads / 17 sectors**, second HDD absent, VGA display and keyboard installed. In Advanced CMOS set **System Boot Up Sequence to `A:, C:`**. Save with Write to CMOS and Exit. The temporary HDD deliberately has no bootloader; `C:, A:` can hang at the system summary because its MBR has a signature but no executable boot code. Do not use Hard Disk Utility or format anything.

The ordinary display probe holds its pattern for three guest seconds and writes `C:\DISPLAY.TXT`; `--hold-display` creates separate test media that waits for Escape for visual inspection. It does not change the game's UI or renderer. A fresh VM may reuse a known-good same-machine `nvr/isa486.nvr` copy without sharing disk images. Leave working VMs and their reports intact. Keyboard Requires Capture can remain unchecked; if checked, capture the guest before typing. A serial mouse and DOS driver will be required later, not for these probes.

Mode X is VGA presentation, not a new 3D rendering engine. The current display backend transports the existing 320x240 indexed image, including its HUD, without changing the painter renderer. Latest user guidance allows an evidence-based choice of resolution; 320x240 display with the existing doubled 160x100 3D option is the initial candidate, not an immutable resolution requirement. Compare full-resolution rendering and transfer costs after the playable port exists.

The old scaffold can still be built explicitly with `--target 386dx33`; its separate output is `build/pc/386dx33`. That is a retained platform diagnostic, not a currently supported 386 game port. It still emulates an Intel `387`: the desired Cyrix FasMath 83D87 is not separately supported/timed by this 86Box version. No Cyrix speed multiplier or emulator rewrite has been introduced; that investigation is deferred with the 386 port.

The probe tests an 8 MiB heap allocation, explicit x87 initialization, a 32 KiB DOS file roundtrip, and Mode 13h framebuffer upload using the guest PIT clock. It restores text mode and writes `PLATFORM.TXT` on the scratch HDD (or floppy fallback). Paging is disabled in the guest startup. This is a platform diagnostic, not a game-resolution/HUD change or a renderer performance result. Host-side FAT tests do not establish that the guest successfully booted.
