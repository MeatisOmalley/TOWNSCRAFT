# DOScraft

IBM-PC port of Townscraft, intended for a late-1989 386DX/33-class PC in 86Box.

Primary target: Everex-class 386DX/33 + 80387, using the documented ASUS emulator proxy, with a Tseng ET4000AX ISA and 1 MB video RAM. The selected VGA BIOS is a later emulation substitute. A 486DX/25 is reserved for later same-binary compatibility/performance comparisons, not a replacement baseline. See the target decision in the port plan; no verified 486 VM is supplied yet.

Current stage: architecture, reproducible house-rendering investigation, and the first DOS platform scaffold. A standalone platform probe compiles; guest boot/display/disk verification is pending. There is no playable DOS game yet.

See [the port plan](PORT_PLAN.md) for hardware qualifications, the DOS platform, renderer candidates, large-world storage, tests and milestone gates. [The investigation runner](tools/audit_towns_house.py) builds isolated Towns test snapshots without modifying Towns production sources or ISOs.

The emulator checkout at `../86Box` is an independent Git repository. Game work belongs here. Generated builds and mutable runtime images are ignored.

## Build the initial platform probe

From this directory, with Python 3.10 or newer:

```powershell
python tools/fetch_dependencies.py
python tools/fetch_roms.py
python -m unittest discover -s tests -p test_fat_image.py
python tools/build_pc.py --prepare-vm
```

The dependencies are version/hash-pinned and extracted only under ignored `build/deps`; nothing is installed globally. Each `--prepare-vm` invocation creates a new private directory under `runtime`, never overwriting an existing world/disk image. The generated `build/pc/build.json` records the executable hash and VM path. This test boots from a custom FreeDOS floppy and uses a fresh approximately 20 MiB FAT16 scratch HDD, **not** the final large-world disk geometry. It contains no Towns source import yet.

The probe tests an 8 MiB heap allocation, explicit x87 initialization, a 32 KiB DOS file roundtrip, and Mode 13h framebuffer upload using the guest PIT clock. It restores text mode and writes `PLATFORM.TXT` on the scratch HDD (or floppy fallback). Paging is disabled in the guest startup. This is a platform diagnostic, not a game-resolution/HUD change or a renderer performance result. Host-side FAT tests do not establish that the guest successfully booted.
