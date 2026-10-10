"""Independent full-byte oracle for the bounded WORLDIO width256/v3 fixture.

No C-generated inputs or parser inference: reproduce the declared flat terrain,
two edits, player/inventory/chest and the two known mob field records.
"""
import struct


def words(*values):
    return struct.pack('<'+'I'*len(values),*(v & 0xffffffff for v in values))


def worldio_expected_image():
    terrain=bytearray(256*256*48)
    terrain[0::48]=bytes([11])*(256*256)  # bedrock
    terrain[1::48]=bytes([2])*(256*256)   # grass
    for cz in range(16):
        for cx in range(16):
            terrain[((cz*16+3)*256+cx*16+3)*48+2]=(1,4,6,21)[(cx+3*cz)%4]
    terrain[(49*256+49)*48+40]=5
    terrain[(49*256+50)*48+40]=16
    rle=bytearray(); last=terrain[0]; run=0
    for block in terrain:
        if block!=last or run==255:
            rle.extend((run,last)); last=block; run=0
        run+=1
    rle.extend((run,last))
    inventory=bytearray(72); inventory[4:6]=bytes((6,9))
    chest=bytearray(58); chest[:4]=bytes((1,51,2,49)); chest[10:12]=bytes((1,7))
    state=words(256,48*4096,2*4096,48*4096,123,-77,19,2,
                48*4096,2*4096,48*4096,1,48,2,48,12345)
    sheep=words(2,2,5,48*4096,2*4096,48*4096,0,0,0,0,0,58,58,61,0,0,0,0,0,0,0)
    pig=words(1,1,6,200*4096,2*4096,200*4096,123,0,0,0,0,21,21,0,0,0,0,17,0,0,0)
    mobs=words(4242,3,256)+words(*([1]*256))+words(1,1)+sheep+pig
    payload=state+inventory+rle+chest+mobs
    checksum=0
    for byte in payload: checksum=(checksum*31+byte)&0xffffffff
    image=words(0x46435354,3,checksum)+payload
    return image+bytes(154*8192-len(image))
