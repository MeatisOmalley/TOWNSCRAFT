#!/usr/bin/env python3
"""Create a test-only ROM directory for running TOWNSCRAFT in Tsugaru
without real FM TOWNS ROM images.  FMT_SYS.ROM holds the stub boot code in
its last 32KB; the other ROM files are blank.  This is not a replacement for
the real ROMs; it only boots TOWNSCRAFT's IPL for automated testing."""
import os
import sys


def main():
    stub, outDir = sys.argv[1], sys.argv[2]
    code = open(stub, "rb").read()
    assert len(code) == 0x8000, len(code)
    os.makedirs(outDir, exist_ok=True)
    sysRom = bytes(256 * 1024 - 0x8000) + code
    blanks = {"FMT_DOS.ROM": 512 * 1024, "FMT_FNT.ROM": 256 * 1024,
              "FMT_DIC.ROM": 512 * 1024, "FMT_F20.ROM": 512 * 1024}
    open(os.path.join(outDir, "FMT_SYS.ROM"), "wb").write(sysRom)
    for name, size in blanks.items():
        open(os.path.join(outDir, name), "wb").write(bytes(size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
