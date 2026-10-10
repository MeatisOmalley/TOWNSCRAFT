"""Independent byte oracle for SAVEIO.EXE's mock-provider diagnostic ONLY.

Not a production save writer, importer, world generator, or changed save policy.
"""
import struct
from save_fixture import save_image, TRACK_BYTES, BANK_TRACKS


def words(values):
    return b''.join(struct.pack('<I',v & 0xffffffff) for v in values)


def checksum(payload):
    value=0
    for b in payload:
        value=(value*31+b)&0xffffffff
    return value


def state(width,time=13579):
    fixed=words([width,8*4096+3,20*4096,11*4096+13,19,-21,17,3,
                 5*4096,19*4096,7*4096,1,2,22,4,time])
    slots=bytes(v for i in range(36) for v in (i,63-i))
    return fixed+slots


def suffix():
    chests=bytearray([2])
    for c in range(2):
        chests.extend((3+c,12+c,8+c))
        chests.extend(v for i in range(27) for v in (i,32-i))
    return bytes(chests)+words(0x87654000+i for i in range(8))


def saveio_expected_image():
    image=bytearray(save_image())
    def overlay(offset,record):
        record=record+bytes((-len(record))%TRACK_BYTES)
        image[offset:offset+len(record)]=record
    # The first width-256 v3 write leaves untouched trailing tracks. Later v4
    # writes overwrite only their own used tracks, including published headers.
    legacy=state(256)+bytes([100,1,255,0,255,0,255,0,159,0])*3072+suffix()
    overlay(0,words([0x46435354,3,checksum(legacy)])+legacy)
    column=words([4096])+words(0x73610000+i for i in range(4096))
    payload=state(32)+column+suffix()
    for bank in range(2):
        header=words([0x46435354,4,checksum(payload),len(payload),bank+1])
        overlay(bank*BANK_TRACKS*TRACK_BYTES,header+payload)
    # Approved loader-only precedence regression: later v3 replaces bank 0;
    # bank 1 remains intact, and the recognized legacy header must win.
    legacy=state(256,9876)+bytes([100,1,255,0,255,0,255,0,159,0])*3072+suffix()
    overlay(0,words([0x46435354,3,checksum(legacy)])+legacy)
    return bytes(image)
