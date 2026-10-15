"""Broad climate regions and independent landforms for the offline baker.

Original compact design inspired by the climate/continents/erosion/ridges
separation visible in the supplied Eaglercraft runtime. No game-time noise,
new blocks, biome RAM table, copied assets, or Minecraft generation parity.
"""
from math import isqrt

PLAINS, FOREST, DESERT, HIGHLANDS = range(4)
NAMES=('plains','forest','desert','highlands')
COLORS=((122,163,78),(53,111,56),(218,193,128),(142,147,133))

def sample(x,z,seed,noise,width=1024,sea=20,world_height=48):
    # Warp the lattice smoothly so coastlines/valleys do not follow long
    # straight grid-cell edges. This runs only while baking on the host.
    wx=max(0,min(width-1,x+(noise(x,z,6,seed+60)-128)*48//128))
    wz=max(0,min(width-1,z+(noise(x,z,6,seed+61)-128)*48//128))
    continent=noise(wx,wz,7,seed+31)
    erosion=noise(wx,wz,6,seed+32)
    heat=noise(wx,wz,7,seed+40)
    moisture=noise(wx,wz,7,seed+41)
    ridge=255-abs(2*noise(wx,wz,5,seed+33)-255)
    local=noise(x,z,4,seed+34)-128
    # Continuous landforms: biome boundaries do not create height steps.
    inland=max(0,min(64,continent-104))
    rugged=max(0,min(96,144-erosion))
    upland=(inland*rugged*ridge)//(64*96*20)
    h=sea-1+(continent-108)//9+local*(64+rugged)//(128*32)+upland
    h=max(4,min(world_height-10,h))
    # Wandering low valleys; not a hydrological drainage simulation.
    channel=max(0,16-abs(noise(wx,wz,6,seed+55)-128))
    valley=channel*min(32,max(0,continent-110))
    if h>sea-2: h-=((h-(sea-2))*valley)//(16*32)
    dx,dz=x-width//2,z-width//2
    edge=isqrt((dx*dx+dz*dz)*65536//((width//2)**2))
    if edge>176: h-=(edge-176)//10
    h=max(4,min(world_height-10,h))
    if erosion<110 and continent>145:
        biome=HIGHLANDS
    elif heat>144 and moisture<127:
        biome=DESERT
    elif moisture>140:
        biome=FOREST
    else:
        biome=PLAINS
    return h,biome

def surface(height,biome,slope,detail,sea=20):
    # IDs are the existing byte-per-cell block format.
    if height<=sea+1: return 8
    if biome==DESERT: return 8
    if biome==HIGHLANDS and height>=26 and (slope>=3 or detail>150):
        return 22 if detail>210 else 1
    return 2

def vegetation(biome):
    # Existing peak forest density was 45/1000. Keep this pass below it.
    return ((5,4,15),(32,1,6),(0,0,0),(3,1,5))[biome]
