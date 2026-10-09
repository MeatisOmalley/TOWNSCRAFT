"""Pinned Towns temporary terrain backing fixture; not a save format.

Record ownership lives in RAM in Towns. This blank sidecar contains only the
validated disk header and two fixed, disjoint slots per column.
"""
import struct


SECTOR_BYTES = 512
# Towns memcmp(..., magic, 16) excludes the C string's trailing NUL.
MAGIC = b'TSC-TERRAIN-TEMP'
VERSION = 1
COLUMNS = 256
SECTORS_PER_COLUMN = 24
SLOTS_PER_COLUMN = 2
SLOT_BYTES = SECTORS_PER_COLUMN * SECTOR_BYTES
IMAGE_BYTES = SECTOR_BYTES + COLUMNS * SLOTS_PER_COLUMN * SLOT_BYTES


def terrain_image() -> bytes:
    """Return a fresh temporary terrain image, not a save format, without I/O."""
    header = MAGIC + struct.pack('<III', VERSION, COLUMNS, SECTORS_PER_COLUMN)
    return header + bytes(IMAGE_BYTES - len(header))
