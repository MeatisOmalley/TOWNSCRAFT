#!/usr/bin/env python3
"""Build the TOWNSCRAFT bootable CD image.

Sector 0 : IPL (boot sector, "IPL4" signature)
Sector 1+: kernel flat binary
"""
import struct
import sys

SECTOR = 2048


def main():
    if len(sys.argv) != 4:
        print("Usage: mkimage.py ipl.bin kernel.bin output.iso")
        return 1
    ipl = bytearray(open(sys.argv[1], "rb").read())
    kernel = open(sys.argv[2], "rb").read()
    if len(ipl) != SECTOR or ipl[0:4] != b"IPL4":
        print("IPL must be exactly 2048 bytes and start with IPL4")
        return 1

    nSectors = (len(kernel) + SECTOR - 1) // SECTOR
    # The IPL loads the kernel to 10800h.  TOWNS OS loads IO.SYS to the same
    # area; keep below 80000h so the load never reaches the IPL at B0000h.
    if 0x10800 + nSectors * SECTOR > 0x80000:
        print("Kernel too large: %d bytes" % len(kernel))
        return 1
    struct.pack_into("<II", ipl, 0x10, 1, nSectors)

    img = bytearray(ipl)
    img += kernel
    img += bytes(nSectors * SECTOR - len(kernel))
    # Pad to a reasonable minimum disc size.
    minSectors = 300
    total = len(img) // SECTOR
    if total < minSectors:
        img += bytes((minSectors - total) * SECTOR)
    open(sys.argv[3], "wb").write(img)
    print("%s: kernel %d bytes (%d sectors)" % (sys.argv[3], len(kernel), nSectors))
    return 0


if __name__ == "__main__":
    sys.exit(main())
