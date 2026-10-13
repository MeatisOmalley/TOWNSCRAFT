/* Host-only baker using the actual pinned sky pass and flood-fill functions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned u32;
#define COMMON_H
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#include "world.h"
int g_W=1024,g_NC=64,g_residentColumns,g_allocChunkCount;
u8 *g_blocks,*g_light,*g_height,*g_columnReady;
int *g_zOff;
short *g_columnMap,*g_columnCoords;
static int strideZ=1024*WH,lightBudget,trackLightDirty;
#define PQ_LEN 16384
static u32 queue[PQ_LEN],*pq=queue,pqHead,pqTail;
static void light_dirty(u32 i,int x,int y,int z) { (void)i;(void)x;(void)y;(void)z; }
#include "world_light_reference.inc"
static unsigned sum(const unsigned char *p,unsigned n)
{
    unsigned h=2166136261u;
    while(n--) h=(h^*p++)*16777619u;
    return h;
}
int main(int argc,char **argv)
{
    unsigned char column[COLUMN_CELLS],record[13*512]={0};
    unsigned checks[4096]={0};
    if(argc!=3) return 1;
    FILE *source=fopen(argv[1],"rb"),*output=fopen(argv[2],"wb");
    if(!source || !output) return 1;
    g_blocks=calloc(1024*1024*WH,1); g_light=calloc(1024*1024*WH,1);
    g_height=calloc(1024*1024,1); g_zOff=calloc(1024,sizeof(int));
    if(!g_blocks || !g_light || !g_height || !g_zOff) return 1;
    for(int z=0;z<1024;++z) g_zOff[z]=z*1024*WH;
    for(int id=0;id<4096;++id) {
        if(fseek(source,(33+id*72)*512,SEEK_SET) || fread(column,1,sizeof(column),source)!=sizeof(column)) return 1;
        for(int cell=0;cell<256;++cell) {
            int x=(id%64)*16+(cell&15),z=(id/64)*16+(cell>>4);
            memcpy(g_blocks+widx(x,0,z),column+cell*WH,WH);
        }
    }
    fclose(source);
    light_init();
    if(fwrite(checks,1,sizeof(checks),output)!=sizeof(checks)) return 1;
    for(int id=0;id<4096;++id) {
        memset(record,0,sizeof(record));
        for(int cell=0;cell<256;++cell) {
            int x=(id%64)*16+(cell&15),z=(id/64)*16+(cell>>4);
            record[cell]=world_height(x,z);
            const u8 *light=g_light+widx(x,0,z);
            for(int y=0;y<WH;y+=2) record[256+(cell*WH+y)/2]=(light[y]>>4)|(light[y+1]&0xf0);
        }
        checks[id]=sum(record,6400);
        if(fwrite(record,1,sizeof(record),output)!=sizeof(record)) return 1;
    }
    if(fseek(output,0,SEEK_SET) || fwrite(checks,1,sizeof(checks),output)!=sizeof(checks) || fclose(output)) return 1;
    printf("LIGHT=4096 columns; exact pinned skylight; packed 4-bit values\n");
    return 0;
}
