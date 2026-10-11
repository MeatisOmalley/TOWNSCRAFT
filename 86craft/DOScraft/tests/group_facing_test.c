/* Actual original/adapted collector bodies, not a reimplementation. */
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define COMMON_H
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ABS(a) ((a)<0?-(a):(a))
#define CLAMP(v,a,b) MIN(MAX(v,a),b)
#include "world.h"
#define NPLANES 5
enum {IK_QUAD,IK_MODEL,IK_BOX};
typedef struct {u32 a,b;} Item;
static Item items[8000],before[8000];
static u32 itemKey[8000],keys[8000];
static int nItems,maxItems,camBX,camBY,camBZ,floorOcclusion;
static int rot[3][3],sqTab[256],planeLen[NPLANES];
static int px[NPLANES][50],py[NPLANES][50],pz[NPLANES][50];
static int *PX[NPLANES],*PY[NPLANES],*PZ[NPLANES];
static u8 visX[50],visY[50],visZ[50];
int g_W=32,g_NC=2,g_residentColumns=4,g_viewDist;
short *g_columnMap;
static short map[4];
static Chunk chunks[15];
Chunk *g_chunks=chunks;
static u32 pool[20000];
u32 *g_meshPool=pool;
static int earlyGroups;
static u32 state=0x387486aa;
static unsigned int random_value(void) {
    state^=state<<13;state^=state>>17;state^=state<<5;return state;
}
#include "collect_helpers.inc"
#include "collect_reference.inc"
#include "collect_candidate.inc"
static void meshes(void) {
    unsigned int c,g,k,at=0;
    memset(chunks,0,sizeof(chunks));
    for(c=0;c<12;++c) {
        Chunk *ch=chunks+c;
        ch->off=at/2;ch->meshed=1;ch->sealed=random_value()%4==0;
        for(g=0;g<24;++g) {
            int dir=g/4,sub=g%4;
            int count=random_value()%9,mnx=15,mny=15,mnz=15,mxx=0,mxy=0,mxz=0;
            ch->group[g]=(u16)count;
            for(k=0;k<(unsigned int)count;++k) {
                int x=(sub&1)*8+random_value()%8,z=((sub>>1)&1)*8+random_value()%8;
                int y=random_value()%16,coordinate;
                int w,h,ex,ez;
                coordinate=(dir&1)?(int)k:7-(int)k;
                if(dir<2) x=(sub&1)*8+coordinate;
                else if(dir<4) y=coordinate*2;
                else z=((sub>>1)&1)*8+coordinate;
                w=1+random_value()%MIN(4,16-x);h=1+random_value()%MIN(4,16-z);
                ex=dir<2?1:w;ez=dir<4?h:1;
                pool[at++]=(u32)x|(u32)z<<4|(u32)y<<8|(u32)dir<<12|(u32)(w-1)<<16|(u32)(h-1)<<20;
                pool[at++]=random_value()&0xffffff;
                mnx=MIN(mnx,x);mny=MIN(mny,y);mnz=MIN(mnz,z);
                mxx=MAX(mxx,x+ex-1);mxy=MAX(mxy,y);mxz=MAX(mxz,z+ez-1);
            }
            if(random_value()%4==0) {mnx=mny=mnz=0;mxx=mxy=mxz=15;} /* Loose partial-rebuild boxes. */
            ch->gbox[g][0]=(u8)(mnx|mxx<<4);ch->gbox[g][1]=(u8)(mnz|mxz<<4);
            ch->gbox[g][2]=(u8)(mny|mxy<<4);ch->count+=(u16)count;
        }
        ch->group[24]=3;ch->count+=3;
        for(k=0;k<3;++k) {
            pool[at++]=MQ_MODEL|(random_value()&0xfff);pool[at++]=random_value()&0xffffff;
        }
    }
    assert(at<20000);
}
int main(void) {
    int scene,k,i,referenceCount;
    for(i=0;i<256;++i) sqTab[i]=i*i;
    for(k=0;k<NPLANES;++k) {PX[k]=px[k];PY[k]=py[k];PZ[k]=pz[k];}
    for(scene=0;scene<6000;++scene) {
        int fractional=scene%3==0?0:(scene%3==1?128:255);
        camBX=random_value()%32;camBY=random_value()%48;camBZ=random_value()%32;
        g_viewDist=scene%2?8:12;floorOcclusion=random_value()%49;
        rot[2][0]=scene%2?16384:0;rot[2][2]=scene%2?0:16384;
        maxItems=scene%11==0?8:(scene%7==0?100:8000);
        for(i=0;i<50;++i) {
            visX[i]=(u8)(((i*256-camBX*256-fractional)>0?1:0)|
                        ((i*256+256-camBX*256-fractional)<0?2:0));
            visY[i]=(u8)(((i*256-camBY*256-fractional)>0?4:0)|
                        ((i*256+256-camBY*256-fractional)<0?8:0));
            visZ[i]=(u8)(((i*256-camBZ*256-fractional)>0?16:0)|
                        ((i*256+256-camBZ*256-fractional)<0?32:0));
        }
        for(k=0;k<NPLANES;++k) {
            int a=scene%4?(int)(random_value()%129)-64:0;
            int b=scene%4?(int)(random_value()%129)-64:0;
            int c=scene%4?(int)(random_value()%129)-64:0;
            planeLen[k]=MAX(1,ABS(a)+ABS(b)+ABS(c));
            for(i=0;i<50;++i) {
                px[k][i]=(i-camBX)*256*a;py[k][i]=(i-camBY)*256*b;pz[k][i]=(i-camBZ)*256*c;
            }
        }
        meshes();
        g_columnMap=scene%3?NULL:map;
        for(i=0;i<4;++i) map[i]=(short)(scene%5==0 && i==2?-1:3-i);
        memset(items,0xa5,sizeof(items));memset(itemKey,0xa5,sizeof(itemKey));nItems=0;
        collect_reference();referenceCount=nItems;
        memcpy(before,items,sizeof(items));memcpy(keys,itemKey,sizeof(keys));
        memset(items,0xa5,sizeof(items));memset(itemKey,0xa5,sizeof(itemKey));nItems=0;
        collect_candidate();
        assert(nItems==referenceCount && !memcmp(before,items,sizeof(items)) && !memcmp(keys,itemKey,sizeof(keys)));
    }
    assert(earlyGroups>1000);
    printf("PASS: 6000 actual collector lists/keys/capacity/loose bounds/residency, early groups %d\n",earlyGroups);
    return 0;
}
