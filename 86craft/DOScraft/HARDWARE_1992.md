# Separate 1992-class DOScraft VM

The complete emulator source is committed and pushed to
[`codex/doscraft-1992-hardware` at `4c26b869560a45435fff843f6d2a357c6f311b5a`](https://github.com/MeatisOmalley/86Box/commit/4c26b869560a45435fff843f6d2a357c6f311b5a).
Check out this revision directly to reproduce the DX2-80 and SCSI timing
changes; do not apply the reference hardware patch again. Build with the
UCRT64 CMake `regular` preset and the static flags in
`tools/build_patched_86box.py`, then place the executable in
`build/deps/86box-doscraft-1992`. A rebuilt binary needs its new SHA-256 in
the prepared VM manifest. Existing published binaries and validated hashes
have not been replaced.

Launch with [`ISO/Launch-DOScraft-1992.cmd`](ISO/Launch-DOScraft-1992.cmd).
It uses its own writable HDD with the same prebaked 1024x1024 seed as the
current 1990 VM. Their world edits and saves are independent. The previous
1992 VM and its 256-world disk are preserved.

The 86Box profile is [`profiles/1992-dx2-80.cfg`](profiles/1992-dx2-80.cfg):
ABIT AB-AV4 VLB motherboard, Intel i486DX2-66 at 80 MHz core/40 MHz bus,
16 MiB RAM, ET4000/W32 VLB with 1 MiB video RAM, and AHA-1542C 16-bit ISA
SCSI. The hard disk uses a Quantum ProDrive 1050S timing proxy, including its
4500 RPM rotation, 18 ms full-stroke seek, and 512 KiB read-cache capacity.
The private disk image is 206,438,400 bytes, holding the prebaked seed and its
edit/save space; it does not emulate the physical drive's full
1.05 GB capacity. 86Box also does not model 256 KiB/12 ns L2 cache capacity
and SRAM latency accurately. Its CPU and memory timing values are approximations.
Quantum reports that the 1050 entered mass production in June 1992 in its
[1994 filing](https://investors.quantum.com/financial-information/sec-filings/content/0000709283-94-000011/0000709283-94-000011.pdf).
The 512 KiB DisCache figure is from a copy of the [August 1992 700/1050/1225S
product manual](https://www.stason.org/TULARC/pc/hard-drives-hdd/quantum/PRODRIVE-700-S-700MB-3-5-HH-SCSI2-FAST.html).
The AHA-1542C appears in Adaptec's [September 1992 user guide](https://bitsavers.org/pdf/adaptec/pc/AHA-1540A/510218-00_AHA-1540_Bus_Master_ISA_to_SCSI_Users_Guide_199209.pdf).

The W32 VLB ROM bundled by 86Box identifies itself as **December 13, 1993**.
This is a later firmware proxy, not proof that this exact card/ROM was sold in
1992. The AB-AV4 motherboard ROM itself is dated June 6, 1992. Its CMOS
currently has both motherboard HDD types set to **None**, with 16 MiB selected.
The AHA-1542C option ROM at C8000 owns the SCSI disk; the motherboard HDD
configuration should stay at None. A private DOS boot successfully wrote to C:
through this path. A guest read of the SiS 460 cache register confirmed that
external cache is enabled. The normal game executable from the shared ISO
passed a private new-world and fresh-process reload smoke on this hardware,
including movement, torch edits, VGA readback, and save/load; see
`tests/benchmarks/2026-10-12/normal-game-1992-shared-smoke.json`.

Both player VMs now use the same `ISO/DOSCRAFT.ISO`. 86Box boots A: rather
than this ISO directly, so each VM keeps an extracted `boot.img` from that
ISO; its hardware profile, CMOS, and writable disk remain separate. The
current 1992 VM has no second ISO file. Its launcher automatically synchronizes
the extracted boot floppy with the published release before a stopped launch.
`tools/refresh_1992_vm.py` copies a
stopped 1992 VM's save disk and CMOS into a new VM with a boot floppy from the
current shared ISO, preserving the previous VM. Its `--large-terrain` option
installs the exact published prebaked seed on a fresh HDD and runs the scripted
new-world/reload smoke on a separate copy before switching the launcher pointer.
The separate launcher checks the
shared ISO, boot floppy, emulator, ROMs, disk geometry, and hardware profile.
Use `-ValidateOnly` with the PowerShell launcher for a read-only validation.

The emulator is the existing pinned DOScraft 86Box fork plus
[`patches/86box-1992-hardware.patch`](patches/86box-1992-hardware.patch). The
patch adds the DX2/80 clock entry and Quantum drive timing proxies. Its built
runtime is kept in ignored `build/deps/86box-doscraft-1992`, separate from the
existing player emulator. `tools/fetch_roms.py` pins the additional motherboard,
video, and SCSI ROMs by SHA-256.
