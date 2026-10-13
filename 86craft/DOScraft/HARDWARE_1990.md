# June 1990 DOScraft player VM

The complete emulator source is committed and pushed to
[`doscraft-isa-timing` at `af84c23312d3585cb946b7eaa43c3970a70a6417`](https://github.com/MeatisOmalley/86Box/commit/af84c23312d3585cb946b7eaa43c3970a70a6417).
This includes the previously unpushed DX40 entry, the ET4000 bus fixes,
external-cache enable/timings, DRAM timings, and SCSI drive presets. Check out
this revision directly for the June 1990 runtime; do not apply the hardware
patch again. The patch file remains a reference against the older `31be77334`
base. `tools/build_patched_86box.py` still reproduces that older baseline,
rather than the complete hardware runtime. To build the complete revision,
use its UCRT64 CMake `regular` preset and the static flags in that script,
then copy `build-ucrt64/src/86Box.exe` into `build/deps/86box-doscraft-1990`.
Record its new SHA-256 in prepared VM manifests before launching; existing
published binaries and their validated hashes have not been replaced.

Launch the upgraded player through [`ISO/Launch-DOScraft.cmd`](ISO/Launch-DOScraft.cmd).
The launcher points to a private copy of the former player's 20 MiB writable
disk. The former VM and its save disk remain intact.

The profile is [`profiles/1990-dx40.cfg`](profiles/1990-dx40.cfg): ASUS ISA-486,
486DX at 40 MHz (overclocked), 8 MiB RAM, Tseng ET4000AX ISA, Adaptec AHA-1542A
ISA SCSI, and a Seagate ST4385N Wren Runner timing proxy. This is an ISA SCSI
controller, not VLB; VLB came later. The ST4385N [technical sheet dated June
26, 1990](https://files.mpoli.fi/drivers_1/HD2/seagate/scsi/st4385n.txt)
specifies 3600 RPM, not 5400 RPM. The VM retains the 20 MiB disk image so
existing saves remain usable; it does not simulate the Wren Runner's full
330 MiB capacity. The [AHA-1540A/1542A manual](https://ftpmirror.your.org/pub/misc/bitsavers/pdf/adaptec/Adaptec_AHA-1540A_1542A_Users_Manual_1989.pdf)
is dated 1989.

The target is 256 KiB of fast external SRAM cache and fast page-mode DRAM.
86Box cannot represent the L2 capacity, hit rate, SRAM access time, or SIMM
speed directly. The private emulator build approximates these with two-cycle
external-cache access and six-cycle DRAM access at the 40 MHz clock. It also
keeps the ASUS/SiS external-cache switch enabled to represent installed SRAM;
the inherited CMOS otherwise left it off. A guest probe read SiS register 61h
as BDh after boot, confirming that the external-cache enable bits are set.
Treat performance changes from this timing model as emulator estimates, not a
measurement of a physical 1990 PC.

The motherboard's CMOS HDD type is **None** because the SCSI adapter's option
ROM provides BIOS INT 13h disk services. A private boot test detected the SCSI
ROM at C8000, successfully read the C: disk through INT 13h, and wrote a marker
file to C:. No BIOS change is needed for this prepared player VM. If CMOS is
reset later, set both motherboard HDD entries to **None** and leave the
Adaptec SCSI BIOS enabled at C8000.

A private DOS boot and cache probe completed. The game initially stopped at a
DOS-port 16 MiB guard, and its fixed 9 MiB arenas also exceeded the 8 MiB
machine. After RAM-scaled arena sizing, the 8 MiB SCSI profile completed a
three-repeat Low graphics/world-update replay and a normal-game new-world and
fresh-process reload smoke, including movement, torch edits, VGA readback and
save/load. The reports are in
`tests/benchmarks/2026-10-12/cw-profile-1990-scsi.json` and
`normal-game-8m-smoke.json`. The tested normal build was published to the
player ISO with a copied save disk; the previous player VM and ISO are retained.
The 2 MiB world-size path has not yet been tested on a 2 MiB DOS VM.

The new runtime is `build/deps/86box-doscraft-1990`; the source changes are in
[`patches/86box-1990-hardware.patch`](patches/86box-1990-hardware.patch). The
launcher validates the selected ISO, boot floppy, emulator, ROMs, and hardware
profile before opening the VM. The game still boots from the ISO's extracted
floppy image and uses C: for its writable world data.

The existing `tools/publish_game.py` rebuild path still prepares the older
16 MiB ESDI hardware. After a future game build, its new player VM will need
the June 1990 hardware reapplied before changing the launcher pointer. Do not
run `tools/upgrade_1990_vm.py` against an already-upgraded VM; it expects the
original 16 MiB ESDI CMOS seed.
