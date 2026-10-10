"""Blank raw Towns logical save medium, without a FAT or outer envelope.

Native valid codec headers are produced by save.c, not this fixture. This
sidecar supplies only the bounded, zero-filled medium and performs no I/O.
"""


TRACK_BYTES = 8192
NUM_TRACKS = 154
BANK_TRACKS = 77
SAVE_BYTES = 1261568


def save_image() -> bytes:
    """Return the blank raw logical save medium without headers or writes."""
    return bytes(SAVE_BYTES)
