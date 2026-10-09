# DOScraft

IBM-PC port of Townscraft, targeting a 486DX/25-class PC in 86Box.

Primary target: Intel 486DX/25 with its integrated x87 FPU, 16 MB RAM, Tseng ET4000AX ISA with 1 MB video RAM, ESDI HDD and original Sound Blaster. The ASUS ISA-486 motherboard and selected BIOS images are emulator proxies, not an authenticated 1989 Compaq reconstruction. The Everex-class 386DX/33 is a **deferred compatibility/port target**, to revisit only after the main demake meets its agreed correctness/performance goals. The main build may use 486 instructions and x87; 386 compatibility is not a current release gate.

Current stage: architecture, reproducible house-rendering investigation, and the first DOS platform scaffold. A standalone platform probe compiles; guest boot/display/disk verification is pending. There is no playable DOS game yet.

See [the port plan](PORT_PLAN.md) for hardware qualifications, the DOS platform, renderer candidates, large-world storage, tests and milestone gates. [The investigation runner](tools/audit_towns_house.py) builds isolated Towns test snapshots without modifying Towns production sources or ISOs.

The emulator checkout at `../86Box` is an independent Git repository. Game work belongs here. Generated builds and mutable runtime images are ignored.

## Build the initial platform probe

From this directory, with Python 3.10 or newer:

```powershell
python tools/fetch_dependencies.py
python tools/fetch_roms.py
python -m unittest discover -s tests -p "test_*.py"
python tools/build_pc.py --prepare-vm
```

The default target is `486dx25`: compiler flags include `-march=i486 -mtune=i486 -m80387 -mfpmath=387`, with no fast-math option. This permits native 486/x87 code; it does not rewrite the integer renderer or establish a speedup. Builds are isolated under `build/pc/486dx25`, whose `build.json` records the compiler command, CPU floor, profile hash, executable hash and VM path.

The dependencies are version/hash-pinned and extracted only under ignored `build/deps`; nothing is installed globally. Each `--prepare-vm` invocation creates a new private directory under `runtime`, never overwriting an existing world/disk image. This test boots from a custom FreeDOS floppy and uses a fresh approximately 20 MiB FAT16 scratch HDD, **not** the final large-world disk geometry. It contains no Towns source import yet.

The old scaffold can still be built explicitly with `--target 386dx33`; its separate output is `build/pc/386dx33`. That is a retained platform diagnostic, not a currently supported 386 game port. It still emulates an Intel `387`: the desired Cyrix FasMath 83D87 is not separately supported/timed by this 86Box version. No Cyrix speed multiplier or emulator rewrite has been introduced; that investigation is deferred with the 386 port.

The probe tests an 8 MiB heap allocation, explicit x87 initialization, a 32 KiB DOS file roundtrip, and Mode 13h framebuffer upload using the guest PIT clock. It restores text mode and writes `PLATFORM.TXT` on the scratch HDD (or floppy fallback). Paging is disabled in the guest startup. This is a platform diagnostic, not a game-resolution/HUD change or a renderer performance result. Host-side FAT tests do not establish that the guest successfully booted.
